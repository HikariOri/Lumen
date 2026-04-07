#include "core/log/logger.hpp"
#include "platform/window.hpp"
#include "rhi/buffer.hpp"
#include "rhi/context.hpp"
#include "rhi/descriptor_layout_cache.hpp"
#include "rhi/device.hpp"
#include "rhi/render_graph.hpp"
#include "rhi/render_graph_gpu.hpp"
#include "rhi/swapchian.hpp"

#include <ghc/filesystem.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = ghc::filesystem;

namespace {

struct Vertex {
    float pos[2];
    float color[3];
};

/// 四边形四角（NDC），由索引缓冲拼成两个三角形。
constexpr Vertex k_rect_vertices[] = {
    { { -0.55F, -0.55F }, { 1.0F, 0.25F, 0.25F } },
    { { 0.55F, -0.55F }, { 0.25F, 1.0F, 0.25F } },
    { { 0.55F, 0.55F }, { 0.25F, 0.25F, 1.0F } },
    { { -0.55F, 0.55F }, { 1.0F, 1.0F, 0.35F } },
};
constexpr std::uint16_t k_rect_indices[] = { 0, 1, 2, 0, 2, 3 };
constexpr std::uint32_t k_rect_index_count =
    static_cast<std::uint32_t>(std::size(k_rect_indices));

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

struct RectGpuBundle {
    rhi::BufferHandle vertex_buffer {};
    rhi::BufferHandle index_buffer {};
    rhi::BufferHandle ubo_buffer {};
    vk::DeviceSize ubo_dynamic_stride { 0 };
};

[[nodiscard]] bool create_rect_gpu_buffers(rhi::Device &device,
                                           RectGpuBundle &out) {
    rhi::BufferDesc vb_desc {};
    vb_desc.size = sizeof(k_rect_vertices);
    vb_desc.usage = rhi::BufferUsage::Vertex;
    vb_desc.memory = rhi::MemoryUsage::GPU_ONLY;
    vb_desc.data = k_rect_vertices;
    out.vertex_buffer = device.create_buffer(vb_desc);
    if (!rhi::is_valid(out.vertex_buffer)) {
        LUMEN_APP_LOG_ERROR("create_rect_gpu_buffers: 顶点缓冲失败");
        return false;
    }

    rhi::BufferDesc ib_desc {};
    ib_desc.size = sizeof(k_rect_indices);
    ib_desc.usage = rhi::BufferUsage::Index;
    ib_desc.memory = rhi::MemoryUsage::GPU_ONLY;
    ib_desc.data = k_rect_indices;
    out.index_buffer = device.create_buffer(ib_desc);
    if (!rhi::is_valid(out.index_buffer)) {
        LUMEN_APP_LOG_ERROR("create_rect_gpu_buffers: 索引缓冲失败");
        device.destroy_buffer(out.vertex_buffer);
        out.vertex_buffer = {};
        return false;
    }

    const vk::DeviceSize ubo_align_raw = static_cast<vk::DeviceSize>(
        device.physical_device()
            .getProperties()
            .limits.minUniformBufferOffsetAlignment);
    const vk::DeviceSize ubo_align =
        ubo_align_raw > 0 ? ubo_align_raw : static_cast<vk::DeviceSize>(256);
    out.ubo_dynamic_stride =
        std::max<vk::DeviceSize>(ubo_align, sizeof(float));
    const vk::DeviceSize ubo_pool_bytes =
        out.ubo_dynamic_stride *
        static_cast<vk::DeviceSize>(rhi::Device::k_frames_in_flight);

    rhi::BufferDesc ubd {};
    ubd.size = static_cast<std::size_t>(ubo_pool_bytes);
    ubd.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDst;
    ubd.memory = rhi::MemoryUsage::GPU_ONLY;
    ubd.data = nullptr;
    out.ubo_buffer = device.create_buffer(ubd);
    if (!rhi::is_valid(out.ubo_buffer)) {
        LUMEN_APP_LOG_ERROR(
            "create_rect_gpu_buffers: UBO 池失败（{} 字节）",
            static_cast<std::uint64_t>(ubo_pool_bytes));
        device.destroy_buffer(out.index_buffer);
        device.destroy_buffer(out.vertex_buffer);
        out.index_buffer = {};
        out.vertex_buffer = {};
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!core::log::Logger::init()) {
        std::cerr
            << "[rectangle] Logger::init 失败，请检查工作目录是否可写 logs/\n";
        return 1;
    }
    struct LogShutdown {
        ~LogShutdown() { core::log::Logger::shutdown(); }
    } log_shutdown;

    LUMEN_APP_LOG_INFO("rectangle（索引矩形）示例启动");

    lumen::platform::Window window {};
    lumen::platform::WindowConfig wcfg {};
    wcfg.title = "rectangle indexed (RHI)";
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
    cdesc.pipeline_cache_file_path =
        (shader_base_dir() / "lumen_rectangle_vk_pipeline_cache.bin")
            .string();
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
    rhi::DescriptorSetLayoutCache rect_dsl_cache {};
    rhi::PipelineLayoutCache rect_pl_cache {};
    rhi::RgGpuCompileContext rect_gpu_ctx {};
    rect_gpu_ctx.rhi_device = &device;
    rect_gpu_ctx.vk_device = device.vk_device();
    rect_gpu_ctx.swapchain = &swap;
    rect_gpu_ctx.dsl_cache = &rect_dsl_cache;
    rect_gpu_ctx.pl_cache = &rect_pl_cache;

    const std::shared_ptr<const std::vector<std::byte>> rect_vert_spv =
        std::make_shared<std::vector<std::byte>>(
            read_spv(shader_dir / "triangle.vert.spv"));
    const std::shared_ptr<const std::vector<std::byte>> rect_frag_spv =
        std::make_shared<std::vector<std::byte>>(
            read_spv(shader_dir / "triangle.frag.spv"));
    if (rect_vert_spv->empty() || rect_frag_spv->empty() ||
        (rect_vert_spv->size() % 4) != 0 || (rect_frag_spv->size() % 4) != 0) {
        LUMEN_APP_LOG_ERROR("着色器 SPIR-V 无效或缺失（检查 {}）",
                            shader_dir.generic_string());
        rect_pl_cache.clear(device.vk_device());
        rect_dsl_cache.clear(device.vk_device());
        swap.destroy();
        device.shutdown();
        ctx.shutdown();
        return 1;
    }

    RectGpuBundle rect_gpu {};
    if (!create_rect_gpu_buffers(device, rect_gpu)) {
        LUMEN_APP_LOG_ERROR("create_rect_gpu_buffers 失败");
        rect_pl_cache.clear(device.vk_device());
        rect_dsl_cache.clear(device.vk_device());
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
            device.destroy_buffer(rect_gpu.ubo_buffer);
            device.destroy_buffer(rect_gpu.index_buffer);
            device.destroy_buffer(rect_gpu.vertex_buffer);
            rect_gpu_ctx.destroy_all(vkdev, &device.graphics_pipeline_cache());
            swap.destroy();
            rect_pl_cache.clear(vkdev);
            rect_dsl_cache.clear(vkdev);
            device.shutdown();
            ctx.shutdown();
            return 1;
        }
    }
    LUMEN_APP_LOG_INFO("同步对象（每帧 semaphore 对）创建完成，进入主循环");

