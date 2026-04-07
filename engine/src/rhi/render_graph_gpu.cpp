#include "rhi/render_graph.hpp"
#include "rhi/render_graph_gpu.hpp"

#include "rhi/command_buffer.hpp"
#include "rhi/descriptor_layout_cache.hpp"
#include "rhi/graphics_pipeline_cache.hpp"
#include "rhi/pipeline_key.hpp"
#include "rhi/reflected_descriptors.hpp"
#include "rhi/shader_reflection.hpp"
#include "rhi/spirv_hash.hpp"
#include "rhi/swapchian.hpp"

#include "core/log/logger.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace rhi {

namespace {

[[nodiscard]] bool named_slot_matches_descriptor_binding(
    const RgPassGpuNamedBufferSlot &s, const DescriptorBinding &b) noexcept {
    if (b.resource_name != s.resource_name) {
        return false;
    }
    if (s.kind == RgPassGpuNamedBufferKind::Uniform) {
        return b.descriptor_type == vk::DescriptorType::eUniformBuffer ||
               b.descriptor_type == vk::DescriptorType::eUniformBufferDynamic;
    }
    return b.descriptor_type == vk::DescriptorType::eStorageBuffer;
}

[[nodiscard]] bool merge_reflect_and_promote_gpu_pass(const RgPassGpuDesc &gpu,
                                                      ShaderReflection &out_merged) {
    if (gpu.vert_spv == nullptr || gpu.frag_spv == nullptr ||
        gpu.vert_spv->empty() || gpu.frag_spv->empty() ||
        (gpu.vert_spv->size() % 4) != 0 || (gpu.frag_spv->size() % 4) != 0) {
        LUMEN_LOG_ERROR("merge_reflect_and_promote_gpu_pass: SPIR-V 无效");
        return false;
    }
    const std::span<const std::uint32_t> vert_words {
        reinterpret_cast<const std::uint32_t *>(gpu.vert_spv->data()),
        gpu.vert_spv->size() / 4
    };
    const std::span<const std::uint32_t> frag_words {
        reinterpret_cast<const std::uint32_t *>(gpu.frag_spv->data()),
        gpu.frag_spv->size() / 4
    };
    const std::optional<ShaderReflection> refl_v =
        reflect_spirv(vert_words, vk::ShaderStageFlagBits::eVertex);
    const std::optional<ShaderReflection> refl_f =
        reflect_spirv(frag_words, vk::ShaderStageFlagBits::eFragment);
    if (!refl_v || !refl_f) {
        LUMEN_LOG_ERROR("merge_reflect_and_promote_gpu_pass: 着色器反射失败");
        return false;
    }
    out_merged = *refl_v;
    if (!merge_reflection(out_merged, *refl_f)) {
        LUMEN_LOG_ERROR("merge_reflect_and_promote_gpu_pass: 合并 VS/FS 反射失败");
        return false;
    }
    for (const RgPassGpuNamedBufferSlot &slot : gpu.named_buffer_slots) {
        if (slot.kind == RgPassGpuNamedBufferKind::Uniform &&
            slot.dynamic_stride > 0) {
            if (!promote_uniform_binding_to_dynamic_by_name(out_merged,
                                                            slot.resource_name)) {
                LUMEN_LOG_ERROR(
                    "merge_reflect_and_promote_gpu_pass: promote 失败 \"{}\"",
                    slot.resource_name);
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t
buffer_binding_count_in_merged(const ShaderReflection &merged) {
    std::uint32_t n = 0;
    for (const DescriptorBinding &b : merged.bindings) {
        switch (b.descriptor_type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            ++n;
            break;
        default:
            break;
        }
    }
    return n;
}

[[nodiscard]] std::vector<std::uintptr_t>
collect_buffer_binding_vk_pointers(const RenderGraph &rg,
                                   const RgPassGpuDesc &gpu,
                                   const ShaderReflection &merged) {
    std::vector<std::uintptr_t> out;
    const std::uint32_t nbuf = buffer_binding_count_in_merged(merged);
    out.reserve(nbuf);
    for (const DescriptorBinding &b : merged.bindings) {
        switch (b.descriptor_type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            break;
        default:
            continue;
        }
        const RgPassGpuNamedBufferSlot *slot = nullptr;
        for (const RgPassGpuNamedBufferSlot &s : gpu.named_buffer_slots) {
            if (named_slot_matches_descriptor_binding(s, b)) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr) {
            return {};
        }
        const vk::Buffer vkbuf = rg.resource_vk_buffer(slot->resource);
        if (!vkbuf) {
            return {};
        }
        out.push_back(reinterpret_cast<std::uintptr_t>(
            static_cast<VkBuffer>(vkbuf)));
    }
    if (out.size() != static_cast<std::size_t>(nbuf)) {
        return {};
    }
    return out;
}

[[nodiscard]] std::uint32_t
count_dynamic_uniforms_in_layout_order(const ShaderReflection &merged) {
    std::map<std::uint32_t, std::vector<const DescriptorBinding *>> by_set;
    for (const DescriptorBinding &b : merged.bindings) {
        switch (b.descriptor_type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            by_set[b.set].push_back(&b);
            break;
        default:
            break;
        }
    }
    if (by_set.empty()) {
        return 0;
    }
    std::uint32_t dyn = 0;
    const std::uint32_t max_set = by_set.rbegin()->first;
    for (std::uint32_t s = 0; s <= max_set; ++s) {
        const auto it = by_set.find(s);
        if (it == by_set.end()) {
            continue;
        }
        std::vector<const DescriptorBinding *> vec = it->second;
        std::sort(vec.begin(), vec.end(),
                  [](const DescriptorBinding *a, const DescriptorBinding *x) {
                      return a->binding < x->binding;
                  });
        for (const DescriptorBinding *bp : vec) {
            if (bp->descriptor_type ==
                vk::DescriptorType::eUniformBufferDynamic) {
                ++dyn;
            }
        }
    }
    return dyn;
}

[[nodiscard]] bool build_reflected_bindings_for_pass(
    const RenderGraph &rg, const RgPassGpuDesc &gpu,
    const ShaderReflection &merged, std::vector<ReflectedBufferBinding> &out) {
    out.clear();
    out.reserve(merged.bindings.size());
    for (const DescriptorBinding &b : merged.bindings) {
        switch (b.descriptor_type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            break;
        default:
            LUMEN_LOG_ERROR(
                "build_reflected_bindings: 暂不支持的描述符类型 set={} binding={}",
                b.set, b.binding);
            return false;
        }
        const RgPassGpuNamedBufferSlot *slot = nullptr;
        for (const RgPassGpuNamedBufferSlot &s : gpu.named_buffer_slots) {
            if (named_slot_matches_descriptor_binding(s, b)) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr) {
            LUMEN_LOG_ERROR(
                "build_reflected_bindings: 缺少 named_buffer_slots 映射 name=\"{}\" "
                "set={} binding={}",
                b.resource_name, b.set, b.binding);
            return false;
        }
        if (!slot->resource.valid()) {
            LUMEN_LOG_ERROR("build_reflected_bindings: 无效 resource id");
            return false;
        }
        const vk::Buffer vkbuf = rg.resource_vk_buffer(slot->resource);
        if (!vkbuf) {
            LUMEN_LOG_ERROR(
                "build_reflected_bindings: 资源未 bind_buffer name=\"{}\"",
                b.resource_name);
            return false;
        }
        const BufferHandle rh = rg.resource_rhi_buffer(slot->resource);
        if (!is_valid(rh)) {
            LUMEN_LOG_ERROR(
                "build_reflected_bindings: 须在 bind_buffer 中传入 BufferHandle 以写描述符 "
                "name=\"{}\"",
                b.resource_name);
            return false;
        }
        ReflectedBufferBinding rb {};
        rb.set = b.set;
        rb.binding = b.binding;
        rb.buffer = rh;
        rb.buffer_offset = 0;
        if (b.descriptor_type == vk::DescriptorType::eUniformBufferDynamic) {
            if (slot->dynamic_stride == 0) {
                LUMEN_LOG_ERROR(
                    "build_reflected_bindings: dynamic UBO \"{}\" 须设置 "
                    "gpu_bind_uniform_buffer 的 dynamic_stride",
                    b.resource_name);
                return false;
            }
            rb.range = slot->dynamic_stride;
        } else {
            const vk::DeviceSize bsz =
                rg.resource_buffer_bound_size(slot->resource);
            if (bsz == 0) {
                LUMEN_LOG_ERROR(
                    "build_reflected_bindings: 静态缓冲需要非零 buffer_size（bind_buffer） "
                    "name=\"{}\"",
                    b.resource_name);
                return false;
            }
            rb.range = bsz;
        }
        out.push_back(rb);
    }
    return true;
}

[[nodiscard]] bool build_present_graphics_pass(
    RenderGraph &rg, const std::size_t pass_index, RgGpuCompileContext &ctx,
    RgGpuCompiledPass &out) {
    const RgPass &pass = rg.passes()[pass_index];
    if (!pass.gpu.has_value()) {
        return false;
    }
    const RgPassGpuDesc &gpu = *pass.gpu;
    if (gpu.vert_spv == nullptr || gpu.frag_spv == nullptr ||
        gpu.vert_spv->empty() || gpu.frag_spv->empty() ||
        (gpu.vert_spv->size() % 4) != 0 || (gpu.frag_spv->size() % 4) != 0) {
        LUMEN_LOG_ERROR("build_present_graphics_pass: SPIR-V 无效");
        return false;
    }

    const std::span<const std::uint32_t> vert_words {
        reinterpret_cast<const std::uint32_t *>(gpu.vert_spv->data()),
        gpu.vert_spv->size() / 4
    };

    std::vector<vk::VertexInputBindingDescription> vertex_bindings =
        gpu.vertex_bindings;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes =
        gpu.vertex_attributes;
    const bool have_bindings = !vertex_bindings.empty();
    const bool have_attrs = !vertex_attributes.empty();
    if (have_bindings != have_attrs) {
        LUMEN_LOG_ERROR(
            "build_present_graphics_pass: vertex bindings/attributes 须同时提供或同时留空（留空则从 VS 反射）");
        return false;
    }
    if (!have_bindings) {
        const std::optional<ReflectedVertexInput> vi =
            reflect_vertex_input_interleaved(vert_words);
        if (!vi.has_value()) {
            LUMEN_LOG_ERROR(
                "build_present_graphics_pass: 从顶点着色器反射 vertex input 失败");
            return false;
        }
        vertex_bindings = std::move(vi->bindings);
        vertex_attributes = std::move(vi->attributes);
    }

    Device &rdev = *ctx.rhi_device;
    const vk::Device dev = ctx.vk_device;
    const Swapchain &swap = *ctx.swapchain;

    ShaderReflection merged {};
    if (!merge_reflect_and_promote_gpu_pass(gpu, merged)) {
        LUMEN_LOG_ERROR("build_present_graphics_pass: 反射或 promote 失败");
        return false;
    }

    std::vector<vk::DescriptorSetLayout> set_layouts {};
    if (!create_reflected_pipeline_layouts(
            dev, merged, *ctx.dsl_cache, *ctx.pl_cache, set_layouts,
            out.pipeline_layout)) {
        LUMEN_LOG_ERROR(
            "build_present_graphics_pass: create_reflected_pipeline_layouts 失败");
        return false;
    }

    const vk::Format color_fmt = swap.format();

    vk::AttachmentDescription color {};
    color.format = color_fmt;
    color.samples = vk::SampleCountFlagBits::e1;
    color.loadOp = vk::AttachmentLoadOp::eClear;
    color.storeOp = vk::AttachmentStoreOp::eStore;
    color.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color.initialLayout = vk::ImageLayout::eUndefined;
    color.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference color_ref {};
    color_ref.attachment = 0;
    color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription sub {};
    sub.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    vk::SubpassDependency dep {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo rpci {};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    const vk::Result rpr =
        dev.createRenderPass(&rpci, nullptr, &out.render_pass);
    if (rpr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("build_present_graphics_pass: createRenderPass 失败 {}",
                        static_cast<int>(rpr));
        return false;
    }

    const vk::DynamicState dyn_states[] = { vk::DynamicState::eViewport,
                                            vk::DynamicState::eScissor };

    GraphicsPipelineKey pk {};
    pk.vert_spv_hash = hash_spirv_bytes(std::span<const std::byte> {
        gpu.vert_spv->data(), gpu.vert_spv->size() });
    pk.frag_spv_hash = hash_spirv_bytes(std::span<const std::byte> {
        gpu.frag_spv->data(), gpu.frag_spv->size() });
    pk.pipeline_layout = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
        static_cast<VkPipelineLayout>(out.pipeline_layout)));
    pk.render_pass = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
        static_cast<VkRenderPass>(out.render_pass)));
    pk.subpass = 0;
    pk.vertex_bindings = std::move(vertex_bindings);
    pk.vertex_attributes = std::move(vertex_attributes);
    pk.topology = vk::PrimitiveTopology::eTriangleList;
    pk.dynamic_viewport_scissor = true;
    pk.polygon_mode = vk::PolygonMode::eFill;
    pk.cull_mode = vk::CullModeFlagBits::eNone;
    pk.front_face = vk::FrontFace::eCounterClockwise;
    pk.line_width = 1.0F;
    pk.rasterization_samples = vk::SampleCountFlagBits::e1;
    pk.depth_stencil = std::nullopt;
    ColorBlendAttachmentKey cbk {};
    cbk.color_write_mask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    pk.color_blend_attachments.push_back(cbk);
    pk.dynamic_states.assign(std::begin(dyn_states), std::end(dyn_states));

    out.pipeline = rdev.graphics_pipeline_cache().get_or_create(
        dev, rdev.vk_pipeline_cache(), rdev.shader_module_cache(), pk,
        std::span<const std::byte> { gpu.vert_spv->data(), gpu.vert_spv->size() },
        std::span<const std::byte> { gpu.frag_spv->data(), gpu.frag_spv->size() });
    if (!out.pipeline) {
        LUMEN_LOG_ERROR(
            "build_present_graphics_pass: GraphicsPipelineCache::get_or_create 失败");
        dev.destroyRenderPass(out.render_pass, nullptr);
        out.render_pass = nullptr;
        out.pipeline_layout = nullptr;
        return false;
    }

    if (!rebuild_swapchain_present_framebuffers(dev, swap, out.render_pass,
                                                out.framebuffers)) {
        LUMEN_LOG_ERROR(
            "build_present_graphics_pass: rebuild_swapchain_present_framebuffers 失败");
        rdev.graphics_pipeline_cache().erase_pipeline(dev, out.pipeline);
        out.pipeline = nullptr;
        dev.destroyRenderPass(out.render_pass, nullptr);
        out.render_pass = nullptr;
        out.pipeline_layout = nullptr;
        return false;
    }

    out.merged_reflection = merged;

    if (!merged.bindings.empty()) {
        constexpr std::uint32_t k_sets_per_layout = 1u;
        std::vector<vk::DescriptorSet> allocated {};
        if (!allocate_reflected_descriptor_pool_and_sets(
                dev, merged, set_layouts, k_sets_per_layout, out.descriptor_pool,
                allocated)) {
            LUMEN_LOG_ERROR(
                "build_present_graphics_pass: allocate_reflected_descriptor_pool_and_sets "
                "失败");
            goto fail_after_pipeline;
        }
        if (allocated.size() != set_layouts.size()) {
            LUMEN_LOG_ERROR(
                "build_present_graphics_pass: 描述符集数量与 set 布局不一致");
            if (out.descriptor_pool) {
                dev.destroyDescriptorPool(out.descriptor_pool, nullptr);
                out.descriptor_pool = nullptr;
            }
            goto fail_after_pipeline;
        }
        out.descriptor_sets_by_set = std::move(allocated);

        std::vector<ReflectedBufferBinding> buf_binds {};
        if (!build_reflected_bindings_for_pass(rg, gpu, merged, buf_binds)) {
            goto fail_after_pool;
        }
        if (!update_reflected_buffer_descriptors(
                dev, merged, out.descriptor_sets_by_set, rdev, buf_binds)) {
            LUMEN_LOG_ERROR(
                "build_present_graphics_pass: update_reflected_buffer_descriptors 失败");
            goto fail_after_pool;
        }
    }

    return true;

fail_after_pool:
    if (out.descriptor_pool) {
        dev.destroyDescriptorPool(out.descriptor_pool, nullptr);
        out.descriptor_pool = nullptr;
    }
    out.descriptor_sets_by_set.clear();
fail_after_pipeline:
    for (vk::Framebuffer fb : out.framebuffers) {
        if (fb) {
            dev.destroyFramebuffer(fb, nullptr);
        }
    }
    out.framebuffers.clear();
    rdev.graphics_pipeline_cache().erase_pipeline(dev, out.pipeline);
    out.pipeline = nullptr;
    if (out.render_pass) {
        dev.destroyRenderPass(out.render_pass, nullptr);
        out.render_pass = nullptr;
    }
    out.pipeline_layout = nullptr;
    out.merged_reflection = {};
    return false;
}

[[nodiscard]] bool update_pass_descriptors_only(
    RenderGraph &rg, const std::size_t pass_index, RgGpuCompileContext &ctx,
    RgGpuCompiledPass &cp) {
    const RgPass &pass = rg.passes()[pass_index];
    if (!pass.gpu.has_value()) {
        return false;
    }
    const RgPassGpuDesc &gpu = *pass.gpu;
    const ShaderReflection &merged = cp.merged_reflection;
    if (merged.bindings.empty()) {
        return true;
    }
    std::vector<ReflectedBufferBinding> buf_binds {};
    if (!build_reflected_bindings_for_pass(rg, gpu, merged, buf_binds)) {
        return false;
    }
    return update_reflected_buffer_descriptors(ctx.vk_device, merged,
                                               cp.descriptor_sets_by_set,
                                               *ctx.rhi_device, buf_binds);
}

} // namespace

void destroy_rg_gpu_compiled_pass(vk::Device dev, RgGpuCompiledPass &p,
                                  GraphicsPipelineCache *gfx_cache) {
    for (vk::Framebuffer fb : p.framebuffers) {
        if (fb) {
            dev.destroyFramebuffer(fb, nullptr);
        }
    }
    p.framebuffers.clear();
    if (gfx_cache != nullptr && static_cast<bool>(p.pipeline)) {
        gfx_cache->erase_pipeline(dev, p.pipeline);
    }
    p.pipeline = nullptr;
    if (p.descriptor_pool) {
        dev.destroyDescriptorPool(p.descriptor_pool, nullptr);
        p.descriptor_pool = nullptr;
    }
    p.descriptor_sets_by_set.clear();
    p.pipeline_layout = nullptr;
    if (p.render_pass) {
        dev.destroyRenderPass(p.render_pass, nullptr);
        p.render_pass = nullptr;
    }
    p.merged_reflection = {};
}

void RgGpuCompileContext::destroy_all(vk::Device dev,
                                      GraphicsPipelineCache *gfx_cache) {
    for (std::optional<RgGpuCompiledPass> &opt : compiled) {
        if (opt.has_value()) {
            destroy_rg_gpu_compiled_pass(dev, *opt, gfx_cache);
            opt.reset();
        }
    }
    compiled.clear();
    pass_caches.clear();
}

bool rg_record_builtin_present_graphics_pass(
    const RenderGraph &rg, const std::size_t pass_index, CommandBuffer &cmd,
    const RgGpuCompiledPass &cp, const RgGpuExecuteFrame &frame) {
    if (frame.rhi_device == nullptr) {
        LUMEN_LOG_ERROR("rg_record_builtin_present_graphics_pass: rhi_device 为空");
        return false;
    }
    if (pass_index >= rg.passes().size()) {
        LUMEN_LOG_ERROR("rg_record_builtin_present_graphics_pass: pass_index 越界");
        return false;
    }
    const RgPass &pass = rg.passes()[pass_index];
    if (!pass.gpu.has_value()) {
        LUMEN_LOG_ERROR(
            "rg_record_builtin_present_graphics_pass: Pass \"{}\" 无 gpu 描述",
            pass.name);
        return false;
    }
    const RgPassGpuDesc &gpu = *pass.gpu;

    if (!cp.render_pass || !cp.pipeline || !cp.pipeline_layout) {
        LUMEN_LOG_ERROR(
            "rg_record_builtin_present_graphics_pass: Pass \"{}\" GPU 编译态不完整",
            pass.name);
        return false;
    }
    if (frame.swap_image_index >= cp.framebuffers.size() ||
        !cp.framebuffers[frame.swap_image_index]) {
        LUMEN_LOG_ERROR(
            "rg_record_builtin_present_graphics_pass: Pass \"{}\" framebuffer 无效 "
            "image_index={}",
            pass.name, frame.swap_image_index);
        return false;
    }

    const std::uint32_t dyn_expected =
        count_dynamic_uniforms_in_layout_order(cp.merged_reflection);
    if (frame.dynamic_uniform_offsets.size() !=
        static_cast<std::size_t>(dyn_expected)) {
        LUMEN_LOG_ERROR(
            "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
            "dynamic_uniform_offsets 数量 {} != 管线布局中 dynamic UBO 数 {}",
            pass.name, frame.dynamic_uniform_offsets.size(), dyn_expected);
        return false;
    }

    vk::ClearValue clear {};
    clear.color = vk::ClearColorValue(gpu.color_attachment_clear);

    vk::RenderPassBeginInfo rpbi {};
    rpbi.renderPass = cp.render_pass;
    rpbi.framebuffer = cp.framebuffers[frame.swap_image_index];
    rpbi.renderArea.offset = vk::Offset2D { 0, 0 };
    rpbi.renderArea.extent = frame.surface_extent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;

    cmd.begin_render_pass(rpbi, vk::SubpassContents::eInline);
    cmd.bind_pipeline(vk::PipelineBindPoint::eGraphics, cp.pipeline);

    if (!cp.merged_reflection.bindings.empty()) {
        if (cp.descriptor_sets_by_set.empty()) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" 缺少描述符集",
                pass.name);
            cmd.end_render_pass();
            return false;
        }
        for (const vk::DescriptorSet ds : cp.descriptor_sets_by_set) {
            if (!ds) {
                LUMEN_LOG_ERROR(
                    "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                    "descriptor_sets_by_set 含空句柄",
                    pass.name);
                cmd.end_render_pass();
                return false;
            }
        }
        std::vector<std::uint32_t> dyn_u32;
        dyn_u32.reserve(frame.dynamic_uniform_offsets.size());
        for (const vk::DeviceSize o : frame.dynamic_uniform_offsets) {
            dyn_u32.push_back(static_cast<std::uint32_t>(o));
        }
        cmd.bind_descriptor_sets(vk::PipelineBindPoint::eGraphics,
                                 cp.pipeline_layout, 0,
                                 cp.descriptor_sets_by_set, dyn_u32);
    }

    if (gpu.draw_index_count > 0) {
        if (!gpu.draw_index_buffer.valid()) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                "draw_index_count>0 但 draw_index_buffer 无效",
                pass.name);
            cmd.end_render_pass();
            return false;
        }
        if (gpu.draw_vertex_count > 0) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                "索引与非索引 draw 字段不能同时有效",
                pass.name);
            cmd.end_render_pass();
            return false;
        }

        vk::Viewport vp {};
        vp.x = 0.0F;
        vp.y = 0.0F;
        vp.width = static_cast<float>(frame.surface_extent.width);
        vp.height = static_cast<float>(frame.surface_extent.height);
        vp.minDepth = 0.0F;
        vp.maxDepth = 1.0F;
        cmd.set_viewport(0, std::array<vk::Viewport, 1> { vp });

        vk::Rect2D sc {};
        sc.offset = vk::Offset2D { 0, 0 };
        sc.extent = frame.surface_extent;
        cmd.set_scissor(0, std::array<vk::Rect2D, 1> { sc });

        const vk::Buffer vb = rg.resource_vk_buffer(gpu.draw_vertex_buffer);
        if (!vb) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                "draw_vertex_buffer 未 bind_buffer",
                pass.name);
            cmd.end_render_pass();
            return false;
        }
        const vk::Buffer ib = rg.resource_vk_buffer(gpu.draw_index_buffer);
        if (!ib) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                "draw_index_buffer 未 bind_buffer",
                pass.name);
            cmd.end_render_pass();
            return false;
        }
        const std::array<vk::Buffer, 1> bufs { vb };
        const std::array<vk::DeviceSize, 1> offs { 0 };
        cmd.bind_vertex_buffers(0, bufs, offs);
        cmd.bind_index_buffer(ib, 0, gpu.draw_index_type);
        cmd.draw_indexed(gpu.draw_index_count, gpu.draw_instance_count,
                         gpu.draw_first_index, gpu.draw_index_vertex_offset, 0);
    } else if (gpu.draw_vertex_count > 0) {
        vk::Viewport vp {};
        vp.x = 0.0F;
        vp.y = 0.0F;
        vp.width = static_cast<float>(frame.surface_extent.width);
        vp.height = static_cast<float>(frame.surface_extent.height);
        vp.minDepth = 0.0F;
        vp.maxDepth = 1.0F;
        cmd.set_viewport(0, std::array<vk::Viewport, 1> { vp });

        vk::Rect2D sc {};
        sc.offset = vk::Offset2D { 0, 0 };
        sc.extent = frame.surface_extent;
        cmd.set_scissor(0, std::array<vk::Rect2D, 1> { sc });

        const vk::Buffer vb = rg.resource_vk_buffer(gpu.draw_vertex_buffer);
        if (!vb) {
            LUMEN_LOG_ERROR(
                "rg_record_builtin_present_graphics_pass: Pass \"{}\" "
                "draw_vertex_buffer 未 bind_buffer",
                pass.name);
            cmd.end_render_pass();
            return false;
        }
        const std::array<vk::Buffer, 1> bufs { vb };
        const std::array<vk::DeviceSize, 1> offs { 0 };
        cmd.bind_vertex_buffers(0, bufs, offs);
        cmd.draw(gpu.draw_vertex_count, gpu.draw_instance_count,
                 gpu.draw_first_vertex, 0);
    }

    cmd.end_render_pass();
    return true;
}

