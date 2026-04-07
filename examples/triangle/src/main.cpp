#include "core/log/logger.hpp"
#include "platform/window.hpp"
#include "rhi/buffer.hpp"
#include "rhi/context.hpp"
#include "rhi/descriptor_layout_cache.hpp"
#include "rhi/device.hpp"
#include "rhi/shader_reflection.hpp"
#include "rhi/swapchian.hpp"

#include <ghc/filesystem.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <vector>

namespace fs = ghc::filesystem;

namespace {

struct Vertex {
    float pos[2];
    float color[3];
};

constexpr Vertex k_triangle_vertices[] = {
    { { 0.0F, -0.5F }, { 1.0F, 0.2F, 0.2F } },
    { { 0.5F, 0.5F }, { 0.2F, 1.0F, 0.2F } },
    { { -0.5F, 0.5F }, { 0.2F, 0.4F, 1.0F } },
};

[[nodiscard]] std::vector<std::byte> read_spv(const fs::path &path) {
    if (!fs::exists(path)) {
        LUMEN_APP_LOG_ERROR("read_spv: 文件不存在 path={}",
                            path.generic_string());
        return {};
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        LUMEN_APP_LOG_ERROR("read_spv: 无法打开 path={}",
                            path.generic_string());
        return {};
    }
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> out(sz);
    if (sz > 0) {
        f.read(reinterpret_cast<char *>(out.data()),
               static_cast<std::streamsize>(sz));
    }
    if (!f) {
        LUMEN_APP_LOG_ERROR("read_spv: 读取失败 path={} size={}",
                            path.generic_string(), sz);
        return {};
    }
    LUMEN_APP_LOG_DEBUG("read_spv: 已读 {} 字节 path={}", sz,
                        path.generic_string());
    return out;
}

[[nodiscard]] fs::path shader_base_dir() {
    const char *base = SDL_GetBasePath();
    if (base != nullptr && base[0] != '\0') {
        LUMEN_APP_LOG_INFO("SDL_GetBasePath()={}", base);
        return fs::path(base);
    }
    LUMEN_APP_LOG_WARN("SDL_GetBasePath() 为空，使用 current_path={}",
                       fs::current_path().generic_string());
    return fs::current_path();
}

[[nodiscard]] vk::ShaderModule
create_shader_module(vk::Device dev, const std::vector<std::byte> &code) {
    if (code.empty()) {
        LUMEN_APP_LOG_ERROR("create_shader_module: SPIR-V 为空");
        return nullptr;
    }
    if ((code.size() % 4) != 0) {
        LUMEN_APP_LOG_ERROR("create_shader_module: 大小非 4 对齐 size={}",
                            code.size());
        return nullptr;
    }
    vk::ShaderModuleCreateInfo ci {};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const std::uint32_t *>(code.data());
    vk::ShaderModule mod {};
    const vk::Result r = dev.createShaderModule(&ci, nullptr, &mod);
    if (r != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("createShaderModule 失败 vk::Result={}",
                            static_cast<int>(r));
        return nullptr;
    }
    return mod;
}

struct DrawResources {
    vk::RenderPass render_pass {};
    vk::DescriptorSetLayout descriptor_layout {};
    vk::PipelineLayout pipeline_layout {};
    vk::Pipeline pipeline {};
    vk::DescriptorPool descriptor_pool {};
    std::array<vk::DescriptorSet, rhi::Device::k_frames_in_flight>
        descriptor_sets {};
    std::vector<vk::Framebuffer> framebuffers;
};

void destroy_draw(vk::Device dev, DrawResources &g) {
    for (vk::Framebuffer fb : g.framebuffers) {
        if (fb) {
            dev.destroyFramebuffer(fb, nullptr);
        }
    }
    g.framebuffers.clear();
    if (g.pipeline) {
        dev.destroyPipeline(g.pipeline, nullptr);
        g.pipeline = nullptr;
    }
    // `descriptor_layout` / `pipeline_layout` 由全局 LayoutCache
    // 持有，在进程退出前 `DescriptorSetLayoutCache::clear` /
    // `PipelineLayoutCache::clear` 中销毁。
    if (g.descriptor_pool) {
        dev.destroyDescriptorPool(g.descriptor_pool, nullptr);
        g.descriptor_pool = nullptr;
    }
    g.descriptor_sets.fill(nullptr);
    g.descriptor_layout = nullptr;
    g.pipeline_layout = nullptr;
    if (g.render_pass) {
        dev.destroyRenderPass(g.render_pass, nullptr);
        g.render_pass = nullptr;
    }
}

[[nodiscard]] bool create_framebuffers(vk::Device dev,
                                       const rhi::Swapchain &swap,
                                       vk::RenderPass rp, DrawResources &g) {
    for (vk::Framebuffer fb : g.framebuffers) {
        if (fb) {
            dev.destroyFramebuffer(fb, nullptr);
        }
    }
    g.framebuffers.clear();
    const vk::Extent2D ext = swap.extent();
    const std::uint32_t n = swap.image_count();
    g.framebuffers.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        vk::ImageView iv = swap.image_view(i);
        vk::FramebufferCreateInfo fci {};
        fci.renderPass = rp;
        fci.attachmentCount = 1;
        fci.pAttachments = &iv;
        fci.width = ext.width;
        fci.height = ext.height;
        fci.layers = 1;
        const vk::Result fr =
            dev.createFramebuffer(&fci, nullptr, &g.framebuffers[i]);
        if (fr != vk::Result::eSuccess) {
            LUMEN_APP_LOG_ERROR(
                "createFramebuffer 失败 i={} vk::Result={} extent={}x{}", i,
                static_cast<int>(fr), ext.width, ext.height);
            return false;
        }
    }
    LUMEN_APP_LOG_INFO("framebuffer: 已创建 {} 个 {}x{}", n, ext.width,
                       ext.height);
    return true;
}

