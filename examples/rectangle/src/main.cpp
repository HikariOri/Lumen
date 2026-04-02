/**
 * @file main.cpp
 * @brief 最小 Vulkan 示例：窗口、Swapchain、单
 * Pass、绘制一个彩色三角形（无纹理、无 Descriptor）
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "core/path.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pipeline.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace {

constexpr uint32_t kMaxFramesInFlight { 3 };

struct Vertex {
    glm::vec2 position;
    glm::vec4 color;
};

} // namespace

static int run_triangle() {
    // 创建 Window
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;

    winConfig.title = "Lumen — 矩形示例";
    winConfig.width = 800;
    winConfig.height = 600;
    winConfig.icon_path =
        lumen::core::get_resource_path("assets/icons/哈士奇.png");

    if (!window.create(winConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    lumen::render::ContextConfig ctxConfig;
    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig, window)) {
        LUMEN_APP_LOG_ERROR("Vulkan Instance 创建失败");
        return -1;
    }

    lumen::render::Surface surface(ctx, window);
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Vulkan Surface 创建失败");
        return -1;
    }

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 创建失败");
        return -1;
    }

    int window_width { 0 };
    int window_height { 0 };
    window.get_framebuffer_size(&window_width, &window_height);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(),
                          static_cast<uint32_t>(window_width),
                          static_cast<uint32_t>(window_height))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = false;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             vk::ImageView {})) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    const std::string vertPath =
        lumen::core::get_resource_path("shaders/rectangle.vert.spv");
    const std::string fragPath =
        lumen::core::get_resource_path("shaders/rectangle.frag.spv");

    lumen::render::ShaderModule vertShader;
    lumen::render::ShaderModule fragShader;

    if (!vertShader.create_from_file(ctx.device(), vertPath.c_str())) {
        LUMEN_APP_LOG_ERROR("顶点着色器加载失败: {}", vertPath);
        return -1;
    }
    if (!fragShader.create_from_file(ctx.device(), fragPath.c_str())) {
        LUMEN_APP_LOG_ERROR("片段着色器加载失败: {}", fragPath);
        return -1;
    }

    const std::array<Vertex, 6> vertices { {
        { .position { -0.5F, 0.5F }, .color = { 0.35F, 0.85F, 1.0F, 1.0F } },
        { .position { -0.5F, -0.5F }, .color = { 0.35F, 0.85F, 1.0F, 1.0F } },
        { .position = { 0.5F, -0.5F }, .color = { 0.55F, 1.0F, 0.45F, 1.0F } },

        { .position = { 0.5F, -0.5F }, .color = { 0.55F, 1.0F, 0.45F, 1.0F } },
        { .position { 0.5F, 0.5F }, .color = { 0.35F, 0.85F, 1.0F, 1.0F } },
        { .position = { -0.5F, 0.5F }, .color = { 0.55F, 1.0F, 0.45F, 1.0F } },
    } };

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }
    auto cmdBuffers = cmdPool.allocate(kMaxFramesInFlight);
    if (cmdBuffers.size() != kMaxFramesInFlight) {
        LUMEN_APP_LOG_ERROR("CommandBuffer 分配失败");
        return -1;
    }

    lumen::render::VertexBuffer vertexBuffer;
    if (!vertexBuffer.create_device_local_and_upload(
            ctx, ctx.graphics_queue(), cmdPool, vertices.data(),
            sizeof(vertices))) {
        LUMEN_APP_LOG_ERROR("VertexBuffer 创建失败");
        return -1;
    }

    lumen::render::PipelineLayout pipelineLayout;
    if (!pipelineLayout.create(ctx, {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.shaderStages.push_back({ .module = vertShader.handle(),
                                        .stage = vk::ShaderStageFlagBits::eVertex,
                                        .entryPoint = "main" });
    pipeConfig.shaderStages.push_back({ .module = fragShader.handle(),
                                        .stage = vk::ShaderStageFlagBits::eFragment,
                                        .entryPoint = "main" });
    pipeConfig.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(Vertex),
          .inputRate = vk::VertexInputRate::eVertex });
    pipeConfig.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = vk::Format::eR32G32Sfloat,
          .offset = offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = vk::Format::eR32G32B32A32Sfloat,
          .offset = offsetof(Vertex, color) });
    pipeConfig.depthTest = false;
    pipeConfig.depthWrite = false;
    pipeConfig.cullMode = vk::CullModeFlagBits::eNone;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout, renderPass, 0, pipeConfig)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

    lumen::render::FrameSync frameSync;
    if (!frameSync.create(ctx.device(), swapchain.image_count(),
                          kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    LUMEN_APP_LOG_INFO("最小三角形示例运行中，按 Esc 退出");

    lumen::platform::EventPump pump;
    uint32_t currentFrame { 0 };
    bool running { true };
    int fbWidth { window_width };
    int fbHeight { window_height };
    bool needRecreateSwapchain { false };

    pump.push_layer([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventQuit>([&](lumen::platform::EventQuit &) {
            LUMEN_APP_LOG_INFO("退出");
            running = false;
            return false;
        });
        d.dispatch<lumen::platform::EventKeyDown>(
            [&](lumen::platform::EventKeyDown &e) {
                LUMEN_APP_LOG_INFO("按键按下: {}",
                                   lumen::platform::key_name(e.key));
                if (e.key == lumen::platform::Key::Escape) {
                    running = false;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventMouseMove>(
            [&](lumen::platform::EventMouseMove &e) {
                LUMEN_APP_LOG_INFO("鼠标移动: ({:.0f}, {:.0f})", e.x, e.y);
                return false;
            });
        d.dispatch<lumen::platform::EventWindowResize>(
            [&](lumen::platform::EventWindowResize &r) {
                fbWidth = r.width;
                fbHeight = r.height;
                needRecreateSwapchain = true;
                return false;
            });
    });

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    while (running) {
        if (!pump.poll()) {
            break;
        }

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            lumen::render::recreate_swapchain_resources(
                ctx, swapchain, framebuffers, frameSync, renderPass,
                static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight),
                kMaxFramesInFlight, vk::ImageView {});
            needRecreateSwapchain = false;
            continue;
        }

        while (!frameSync.wait_fence(currentFrame, kFenceWaitTimeoutNs)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running) {
            break;
        }

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), {},
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        auto &cmdBuf = cmdBuffers[currentFrame];
        cmdBuf.reset({});

        vk::CommandBufferBeginInfo frameBegin {};
        frameBegin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        if (cmdBuf.begin(&frameBegin) != vk::Result::eSuccess) {
            continue;
        }

        std::array<vk::ClearValue, 1> clearValues {};
        clearValues[0].color.float32[0] = 0.06F;
        clearValues[0].color.float32[1] = 0.07F;
        clearValues[0].color.float32[2] = 0.10F;
        clearValues[0].color.float32[3] = 1.0F;

        vk::RenderPassBeginInfo rpBegin {};
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = vk::Offset2D { 0, 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount =
            static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        cmdBuf.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        const vk::Viewport viewport {
            0.0F, 0.0F,
            static_cast<float>(swapchain.extent().width),
            static_cast<float>(swapchain.extent().height),
            0.0F, 1.0F};
        cmdBuf.setViewport(0, { viewport });

        const vk::Rect2D scissor { vk::Offset2D { 0, 0 },
                                   swapchain.extent() };
        cmdBuf.setScissor(0, { scissor });

        cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                            pipeline.handle());

        const vk::Buffer vb = vertexBuffer.handle();
        const vk::DeviceSize vbo { 0 };
        cmdBuf.bindVertexBuffers(0, { vb }, { vbo });
        cmdBuf.draw(6, 1, 0, 0);

        cmdBuf.endRenderPass();

        cmdBuf.end();

        const vk::Semaphore wait_sem = frameSync.image_available(currentFrame);
        const vk::Semaphore signal_sem = frameSync.render_finished(imageIndex);
        const std::array<vk::PipelineStageFlags, 1> wait_stages {
            vk::PipelineStageFlagBits::eColorAttachmentOutput};

        vk::SubmitInfo submitInfo {};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &wait_sem;
        submitInfo.pWaitDstStageMask = wait_stages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signal_sem;

        if (!frameSync.reset_fence(currentFrame)) {
            LUMEN_LOG_ERROR("FrameSync::reset_fence 失败 currentFrame={}",
                            currentFrame);
            continue;
        }
        if (ctx.graphics_queue().submit(
                1, &submitInfo, frameSync.in_flight_fence(currentFrame)) !=
            vk::Result::eSuccess) {
            continue;
        }

        const vk::Result presentResult =
            swapchain.present(ctx.present_queue(), imageIndex,
                              frameSync.render_finished(imageIndex));
        if (presentResult == vk::Result::eErrorOutOfDateKHR) {
            needRecreateSwapchain = true;
        } else if (presentResult != vk::Result::eSuccess &&
                   presentResult != vk::Result::eSuboptimalKHR) {
            LUMEN_APP_LOG_WARN("Present 失败 result={}",
                               static_cast<int>(presentResult));
        }

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    LUMEN_APP_LOG_INFO("退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int result = run_triangle();
    lumen::core::Logger::shutdown();
    return result;
}