bool compile_render_graph_gpu_phase(RenderGraph &rg, RgGpuCompileContext &ctx) {
    if (ctx.rhi_device == nullptr || !ctx.vk_device || ctx.swapchain == nullptr ||
        ctx.dsl_cache == nullptr || ctx.pl_cache == nullptr) {
        LUMEN_LOG_ERROR("compile_render_graph_gpu_phase: RgGpuCompileContext 未填满");
        return false;
    }
    GraphicsPipelineCache *const gfx = &ctx.rhi_device->graphics_pipeline_cache();
    const std::size_t n = rg.passes().size();

    while (ctx.compiled.size() > n) {
        const std::size_t li = ctx.compiled.size() - 1;
        if (ctx.compiled[li].has_value()) {
            destroy_rg_gpu_compiled_pass(ctx.vk_device, *ctx.compiled[li], gfx);
            ctx.compiled[li].reset();
        }
        ctx.compiled.pop_back();
        if (!ctx.pass_caches.empty()) {
            ctx.pass_caches.pop_back();
        }
    }
    while (ctx.compiled.size() < n) {
        ctx.compiled.push_back(std::nullopt);
        ctx.pass_caches.push_back({});
    }
    ctx.pass_caches.resize(n);

    const Swapchain &swap = *ctx.swapchain;

    for (std::size_t i = 0; i < n; ++i) {
        const RgPass &p = rg.passes()[i];
        if (!p.gpu.has_value()) {
            if (ctx.compiled[i].has_value()) {
                destroy_rg_gpu_compiled_pass(ctx.vk_device, *ctx.compiled[i],
                                             gfx);
                ctx.compiled[i].reset();
            }
            ctx.pass_caches[i] = {};
            continue;
        }

        const RgPassGpuDesc &gpu = *p.gpu;
        if (gpu.vert_spv == nullptr || gpu.frag_spv == nullptr) {
            LUMEN_LOG_ERROR(
                "compile_render_graph_gpu_phase: Pass \"{}\" gpu_shaders 无效",
                p.name);
            return false;
        }

        ShaderReflection merged_for_cache {};
        if (!merge_reflect_and_promote_gpu_pass(gpu, merged_for_cache)) {
            LUMEN_LOG_ERROR(
                "compile_render_graph_gpu_phase: Pass \"{}\" 反射或 promote 失败",
                p.name);
            return false;
        }
        const std::uint64_t vh = hash_spirv_bytes(std::span<const std::byte> {
            gpu.vert_spv->data(), gpu.vert_spv->size() });
        const std::uint64_t fh = hash_spirv_bytes(std::span<const std::byte> {
            gpu.frag_spv->data(), gpu.frag_spv->size() });
        const std::vector<std::uintptr_t> ptrs =
            collect_buffer_binding_vk_pointers(rg, gpu, merged_for_cache);
        if (ptrs.size() != static_cast<std::size_t>(
                buffer_binding_count_in_merged(merged_for_cache))) {
            LUMEN_LOG_ERROR(
                "compile_render_graph_gpu_phase: Pass \"{}\" 缓冲绑定资源无效（bind_buffer）",
                p.name);
            return false;
        }

        RgGpuPassBuildCache &cache = ctx.pass_caches[i];

        if (ctx.compiled[i].has_value() && cache.pipeline_valid &&
            vh == cache.vert_hash && fh == cache.frag_hash &&
            static_cast<int>(swap.format()) == cache.swap_format &&
            ptrs == cache.uniform_vk_buffers) {
            if (swap.extent().width != cache.extent.width ||
                swap.extent().height != cache.extent.height ||
                swap.image_count() != cache.image_count) {
                RgGpuCompiledPass &cp = *ctx.compiled[i];
                for (vk::Framebuffer fb : cp.framebuffers) {
                    if (fb) {
                        ctx.vk_device.destroyFramebuffer(fb, nullptr);
                    }
                }
                cp.framebuffers.clear();
                if (!rebuild_swapchain_present_framebuffers(
                        ctx.vk_device, swap, cp.render_pass, cp.framebuffers)) {
                    LUMEN_LOG_ERROR(
                        "compile_render_graph_gpu_phase: 仅重建 framebuffer 失败 Pass "
                        "\"{}\"",
                        p.name);
                    return false;
                }
                cache.extent = swap.extent();
                cache.image_count = swap.image_count();
            }
            continue;
        }

        if (ctx.compiled[i].has_value() && cache.pipeline_valid &&
            vh == cache.vert_hash && fh == cache.frag_hash &&
            static_cast<int>(swap.format()) == cache.swap_format &&
            swap.extent().width == cache.extent.width &&
            swap.extent().height == cache.extent.height &&
            swap.image_count() == cache.image_count &&
            ptrs != cache.uniform_vk_buffers) {
            if (!update_pass_descriptors_only(rg, i, ctx, *ctx.compiled[i])) {
                LUMEN_LOG_ERROR(
                    "compile_render_graph_gpu_phase: 更新描述符失败 Pass \"{}\"",
                    p.name);
                return false;
            }
            cache.uniform_vk_buffers = ptrs;
            continue;
        }

        if (ctx.compiled[i].has_value()) {
            destroy_rg_gpu_compiled_pass(ctx.vk_device, *ctx.compiled[i], gfx);
            ctx.compiled[i].reset();
        }
        cache = {};

        RgGpuCompiledPass built {};
        if (!build_present_graphics_pass(rg, i, ctx, built)) {
            LUMEN_LOG_ERROR(
                "compile_render_graph_gpu_phase: 构建 GPU Pass 失败 \"{}\"",
                p.name);
            return false;
        }
        ctx.compiled[i] = std::move(built);

        cache.vert_hash = vh;
        cache.frag_hash = fh;
        cache.swap_format = static_cast<int>(swap.format());
        cache.extent = swap.extent();
        cache.image_count = swap.image_count();
        cache.uniform_vk_buffers = std::move(ptrs);
        cache.pipeline_valid = true;
    }

    return true;
}

} // namespace rhi