[[nodiscard]] bool setup_triangle_descriptors(
    vk::Device dev, DrawResources &g,
    const std::array<rhi::BufferHandle, rhi::Device::k_frames_in_flight> &ubos,
    rhi::Device &rdev, const vk::DeviceSize ubo_range) {
    constexpr std::uint32_t n = rhi::Device::k_frames_in_flight;
    vk::DescriptorPoolSize ps {};
    ps.type = vk::DescriptorType::eUniformBuffer;
    ps.descriptorCount = n;

    vk::DescriptorPoolCreateInfo dpci {};
    dpci.maxSets = n;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    const vk::Result rdp =
        dev.createDescriptorPool(&dpci, nullptr, &g.descriptor_pool);
    if (rdp != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("createDescriptorPool 失败 vk::Result={}",
                            static_cast<int>(rdp));
        return false;
    }

    std::array<vk::DescriptorSetLayout, n> dsls {};
    dsls.fill(g.descriptor_layout);

    vk::DescriptorSetAllocateInfo dsai {};
    dsai.descriptorPool = g.descriptor_pool;
    dsai.descriptorSetCount = n;
    dsai.pSetLayouts = dsls.data();
    const vk::Result ras =
        dev.allocateDescriptorSets(&dsai, g.descriptor_sets.data());
    if (ras != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("allocateDescriptorSets 失败 vk::Result={}",
                            static_cast<int>(ras));
        dev.destroyDescriptorPool(g.descriptor_pool, nullptr);
        g.descriptor_pool = nullptr;
        g.descriptor_sets.fill(nullptr);
        return false;
    }

    for (std::uint32_t i = 0; i < n; ++i) {
        const rhi::BufferResource *br = rdev.try_get(ubos[i]);
        if (br == nullptr) {
            LUMEN_APP_LOG_ERROR("setup_triangle_descriptors: try_get(ubos[{}]) "
                                "失败",
                                i);
            dev.destroyDescriptorPool(g.descriptor_pool, nullptr);
            g.descriptor_pool = nullptr;
            g.descriptor_sets.fill(nullptr);
            return false;
        }
        vk::DescriptorBufferInfo bi {};
        bi.buffer = br->buffer;
        bi.offset = 0;
        bi.range = ubo_range;

        vk::WriteDescriptorSet w {};
        w.dstSet = g.descriptor_sets[i];
        w.dstBinding = 0;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo = &bi;
        dev.updateDescriptorSets(1, &w, 0, nullptr);
    }
    return true;
}

