/**
 * @file main.cpp
 * @brief Shadertoy：全屏片段着色，复现 Shadertoy 效果
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "core/path.hpp"
#include "core/time.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pipeline.hpp"
#include "render/resource/descriptor.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <array>
#include <string>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

/// Shadertoy 兼容 UBO (std140)，vec3 后需 padding 对齐
struct ShadertoyUBO  {
    glm::vec3 iResolution { 1.0f, 1.0f, 1.0f };
    float _padRes { 0.0f };
    float iTime { 0.0f };
    float iTimeDelta { 0.0f };
    float iFrame { 0.0f };
    float _pad0 { 0.0f };
    glm::vec4 iMouse { 0.0f, 0.0f, 0.0f, 0.0f };
};

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };

} // namespace

static int run_shadertoy() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Shadertoy - Vulkan";
    winConfig.width = 1280;
    winConfig.height = 720;

    if (!window.create(winConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("窗口创建成功: {}x{}", window.width(), window.height());

    auto extensions = window.get_vulkan_instance_extensions();
    lumen::render::ContextConfig ctxConfig;
    ctxConfig.instanceExtensions.assign(extensions.begin(), extensions.end());

    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig)) {
        LUMEN_APP_LOG_ERROR("Vulkan Instance 创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("Shadertoy 启动");

    lumen::render::Surface surface(
        ctx.instance(), window.create_vulkan_surface(ctx.instance()));
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Vulkan Surface 创建失败");
        return -1;
    }

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 创建失败");
        return -1;
    }
    {
        auto gpu = ctx.physical_device_info();
        LUMEN_APP_LOG_INFO("Vulkan Device: {} ({})", gpu.deviceName,
                           lumen::render::device_type_name(gpu.deviceType));
    }

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
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
                             VK_NULL_HANDLE)) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    std::string vertPath =
        lumen::core::get_resource_path("shaders/shadertoy.vert.spv");
    std::string fragPath =
        lumen::core::get_resource_path("shaders/shadertoy.frag.spv");
    lumen::render::ShaderModule vertShader;
    lumen::render::ShaderModule fragShader;
    if (!vertShader.create_from_file(ctx.device(), vertPath.c_str()) ||
        !fragShader.create_from_file(ctx.device(), fragPath.c_str())) {
        LUMEN_APP_LOG_ERROR("Shadertoy 着色器加载失败");
        return -1;
    }

    lumen::render::DescriptorSetLayout descLayout;
    std::vector<lumen::render::DescriptorBinding> bindings = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
          VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!descLayout.create(ctx, bindings)) {
        LUMEN_APP_LOG_ERROR("DescriptorSetLayout 创建失败");
        return -1;
    }

    lumen::render::DescriptorPool descPool;
    if (!descPool.create(
            ctx, { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight } },
            kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
    }

    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!uniformBuffers[i].create(ctx, sizeof(ShadertoyUBO))) {
            LUMEN_APP_LOG_ERROR("UniformBuffer[{}] 创建失败", i);
            return -1;
        }
    }

    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!descPool.allocate(ctx.device(), descLayout.handle(),
                               descriptorSets[i])) {
            LUMEN_APP_LOG_ERROR("DescriptorSet[{}] 分配失败", i);
            return -1;
        }
        lumen::render::write_descriptor_buffer(
            ctx.device(), descriptorSets[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBuffers[i].handle(), 0,
            sizeof(ShadertoyUBO));
    }

    lumen::render::PipelineLayout pipelineLayout;
    if (!pipelineLayout.create(ctx, { descLayout.handle() }, {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.stages.push_back(
        { vertShader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipeConfig.stages.push_back(
        { fragShader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipeConfig.vertexBindings = {};
    pipeConfig.vertexAttributes = {};
    pipeConfig.depthTest = false;
    pipeConfig.depthWrite = false;
    pipeConfig.cullMode = VK_CULL_MODE_NONE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(), renderPass.handle(), 0,
                         pipeConfig)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

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

    lumen::render::FrameSync frameSync;
    if (!frameSync.create(ctx.device(), swapchain.image_count(),
                          kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    LUMEN_APP_LOG_INFO("Shadertoy 启动完成，拖动鼠标控制相机 [ESC] 退出");

    float mouseX { 0.0f }, mouseY { 0.0f };
    float mouseClickX { 0.0f }, mouseClickY { 0.0f };
    uint64_t frameCount { 0 };
    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };
    uint32_t currentFrame { 0 };
    bool running { true };

    lumen::platform::EventPump pump;

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape)
            running = false;
    });
    pump.on_mouse_move([&](const lumen::platform::EventMouseMove &e) {
        mouseX = static_cast<float>(e.x);
        mouseY = static_cast<float>(e.y);
    });
    pump.on_mouse_button_down(
        [&](const lumen::platform::EventMouseButtonDown &e) {
            mouseClickX = static_cast<float>(e.x);
            mouseClickY = static_cast<float>(e.y);
        });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
    });

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    while (running) {
        if (!pump.poll())
            break;

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            lumen::render::recreate_swapchain_resources(
                ctx, swapchain, framebuffers, frameSync, renderPass.handle(),
                static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight),
                kMaxFramesInFlight, VK_NULL_HANDLE);
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
        if (!running)
            break;

        const float dt = static_cast<float>(frame_dt.tick_seconds());

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX)
            continue;

        vkResetCommandBuffer(cmdBuffers[currentFrame], 0);

        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmdBuffers[currentFrame], &beginInfo) !=
            VK_SUCCESS)
            continue;

        VkRenderPassBeginInfo rpBegin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        VkClearValue clearColor {};
        clearColor.color = { { 0.1f, 0.1f, 0.15f, 1.0f } };
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmdBuffers[currentFrame], &rpBegin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffers[currentFrame], 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { 0, 0 };
        scissor.extent = swapchain.extent();
        vkCmdSetScissor(cmdBuffers[currentFrame], 0, 1, &scissor);

        vkCmdBindPipeline(cmdBuffers[currentFrame],
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

        ShadertoyUBO ubo {};
        ubo.iResolution.x = static_cast<float>(swapchain.extent().width);
        ubo.iResolution.y = static_cast<float>(swapchain.extent().height);
        ubo.iResolution.z = 1.0f;
        ubo.iTime = static_cast<float>(frame_dt.last_steady_seconds());
        ubo.iTimeDelta = dt;
        ubo.iFrame = static_cast<float>(frameCount);
        ubo.iMouse = { mouseX, mouseY, mouseClickX, mouseClickY };
        uniformBuffers[currentFrame].update(ubo);

        vkCmdBindDescriptorSets(cmdBuffers[currentFrame],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        vkCmdDraw(cmdBuffers[currentFrame], 3, 1, 0, 0);

        vkCmdEndRenderPass(cmdBuffers[currentFrame]);

        if (vkEndCommandBuffer(cmdBuffers[currentFrame]) != VK_SUCCESS)
            continue;

        VkSemaphore waitSem = frameSync.image_available(currentFrame);
        VkSemaphore signalSem = frameSync.render_finished(imageIndex);

        VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSem;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffers[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSem;

        if (vkQueueSubmit(ctx.graphics_queue(), 1, &submitInfo,
                          frameSync.in_flight_fence(currentFrame)) !=
            VK_SUCCESS)
            continue;

        VkResult presentResult =
            swapchain.present(ctx.present_queue(), imageIndex, signalSem);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
            needRecreateSwapchain = true;
        }

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
        ++frameCount;
    }

    ctx.wait_idle();

    LUMEN_APP_LOG_INFO("Shadertoy 退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    int result = run_shadertoy();
    lumen::core::Logger::shutdown();
    return result;
}