    bool resize_pending = false;
    std::uint64_t frame_i = 0;
    const auto app_start = std::chrono::steady_clock::now();
    while (window.poll_events()) {
        if (resize_pending) {
            LUMEN_APP_LOG_DEBUG("处理 resize_pending，wait_idle 后重建交换链");
            ctx.wait_idle();
            window.get_framebuffer_size(&fbw, &fbh);
            if (fbw > 0 && fbh > 0) {
                bool resized = false;
                if (!rect_gpu_ctx.compiled.empty() &&
                    rect_gpu_ctx.compiled[0].has_value()) {
                    resized = rhi::recreate_swapchain_and_present_framebuffers(
                        swap, static_cast<std::uint32_t>(fbw),
                        static_cast<std::uint32_t>(fbh), vkdev,
                        rect_gpu_ctx.compiled[0]->render_pass,
                        rect_gpu_ctx.compiled[0]->framebuffers);
                }
                if (resized) {
                    resize_pending = false;
                    LUMEN_APP_LOG_INFO("交换链与呈现 framebuffer 重建成功 {}x{}",
                                       fbw, fbh);
                } else {
                    LUMEN_APP_LOG_ERROR(
                        "交换链或呈现 framebuffer 重建失败 {}x{}", fbw, fbh);
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
        vk::CommandBuffer vk_cmd = device.frame_command_buffer();

        const float time_sec = std::chrono::duration<float>(
                                   std::chrono::steady_clock::now() - app_start)
                                   .count();
        const vk::DeviceSize ubo_slot_offset =
            static_cast<vk::DeviceSize>(slot) * rect_gpu.ubo_dynamic_stride;
        device.upload_buffer(rect_gpu.ubo_buffer, &time_sec, sizeof(time_sec),
                             ubo_slot_offset);

        const rhi::BufferResource *ubo_res = device.try_get(rect_gpu.ubo_buffer);
        if (ubo_res == nullptr) {
            LUMEN_APP_LOG_ERROR("try_get(ubo_buffer) 失败");
            device.end_frame();
            break;
        }
        const rhi::BufferResource *vb_res = device.try_get(rect_gpu.vertex_buffer);
        if (vb_res == nullptr) {
            LUMEN_APP_LOG_ERROR("try_get(vertex_buffer) 失败");
            device.end_frame();
            break;
        }
        const rhi::BufferResource *ib_res = device.try_get(rect_gpu.index_buffer);
        if (ib_res == nullptr) {
            LUMEN_APP_LOG_ERROR("try_get(index_buffer) 失败");
            device.end_frame();
            break;
        }

        const std::uint32_t swap_image_index = acq.image_index;
        rhi::RenderGraph frame_rg {};
        const rhi::RgResourceId rg_ubo = frame_rg.create_buffer();
        const rhi::RgResourceId rg_vb = frame_rg.create_buffer();
        const rhi::RgResourceId rg_ib = frame_rg.create_buffer();
        frame_rg.bind_buffer(rg_ubo, ubo_res->buffer, 0, rect_gpu.ubo_buffer);
        frame_rg.bind_buffer(rg_vb, vb_res->buffer, 0, rect_gpu.vertex_buffer);
        frame_rg.bind_buffer(rg_ib, ib_res->buffer, 0, rect_gpu.index_buffer);
        frame_rg.declare_buffer_prior_write(
            rg_ubo, vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite);

        frame_rg.add_pass("rectangle_draw")
            .gpu_shaders(rect_vert_spv, rect_frag_spv)
            .gpu_bind_uniform_buffer("ubo", rg_ubo, rect_gpu.ubo_dynamic_stride)
            .gpu_draw_indexed(rg_vb, rg_ib, k_rect_index_count);

        if (!frame_rg.compile(&rect_gpu_ctx)) {
            LUMEN_APP_LOG_ERROR(
                "rectangle 帧 RenderGraph::compile（含 GPU 阶段）失败");
            device.end_frame();
            break;
        }

        std::vector<std::optional<rhi::RgGpuExecuteFrame>> exec_frame(
            frame_rg.passes().size());
        exec_frame[0].emplace();
        exec_frame[0]->rhi_device = &device;
        exec_frame[0]->swap_image_index = swap_image_index;
        exec_frame[0]->surface_extent = ext;
        exec_frame[0]->dynamic_uniform_offsets = { ubo_slot_offset };

        rhi::CommandBuffer cmd { vk_cmd };
        if (!frame_rg.execute(cmd, rect_gpu_ctx, exec_frame)) {
            LUMEN_APP_LOG_ERROR("rectangle 帧 RenderGraph::execute 失败");
            device.end_frame();
            break;
        }

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
    rect_gpu_ctx.destroy_all(vkdev, &device.graphics_pipeline_cache());
    swap.destroy();
    rect_pl_cache.clear(vkdev);
    rect_dsl_cache.clear(vkdev);
    device.destroy_buffer(rect_gpu.ubo_buffer);
    device.destroy_buffer(rect_gpu.index_buffer);
    device.destroy_buffer(rect_gpu.vertex_buffer);
    device.shutdown();
    ctx.shutdown();
    return 0;
}