[[nodiscard]] bool
create_draw_pipeline(vk::Device dev, const rhi::Swapchain &swap,
                     const fs::path &vert_spv, const fs::path &frag_spv,
                     DrawResources &g, rhi::DescriptorSetLayoutCache &dsl_cache,
                     rhi::PipelineLayoutCache &pl_cache) {
    destroy_draw(dev, g);

    const std::vector<std::byte> vert_code = read_spv(vert_spv);
    const std::vector<std::byte> frag_code = read_spv(frag_spv);
    if (vert_code.empty() || frag_code.empty() || (vert_code.size() % 4) != 0 ||
        (frag_code.size() % 4) != 0) {
        LUMEN_APP_LOG_ERROR("create_draw_pipeline: SPIR-V 无效 vert={} frag={}",
                            vert_spv.generic_string(),
                            frag_spv.generic_string());
        return false;
    }

    const std::span<const std::uint32_t> vert_words {
        reinterpret_cast<const std::uint32_t *>(vert_code.data()),
        vert_code.size() / 4
    };
    const std::span<const std::uint32_t> frag_words {
        reinterpret_cast<const std::uint32_t *>(frag_code.data()),
        frag_code.size() / 4
    };

    const std::optional<rhi::ShaderReflection> refl_v =
        rhi::reflect_spirv(vert_words, vk::ShaderStageFlagBits::eVertex);
    const std::optional<rhi::ShaderReflection> refl_f =
        rhi::reflect_spirv(frag_words, vk::ShaderStageFlagBits::eFragment);
    if (!refl_v || !refl_f) {
        LUMEN_APP_LOG_ERROR("create_draw_pipeline: 着色器反射失败");
        return false;
    }
    rhi::ShaderReflection merged {};
    if (!rhi::merge_vert_frag_reflection(*refl_v, *refl_f, merged)) {
        LUMEN_APP_LOG_ERROR("create_draw_pipeline: 合并 VS/FS 反射失败");
        return false;
    }

    std::vector<vk::DescriptorSetLayout> set_layouts {};
    if (!rhi::create_reflected_pipeline_layouts(
            dev, merged, dsl_cache, pl_cache, set_layouts, g.pipeline_layout)) {
        LUMEN_APP_LOG_ERROR(
            "create_draw_pipeline: 从反射创建 PipelineLayout 失败");
        return false;
    }
    if (set_layouts.size() != 1) {
        LUMEN_APP_LOG_ERROR(
            "create_draw_pipeline: triangle 期望单 set，实际 set 数={}",
            set_layouts.size());
        return false;
    }
    g.descriptor_layout = set_layouts[0];

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

    const vk::Result rpr = dev.createRenderPass(&rpci, nullptr, &g.render_pass);
    if (rpr != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("createRenderPass 失败 vk::Result={}",
                            static_cast<int>(rpr));
        return false;
    }
    LUMEN_APP_LOG_DEBUG("createRenderPass 成功 format={}",
                        static_cast<int>(color_fmt));

    vk::ShaderModule vert_mod = create_shader_module(dev, vert_code);
    vk::ShaderModule frag_mod = create_shader_module(dev, frag_code);
    if (!vert_mod || !frag_mod) {
        if (vert_mod) {
            dev.destroyShaderModule(vert_mod, nullptr);
        }
        if (frag_mod) {
            dev.destroyShaderModule(frag_mod, nullptr);
        }
        destroy_draw(dev, g);
        LUMEN_APP_LOG_ERROR(
            "着色器模块创建失败 vert_bytes={} frag_bytes={} vert={} frag={}",
            vert_code.size(), frag_code.size(), vert_spv.generic_string(),
            frag_spv.generic_string());
        return false;
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

    vk::VertexInputBindingDescription vib {};
    vib.binding = 0;
    vib.stride = sizeof(Vertex);
    vib.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription via[2] {};
    via[0].location = 0;
    via[0].binding = 0;
    via[0].format = vk::Format::eR32G32Sfloat;
    via[0].offset = offsetof(Vertex, pos);
    via[1].location = 1;
    via[1].binding = 0;
    via[1].format = vk::Format::eR32G32B32Sfloat;
    via[1].offset = offsetof(Vertex, color);

    vk::PipelineVertexInputStateCreateInfo vi {};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vib;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = via;

    vk::PipelineInputAssemblyStateCreateInfo ia {};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vp {};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rs {};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0F;

    vk::PipelineMultisampleStateCreateInfo ms {};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState cba {};
    cba.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo cb {};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    const vk::DynamicState dyn_states[] = { vk::DynamicState::eViewport,
                                            vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dyn {};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    vk::GraphicsPipelineCreateInfo gpi {};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dyn;
    gpi.layout = g.pipeline_layout;
    gpi.renderPass = g.render_pass;
    gpi.subpass = 0;

    const vk::Result gpr =
        dev.createGraphicsPipelines(nullptr, 1, &gpi, nullptr, &g.pipeline);
    if (gpr != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("createGraphicsPipelines 失败 vk::Result={}",
                            static_cast<int>(gpr));
        dev.destroyShaderModule(vert_mod, nullptr);
        dev.destroyShaderModule(frag_mod, nullptr);
        destroy_draw(dev, g);
        return false;
    }
    LUMEN_APP_LOG_INFO("graphics pipeline 创建成功");

    dev.destroyShaderModule(vert_mod, nullptr);
    dev.destroyShaderModule(frag_mod, nullptr);

    if (!create_framebuffers(dev, swap, g.render_pass, g)) {
        LUMEN_APP_LOG_ERROR("create_framebuffers 失败");
        destroy_draw(dev, g);
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!core::log::Logger::init()) {
        std::cerr
            << "[triangle] Logger::init 失败，请检查工作目录是否可写 logs/\n";
        return 1;
    }
    struct LogShutdown {
        ~LogShutdown() { core::log::Logger::shutdown(); }
    } log_shutdown;

    LUMEN_APP_LOG_INFO("triangle 示例启动");

    lumen::platform::Window window {};
    lumen::platform::WindowConfig wcfg {};
    wcfg.title = "triangle (RHI)";
    wcfg.width = 960;
    wcfg.height = 540;
    if (!window.create(wcfg)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return 1;
    }
    LUMEN_APP_LOG_INFO("窗口已创建 逻辑尺寸 {}x{}", wcfg.width, wcfg.height);

    rhi::Context ctx {};
    rhi::ContextDesc cdesc {};
    cdesc.windowHandle = &window;
    cdesc.instanceExtensions = window.get_vulkan_instance_extensions();
    LUMEN_APP_LOG_INFO("传给 rhi::Context 的实例扩展共 {} 个",
                       cdesc.instanceExtensions.size());
    for (const char *name : cdesc.instanceExtensions) {
        LUMEN_APP_LOG_INFO("  instanceExt: {}",
                           name != nullptr ? name : "(null)");
    }
    if (!ctx.init(cdesc)) {
        LUMEN_APP_LOG_ERROR("rhi::Context 初始化失败");
        return 1;
    }
    LUMEN_APP_LOG_INFO("rhi::Context 就绪 surface 有效={}",
                       static_cast<bool>(ctx.surface()));

    rhi::Device device {};
    if (!device.init(ctx)) {
        LUMEN_APP_LOG_ERROR("rhi::Device 初始化失败");
        ctx.shutdown();
        return 1;
    }
    LUMEN_APP_LOG_INFO("rhi::Device 就绪 frames_in_flight={}",
                       rhi::Device::k_frames_in_flight);

    int fbw = 0;
    int fbh = 0;
    window.get_framebuffer_size(&fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) {
        LUMEN_APP_LOG_WARN("get_framebuffer_size 无效 {}x{}，回退逻辑尺寸", fbw,
                           fbh);
        fbw = static_cast<int>(wcfg.width);
        fbh = static_cast<int>(wcfg.height);
    } else {
        LUMEN_APP_LOG_INFO("framebuffer 像素尺寸 {}x{}", fbw, fbh);
    }

    rhi::Swapchain swap {};
    if (!swap.init(&device, ctx.surface(), static_cast<std::uint32_t>(fbw),
                   static_cast<std::uint32_t>(fbh))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败（extent {}x{}）", fbw, fbh);
        device.shutdown();
        ctx.shutdown();
        return 1;
    }
    {
        const vk::Extent2D e = swap.extent();
        LUMEN_APP_LOG_INFO(
            "Swapchain 就绪 extent={}x{} format={} image_count={}", e.width,
            e.height, static_cast<int>(swap.format()), swap.image_count());
    }

    const fs::path shader_dir = shader_base_dir() / "shaders";
    LUMEN_APP_LOG_INFO("着色器目录 shader_dir={}", shader_dir.generic_string());
    rhi::DescriptorSetLayoutCache triangle_dsl_cache {};
    rhi::PipelineLayoutCache triangle_pl_cache {};
    DrawResources draw {};
    if (!create_draw_pipeline(device.vk_device(), swap,
                              shader_dir / "triangle.vert.spv",
                              shader_dir / "triangle.frag.spv", draw,
                              triangle_dsl_cache, triangle_pl_cache)) {
        LUMEN_APP_LOG_ERROR(
            "create_draw_pipeline 失败（见上文 SPIR-V / Vulkan 日志）");
        triangle_pl_cache.clear(device.vk_device());
        triangle_dsl_cache.clear(device.vk_device());
        swap.destroy();
        device.shutdown();
        ctx.shutdown();
        return 1;
    }

    vk::Device vkdev = device.vk_device();
    std::array<vk::Semaphore, rhi::Device::k_frames_in_flight>
        image_available {};
    std::array<vk::Semaphore, rhi::Device::k_frames_in_flight>
        render_finished {};
    vk::SemaphoreCreateInfo sci {};
    for (std::uint32_t i = 0; i < rhi::Device::k_frames_in_flight; ++i) {
        const vk::Result ar =
            vkdev.createSemaphore(&sci, nullptr, &image_available[i]);
        const vk::Result dr =
            vkdev.createSemaphore(&sci, nullptr, &render_finished[i]);
        if (ar != vk::Result::eSuccess || dr != vk::Result::eSuccess) {
            LUMEN_APP_LOG_ERROR("createSemaphore 失败 slot={} "
                                "image_available={} render_finished={}",
                                i, static_cast<int>(ar), static_cast<int>(dr));
            for (std::uint32_t j = 0; j < i; ++j) {
                vkdev.destroySemaphore(image_available[j], nullptr);
                vkdev.destroySemaphore(render_finished[j], nullptr);
            }
            destroy_draw(vkdev, draw);
            swap.destroy();
            triangle_pl_cache.clear(vkdev);
            triangle_dsl_cache.clear(vkdev);
            device.shutdown();
            ctx.shutdown();
            return 1;
        }
    }
    LUMEN_APP_LOG_INFO("同步对象（每帧 semaphore 对）创建完成，进入主循环");

    rhi::BufferDesc vb_desc {};
    vb_desc.size = sizeof(k_triangle_vertices);
    vb_desc.usage = rhi::BufferUsage::Vertex;
    vb_desc.memory = rhi::MemoryUsage::GPU_ONLY;
    vb_desc.data = k_triangle_vertices;
    const rhi::BufferHandle vertex_buffer = device.create_buffer(vb_desc);
    if (!rhi::is_valid(vertex_buffer)) {
        LUMEN_APP_LOG_ERROR("顶点缓冲创建失败");
        for (std::uint32_t i = 0; i < rhi::Device::k_frames_in_flight; ++i) {
            vkdev.destroySemaphore(image_available[i], nullptr);
            vkdev.destroySemaphore(render_finished[i], nullptr);
        }
        destroy_draw(vkdev, draw);
        swap.destroy();
        triangle_pl_cache.clear(vkdev);
        triangle_dsl_cache.clear(vkdev);
        device.shutdown();
        ctx.shutdown();
        return 1;
    }

    const vk::DeviceSize ubo_align_raw = static_cast<vk::DeviceSize>(
        device.physical_device()
            .getProperties()
            .limits.minUniformBufferOffsetAlignment);
    const vk::DeviceSize ubo_align =
        ubo_align_raw > 0 ? ubo_align_raw : static_cast<vk::DeviceSize>(256);
    const vk::DeviceSize ubo_range =
        std::max<vk::DeviceSize>(ubo_align, sizeof(float));

    std::array<rhi::BufferHandle, rhi::Device::k_frames_in_flight> ubo_bufs {};
    bool ubo_ok = true;
    for (std::uint32_t ui = 0; ui < rhi::Device::k_frames_in_flight; ++ui) {
        rhi::BufferDesc ubd {};
        ubd.size = static_cast<std::size_t>(ubo_range);
        ubd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDst;
        ubd.memory = rhi::MemoryUsage::GPU_ONLY;
        ubd.data = nullptr;
        ubo_bufs[ui] = device.create_buffer(ubd);
        if (!rhi::is_valid(ubo_bufs[ui])) {
            LUMEN_APP_LOG_ERROR("UBO 缓冲创建失败 slot={}", ui);
            ubo_ok = false;
            break;
        }
    }
    if (!ubo_ok) {
        for (rhi::BufferHandle h : ubo_bufs) {
            if (rhi::is_valid(h)) {
                device.destroy_buffer(h);
            }
        }
        for (std::uint32_t i = 0; i < rhi::Device::k_frames_in_flight; ++i) {
            vkdev.destroySemaphore(image_available[i], nullptr);
            vkdev.destroySemaphore(render_finished[i], nullptr);
        }
        device.destroy_buffer(vertex_buffer);
        destroy_draw(vkdev, draw);
        swap.destroy();
        triangle_pl_cache.clear(vkdev);
        triangle_dsl_cache.clear(vkdev);
        device.shutdown();
        ctx.shutdown();
        return 1;
    }

    if (!setup_triangle_descriptors(vkdev, draw, ubo_bufs, device, ubo_range)) {
        for (rhi::BufferHandle h : ubo_bufs) {
            device.destroy_buffer(h);
        }
        for (std::uint32_t i = 0; i < rhi::Device::k_frames_in_flight; ++i) {
            vkdev.destroySemaphore(image_available[i], nullptr);
            vkdev.destroySemaphore(render_finished[i], nullptr);
        }
        device.destroy_buffer(vertex_buffer);
        destroy_draw(vkdev, draw);
        swap.destroy();
        triangle_pl_cache.clear(vkdev);
        triangle_dsl_cache.clear(vkdev);
        device.shutdown();
        ctx.shutdown();
        return 1;
    }

    bool resize_pending = false;
    std::uint64_t frame_i = 0;
    const auto app_start = std::chrono::steady_clock::now();
    while (window.poll_events()) {
        if (resize_pending) {
            LUMEN_APP_LOG_DEBUG("处理 resize_pending，wait_idle 后重建交换链");
            ctx.wait_idle();
            window.get_framebuffer_size(&fbw, &fbh);
            if (fbw > 0 && fbh > 0) {
                if (swap.recreate(static_cast<std::uint32_t>(fbw),
                                  static_cast<std::uint32_t>(fbh)) &&
                    create_framebuffers(vkdev, swap, draw.render_pass, draw)) {
                    resize_pending = false;
                    LUMEN_APP_LOG_INFO("交换链重建成功 {}x{}", fbw, fbh);
                } else {
                    LUMEN_APP_LOG_ERROR("交换链或 framebuffer 重建失败 {}x{}",
                                        fbw, fbh);
                }
            } else {
                LUMEN_APP_LOG_WARN("重建跳过：framebuffer 尺寸仍无效 {}x{}",
                                   fbw, fbh);
            }
            if (resize_pending) {
                continue;
            }
        }

        device.begin_frame();
        const std::uint32_t slot = device.frame_slot();
        const vk::Semaphore img_sem = image_available[slot];
        const vk::Semaphore done_sem = render_finished[slot];

        rhi::SwapchainAcquireResult acq = swap.acquire_next_image(img_sem);
        if (frame_i < 3) {
            LUMEN_APP_LOG_DEBUG("帧 {} slot={} acquire result={} imageIndex={}",
                                frame_i, slot, static_cast<int>(acq.result),
                                acq.image_index);
        }
        if (rhi::swapchain_acquire_needs_recreate(acq.result)) {
            LUMEN_APP_LOG_WARN("acquire OUT_OF_DATE，标记 resize");
            device.end_frame();
            resize_pending = true;
            ++frame_i;
            continue;
        }
        if (acq.result != vk::Result::eSuccess &&
            acq.result != vk::Result::eSuboptimalKHR) {
            LUMEN_APP_LOG_ERROR("acquireNextImageKHR 失败 {}",
                                static_cast<int>(acq.result));
            device.end_frame();
            break;
        }
        if (acq.result == vk::Result::eSuboptimalKHR) {
            LUMEN_APP_LOG_DEBUG("acquire SUBOPTIMAL，本帧继续，稍后重建");
            resize_pending = true;
        }

        const vk::Extent2D ext = swap.extent();
        vk::CommandBuffer cmd = device.frame_command_buffer();

        const float time_sec = std::chrono::duration<float>(
                                   std::chrono::steady_clock::now() - app_start)
                                   .count();
        device.upload_buffer(ubo_bufs[slot], &time_sec, sizeof(time_sec), 0);

        const rhi::BufferResource *ubo_res = device.try_get(ubo_bufs[slot]);
        if (ubo_res == nullptr) {
            LUMEN_APP_LOG_ERROR("try_get(ubo_bufs[slot]) 失败");
            device.end_frame();
            break;
        }
        vk::BufferMemoryBarrier ubo_barrier {};
        ubo_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        ubo_barrier.dstAccessMask = vk::AccessFlagBits::eUniformRead;
        ubo_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ubo_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ubo_barrier.buffer = ubo_res->buffer;
        ubo_barrier.offset = 0;
        ubo_barrier.size = VK_WHOLE_SIZE;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eVertexShader, {},
                            nullptr, ubo_barrier, nullptr);

        vk::ClearValue clear {};
        clear.color = vk::ClearColorValue(
            std::array<float, 4> { 0.05F, 0.06F, 0.09F, 1.0F });

        vk::RenderPassBeginInfo rpbi {};
        rpbi.renderPass = draw.render_pass;
        rpbi.framebuffer = draw.framebuffers[acq.image_index];
        rpbi.renderArea.offset = vk::Offset2D { 0, 0 };
        rpbi.renderArea.extent = ext;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;

        cmd.beginRenderPass(rpbi, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, draw.pipeline);

        const std::array<vk::DescriptorSet, 1> draw_dss {
            draw.descriptor_sets[slot]
        };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               draw.pipeline_layout, 0, draw_dss, {});

        vk::Viewport vp {};
        vp.x = 0.0F;
        vp.y = 0.0F;
        vp.width = static_cast<float>(ext.width);
        vp.height = static_cast<float>(ext.height);
        vp.minDepth = 0.0F;
        vp.maxDepth = 1.0F;
        cmd.setViewport(0, 1, &vp);

        vk::Rect2D sc {};
        sc.offset = vk::Offset2D { 0, 0 };
        sc.extent = ext;
        cmd.setScissor(0, 1, &sc);

        const rhi::BufferResource *vb_res = device.try_get(vertex_buffer);
        if (vb_res == nullptr) {
            LUMEN_APP_LOG_ERROR("try_get(vertex_buffer) 失败");
            cmd.endRenderPass();
            device.end_frame();
            break;
        }
        const vk::Buffer vb_buf = vb_res->buffer;
        const vk::DeviceSize vb_off = 0;
        cmd.bindVertexBuffers(0, 1, &vb_buf, &vb_off);

        cmd.draw(static_cast<std::uint32_t>(std::size(k_triangle_vertices)), 1,
                 0, 0);
        cmd.endRenderPass();

        device.end_frame(img_sem,
                         vk::PipelineStageFlagBits::eColorAttachmentOutput,
                         done_sem);

        const std::array<vk::Semaphore, 1> present_wait { done_sem };
        const vk::Result pr = swap.present(acq.image_index, present_wait);
        if (frame_i < 3) {
            LUMEN_APP_LOG_DEBUG("帧 {} present result={}", frame_i,
                                static_cast<int>(pr));
        }
        if (rhi::swapchain_present_needs_recreate(pr)) {
            LUMEN_APP_LOG_WARN("present OUT_OF_DATE，标记 resize");
            resize_pending = true;
        } else if (pr == vk::Result::eSuboptimalKHR) {
            LUMEN_APP_LOG_DEBUG("present SUBOPTIMAL，标记 resize");
            resize_pending = true;
        } else if (pr != vk::Result::eSuccess) {
            LUMEN_APP_LOG_ERROR("presentKHR 失败 {}", static_cast<int>(pr));
            break;
        }
        ++frame_i;
    }

    LUMEN_APP_LOG_INFO("主循环结束（窗口关闭或错误），waitIdle 并清理");
    static_cast<void>(vkdev.waitIdle());
    for (std::uint32_t i = 0; i < rhi::Device::k_frames_in_flight; ++i) {
        vkdev.destroySemaphore(image_available[i], nullptr);
        vkdev.destroySemaphore(render_finished[i], nullptr);
    }
    destroy_draw(vkdev, draw);
    swap.destroy();
    triangle_pl_cache.clear(vkdev);
    triangle_dsl_cache.clear(vkdev);
    for (rhi::BufferHandle h : ubo_bufs) {
        device.destroy_buffer(h);
    }
    device.destroy_buffer(vertex_buffer);
    device.shutdown();
    ctx.shutdown();
    return 0;
}
