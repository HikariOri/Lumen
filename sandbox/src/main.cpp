/**
 * @file main.cpp
 * @brief Sandbox：测试引擎功能，绘制三角形
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "core/time.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pipeline.hpp"
#include "render/resource/descriptor.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec2 position;
    glm::vec4 color;
};

/// UBO 与着色器 layout(std140) 对应
struct UBO {
    float time;
    float _pad[3]; // std140 对齐到 16 字节
};
#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {

    constexpr uint32_t kMaxFramesInFlight { 2 };

    std::string get_shader_path(const char *name) {
        static std::string basePath;
        if (basePath.empty()) {
            const char *base = SDL_GetBasePath();
            if (!base) {
                return std::string { name };
            }
            basePath = base;
            SDL_free(const_cast<void *>(static_cast<const void *>(base)));
        }
        return basePath + "shaders/" + name;
    }

} // namespace

int main() {
    lumen::core::LoggerConfig logConfig;
    logConfig.engine.level = spdlog::level::debug;
    logConfig.app.level = spdlog::level::debug;
    if (!lumen::core::Logger::init(logConfig)) {
        return -1;
    }

    LUMEN_APP_LOG_INFO("Sandbox 启动");
    LUMEN_APP_LOG_INFO("应用层日志测试");

    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "LearnVulkan Sandbox - 三角形";
    winConfig.width = 800;
    winConfig.height = 600;

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
    LUMEN_APP_LOG_INFO("Vulkan Instance 创建成功");

    lumen::render::Surface surface(
        ctx.instance(), window.create_vulkan_surface(ctx.instance()));
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Vulkan Surface 创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("Vulkan Surface 创建成功");

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("Vulkan Device 创建成功");

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("Swapchain 创建成功, {} 张图像", swapchain.image_count());

    // RenderPass（无深度，与 Swapchain 格式匹配）
    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = false;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    // Framebuffers
    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             VK_NULL_HANDLE)) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    // Shaders
    std::string vertPath = get_shader_path("triangle.vert.spv");
    std::string fragPath = get_shader_path("triangle.frag.spv");
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

    const std::array<Vertex, 3> vertices = { {
        { { 0.0f, -0.5f }, { 1.0f, 0.75f, 0.85f, 1.0f } },   // 珊瑚粉
        { { -0.5f, 0.5f }, { 1.0f, 0.7f, 0.9f, 1.0f } },    // 玫粉
        { { 0.5f, 0.5f }, { 0.95f, 0.8f, 0.95f, 1.0f } },   // 薰衣草粉
    } };

    lumen::render::VertexBuffer vertexBuffer;
    if (!vertexBuffer.create(ctx, sizeof(vertices))) {
        LUMEN_APP_LOG_ERROR("VertexBuffer 创建失败");
        return -1;
    }
    vertexBuffer.upload(vertices.data(), sizeof(vertices));

    // Per-frame UniformBuffer（避免多帧并发时覆盖）
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!uniformBuffers[i].create(ctx, sizeof(UBO))) {
            LUMEN_APP_LOG_ERROR("UniformBuffer[{}] 创建失败", i);
            return -1;
        }
    }

    // DescriptorSetLayout：binding 0 = UBO (fragment)
    lumen::render::DescriptorSetLayout descLayout;
    std::vector<lumen::render::DescriptorBinding> bindings = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
          VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    if (!descLayout.create(ctx, bindings)) {
        LUMEN_APP_LOG_ERROR("DescriptorSetLayout 创建失败");
        return -1;
    }

    // DescriptorPool 与 Per-frame DescriptorSet
    lumen::render::DescriptorPool descPool;
    if (!descPool.create(
            ctx, { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight } },
            kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
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
            sizeof(UBO));
    }

    // PipelineLayout（含 descriptor set layout）
    lumen::render::PipelineLayout pipelineLayout;
    if (!pipelineLayout.create(ctx, { descLayout.handle() }, {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    // GraphicsPipeline
    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.stages.push_back(
        { vertShader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipeConfig.stages.push_back(
        { fragShader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipeConfig.vertexBindings.push_back(
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
    pipeConfig.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
    pipeConfig.depthTest = false;
    pipeConfig.depthWrite = false;
    pipeConfig.cullMode = VK_CULL_MODE_NONE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(), renderPass.handle(), 0,
                         pipeConfig)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

    // CommandPool & CommandBuffers
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

    // FrameSync：per-image semaphores 避免 Swapchain 复用冲突
    lumen::render::FrameSync frameSync;
    if (!frameSync.create(ctx.device(), swapchain.image_count(),
                          kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    LUMEN_APP_LOG_INFO("引擎初始化完成，进入主循环");
    float lastLoggedTime = -10.0f; // 用于限速调试日志
    uint64_t frameCount = 0;       // 用于限速调试日志
    constexpr uint64_t kLogInterval = 60; // 每 N 帧输出一次阶段日志
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };

    lumen::platform::EventPump pump;
    lumen::platform::EventList events;
    lumen::platform::Input input;
    uint32_t currentFrame { 0 };
    bool running { true };

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    auto pump_and_check_quit = [&]() {
        if (!pump.poll(events, input)) {
            return true;
        }
        for (const auto &e : events) {
            if (std::holds_alternative<lumen::platform::EventQuit>(e)) {
                return true;
            }
        }
        return false;
    };

    while (running) {
        bool doLog = (frameCount < 5) || (frameCount % kLogInterval == 0);
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 循环开始", frameCount);
        }

        if (pump_and_check_quit()) {
            LUMEN_APP_LOG_DEBUG("[frame {}] pump 检测到退出", frameCount);
            running = false;
            break;
        }
        for (const auto &e : events) {
            if (std::holds_alternative<lumen::platform::EventWindowResize>(e)) {
                const auto &r = std::get<lumen::platform::EventWindowResize>(e);
                fbWidth = r.width;
                fbHeight = r.height;
                needRecreateSwapchain = true;
                LUMEN_APP_LOG_DEBUG("窗口大小: {}x{}", r.width, r.height);
            }
        }

        // 窗口 resize 或 Present 返回 OUT_OF_DATE 时重建 Swapchain
        if (needRecreateSwapchain) {
            vkDeviceWaitIdle(ctx.device());
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            if (fbWidth > 0 && fbHeight > 0 &&
                swapchain.resize(static_cast<uint32_t>(fbWidth),
                                static_cast<uint32_t>(fbHeight))) {
                framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                                   VK_NULL_HANDLE);
                frameSync.create(ctx.device(), swapchain.image_count(),
                                kMaxFramesInFlight);
                LUMEN_APP_LOG_DEBUG("Swapchain 已重建 {}x{}", fbWidth, fbHeight);
            }
            needRecreateSwapchain = false;
            continue;
        }

        // 只等待即将复用的 currentFrame 的 fence，避免等待从未被 submit 的 prevFrame
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 等待 fence curr={} ...", frameCount,
                            currentFrame);
        }
        while (!frameSync.wait_fence(currentFrame, kFenceWaitTimeoutNs)) {
            if (pump_and_check_quit()) {
                running = false;
                goto exit_loop;
            }
            SDL_Delay(1);
        }
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] acquire 图像 ...", frameCount);
        }

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            if (doLog) {
                LUMEN_APP_LOG_DEBUG("[frame {}] acquire 失败/超时，跳过", frameCount);
            }
            continue;
        }
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] acquired imageIndex={}", frameCount,
                            imageIndex);
        }

        vkResetCommandBuffer(cmdBuffers[currentFrame], 0);

        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmdBuffers[currentFrame], &beginInfo) !=
            VK_SUCCESS) {
            LUMEN_APP_LOG_DEBUG("[frame {}] vkBeginCommandBuffer 失败", frameCount);
            continue;
        }

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

        UBO ubo {};
        ubo.time = static_cast<float>(lumen::core::get_time_seconds());
        uniformBuffers[currentFrame].update(ubo);
        // 调试：每 2 秒打印一次 time，确认是否递增（调试完可删除）
        if (ubo.time - lastLoggedTime > 2.0f) {
            LUMEN_APP_LOG_DEBUG("UBO time={}", ubo.time);
            lastLoggedTime = ubo.time;
        }

        vkCmdBindDescriptorSets(cmdBuffers[currentFrame],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmdBuffers[currentFrame], 0, 1, &vb, &vbOffset);
        vkCmdDraw(cmdBuffers[currentFrame], 3, 1, 0, 0);

        vkCmdEndRenderPass(cmdBuffers[currentFrame]);

        if (vkEndCommandBuffer(cmdBuffers[currentFrame]) != VK_SUCCESS) {
            LUMEN_APP_LOG_DEBUG("[frame {}] vkEndCommandBuffer 失败", frameCount);
            continue;
        }

        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] QueueSubmit ...", frameCount);
        }
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
            VK_SUCCESS) {
            LUMEN_APP_LOG_DEBUG("[frame {}] vkQueueSubmit 失败", frameCount);
            continue;
        }

        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] Present ...", frameCount);
        }
        VkResult presentResult =
            swapchain.present(ctx.present_queue(), imageIndex,
                              frameSync.render_finished(imageIndex));
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
            needRecreateSwapchain = true;
        } else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            LUMEN_APP_LOG_DEBUG("[frame {}] Present 失败 result={}", frameCount,
                            static_cast<int>(presentResult));
        }

        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 帧完成", frameCount);
        }
        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
        ++frameCount;
    }
exit_loop:

    vkDeviceWaitIdle(ctx.device());

    LUMEN_APP_LOG_INFO("Sandbox 退出");
    lumen::core::Logger::shutdown();
    return 0;
}
