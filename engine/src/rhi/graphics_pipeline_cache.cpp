#include "rhi/graphics_pipeline_cache.hpp"

#include "rhi/spirv_hash.hpp"

#include "core/log/logger.hpp"

#include <vector>

namespace rhi {

namespace {

[[nodiscard]] vk::PipelineLayout layout_from_u64(const std::uint64_t h) {
    return vk::PipelineLayout { reinterpret_cast<VkPipelineLayout>(h) };
}

[[nodiscard]] vk::RenderPass render_pass_from_u64(const std::uint64_t h) {
    return vk::RenderPass { reinterpret_cast<VkRenderPass>(h) };
}

[[nodiscard]] bool hashes_match(const GraphicsPipelineKey &key,
                                const std::span<const std::byte> vert_spv,
                                const std::span<const std::byte> frag_spv) {
    return hash_spirv_bytes(vert_spv) == key.vert_spv_hash &&
           hash_spirv_bytes(frag_spv) == key.frag_spv_hash;
}

} // namespace

vk::Pipeline GraphicsPipelineCache::get_or_create(
    const vk::Device device, const vk::PipelineCache pipeline_cache,
    ShaderModuleCache &shader_modules, const GraphicsPipelineKey &key,
    const std::span<const std::byte> vert_spv,
    const std::span<const std::byte> frag_spv) {
    if (!device) {
        return nullptr;
    }
    const auto it = pipelines_.find(key);
    if (it != pipelines_.end()) {
        return it->second;
    }
    if (!hashes_match(key, vert_spv, frag_spv)) {
        LUMEN_LOG_ERROR(
            "GraphicsPipelineCache::get_or_create: SPIR-V 哈希与 key 不一致");
        return nullptr;
    }
    const vk::ShaderModule vert_mod = shader_modules.get_or_create(device, vert_spv);
    const vk::ShaderModule frag_mod = shader_modules.get_or_create(device, frag_spv);
    if (!vert_mod || !frag_mod) {
        return nullptr;
    }

    vk::PipelineShaderStageCreateInfo st_vert {};
    st_vert.stage = vk::ShaderStageFlagBits::eVertex;
    st_vert.module = vert_mod;
    st_vert.pName = "main";
    vk::PipelineShaderStageCreateInfo st_frag {};
    st_frag.stage = vk::ShaderStageFlagBits::eFragment;
    st_frag.module = frag_mod;
    st_frag.pName = "main";
    const vk::PipelineShaderStageCreateInfo stages[] = { st_vert, st_frag };

    vk::PipelineVertexInputStateCreateInfo vi {};
    vi.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(key.vertex_bindings.size());
    vi.pVertexBindingDescriptions = key.vertex_bindings.data();
    vi.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(key.vertex_attributes.size());
    vi.pVertexAttributeDescriptions = key.vertex_attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo ia {};
    ia.topology = key.topology;

    vk::PipelineViewportStateCreateInfo vp {};
    if (key.dynamic_viewport_scissor) {
        vp.viewportCount = 1;
        vp.scissorCount = 1;
    } else {
        vp.viewportCount = 0;
        vp.scissorCount = 0;
    }

    vk::PipelineRasterizationStateCreateInfo rs {};
    rs.depthClampEnable = vk::False;
    rs.rasterizerDiscardEnable = vk::False;
    rs.polygonMode = key.polygon_mode;
    rs.cullMode = key.cull_mode;
    rs.frontFace = key.front_face;
    rs.depthBiasEnable = vk::False;
    rs.lineWidth = key.line_width;

    vk::PipelineMultisampleStateCreateInfo ms {};
    ms.rasterizationSamples = key.rasterization_samples;
    ms.sampleShadingEnable = vk::False;

    vk::PipelineDepthStencilStateCreateInfo ds {};
    const vk::PipelineDepthStencilStateCreateInfo *ds_ptr = nullptr;
    if (key.depth_stencil.has_value()) {
        const DepthStencilPipelineKey &d = *key.depth_stencil;
        ds.depthTestEnable = d.depth_test ? vk::True : vk::False;
        ds.depthWriteEnable = d.depth_write ? vk::True : vk::False;
        ds.depthCompareOp = d.depth_compare;
        ds.depthBoundsTestEnable = d.depth_bounds_test ? vk::True : vk::False;
        ds.minDepthBounds = d.min_depth_bounds;
        ds.maxDepthBounds = d.max_depth_bounds;
        ds.stencilTestEnable = d.stencil_test ? vk::True : vk::False;
        ds_ptr = &ds;
    }

    std::vector<vk::PipelineColorBlendAttachmentState> cba;
    cba.reserve(key.color_blend_attachments.size());
    for (const ColorBlendAttachmentKey &c : key.color_blend_attachments) {
        vk::PipelineColorBlendAttachmentState s {};
        s.blendEnable = c.blend_enable ? vk::True : vk::False;
        s.srcColorBlendFactor = c.src_color;
        s.dstColorBlendFactor = c.dst_color;
        s.colorBlendOp = c.color_op;
        s.srcAlphaBlendFactor = c.src_alpha;
        s.dstAlphaBlendFactor = c.dst_alpha;
        s.alphaBlendOp = c.alpha_op;
        s.colorWriteMask = c.color_write_mask;
        cba.push_back(s);
    }

    vk::PipelineColorBlendStateCreateInfo cb {};
    cb.logicOpEnable = key.color_logic_op_enable ? vk::True : vk::False;
    cb.logicOp = key.logic_op;
    cb.attachmentCount = static_cast<std::uint32_t>(cba.size());
    cb.pAttachments = cba.data();

    vk::PipelineDynamicStateCreateInfo dyn {};
    dyn.dynamicStateCount =
        static_cast<std::uint32_t>(key.dynamic_states.size());
    dyn.pDynamicStates = key.dynamic_states.data();

    vk::GraphicsPipelineCreateInfo gpi {};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = ds_ptr;
    gpi.pColorBlendState = &cb;
    if (!key.dynamic_states.empty()) {
        gpi.pDynamicState = &dyn;
    }
    gpi.layout = layout_from_u64(key.pipeline_layout);
    gpi.renderPass = render_pass_from_u64(key.render_pass);
    gpi.subpass = key.subpass;

    vk::Pipeline pipe {};
    const vk::Result gpr = device.createGraphicsPipelines(
        pipeline_cache, 1, &gpi, nullptr, &pipe);
    if (gpr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("GraphicsPipelineCache: createGraphicsPipelines 失败 {}",
                        static_cast<int>(gpr));
        return nullptr;
    }
    pipelines_.emplace(key, pipe);
    return pipe;
}

void GraphicsPipelineCache::clear(const vk::Device device) {
    if (device) {
        for (const auto &e : pipelines_) {
            if (e.second) {
                device.destroyPipeline(e.second, nullptr);
            }
        }
    }
    pipelines_.clear();
}

void GraphicsPipelineCache::erase_pipeline(const vk::Device device,
                                           const vk::Pipeline pipeline) {
    if (!device || !pipeline) {
        return;
    }
    for (auto it = pipelines_.begin(); it != pipelines_.end();) {
        if (it->second == pipeline) {
            device.destroyPipeline(it->second, nullptr);
            it = pipelines_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace rhi
