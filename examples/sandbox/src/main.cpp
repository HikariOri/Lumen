/**
 * @file main.cpp
 * @brief Sandbox：测试引擎功能，纹理矩形
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
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

struct Vertex {
    glm::vec2 position;
    glm::vec2 uv;
};

/// UBO 与着色器 layout(std140) 对应
struct UBO {
    glm::vec2 position { 0.0f, 0.0f };
    float rotation { 0.0f };
    float _pad; // std140 对齐到 16 字节
};

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };

} // namespace

static int run_sandbox() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen Sandbox - 纹理矩形";
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
    LUMEN_APP_LOG_INFO("Sandbox 启动");
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
    {
        auto gpu = ctx.physical_device_info();
        LUMEN_APP_LOG_INFO("Vulkan Device 创建成功: {} ({})", gpu.deviceName,
                           lumen::render::device_type_name(gpu.deviceType));
        if (gpu.deviceLocalMemoryBytes > 0) {
            LUMEN_APP_LOG_INFO("  显存: {:.1f} GiB",
                               gpu.deviceLocalMemoryBytes /
                                   (1024.0 * 1024.0 * 1024.0));
        }
    }

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }
    LUMEN_APP_LOG_INFO("Swapchain 创建成功, {} 张图像",
                       swapchain.image_count());

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

    // Shaders（纹理矩形）
    std::string vertPath =
        lumen::core::get_resource_path("shaders/texture.vert.spv");
    std::string fragPath =
        lumen::core::get_resource_path("shaders/texture.frag.spv");
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

    // 矩形 4 个顶点（左下、左上、右上、右下）+ UV
    const std::array<Vertex, 4> vertices = { {
        { glm::vec2(-0.5f, -0.5f), glm::vec2(0.0f, 1.0f) }, // 左下
        { glm::vec2(-0.5f, 0.5f), glm::vec2(0.0f, 0.0f) },  // 左上
        { glm::vec2(0.5f, 0.5f), glm::vec2(1.0f, 0.0f) },   // 右上
        { glm::vec2(0.5f, -0.5f), glm::vec2(1.0f, 1.0f) },  // 右下
    } };

    // 索引：两个三角形组成矩形（0,1,2 和 0,2,3）
    const std::array<uint16_t, 6> indices = { 0, 1, 2, 0, 2, 3 };

    lumen::render::VertexBuffer vertexBuffer;
    if (!vertexBuffer.create(ctx, sizeof(vertices))) {
        LUMEN_APP_LOG_ERROR("VertexBuffer 创建失败");
        return -1;
    }
    vertexBuffer.upload(vertices.data(), sizeof(vertices));

    lumen::render::IndexBuffer indexBuffer;
    if (!indexBuffer.create(ctx, sizeof(indices))) {
        LUMEN_APP_LOG_ERROR("IndexBuffer 创建失败");
        return -1;
    }
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint16);
    indexBuffer.upload(indices.data(), sizeof(indices));

    // CommandPool（纹理加载需提前创建）
    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    // 纹理：优先从文件加载，失败则用程序化棋盘格
    lumen::render::Texture texture;
    std::string texPath = lumen::core::get_resource_path(
        "./assets/textures/ikun2026_happy_new_year.jpg");
    if (!texture.create_from_file(ctx, texPath.c_str(), ctx.graphics_queue(),
                                  cmdPool)) {
        // 程序化棋盘格纹理 64x64 RGBA
        constexpr uint32_t kTexSize = 64;
        std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
        for (uint32_t y = 0; y < kTexSize; ++y) {
            for (uint32_t x = 0; x < kTexSize; ++x) {
                bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                uint8_t v = checker ? 255 : 80;
                size_t i = (y * kTexSize + x) * 4;
                pixels[i + 0] = v;
                pixels[i + 1] = v;
                pixels[i + 2] = checker ? 180 : 100;
                pixels[i + 3] = 255;
            }
        }
        if (!texture.create_from_memory(ctx, pixels.data(), pixels.size(),
                                        kTexSize, kTexSize,
                                        ctx.graphics_queue(), cmdPool)) {
            LUMEN_APP_LOG_ERROR("纹理创建失败");
            return -1;
        }
        LUMEN_APP_LOG_INFO("使用程序化棋盘格纹理");
    } else {
        LUMEN_APP_LOG_INFO("纹理加载成功: {}", texPath);
    }

    // Per-frame UniformBuffer（避免多帧并发时覆盖）
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!uniformBuffers[i].create(ctx, sizeof(UBO))) {
            LUMEN_APP_LOG_ERROR("UniformBuffer[{}] 创建失败", i);
            return -1;
        }
    }

    // DescriptorSetLayout：binding 0 = UBO, binding 1 = 纹理
    lumen::render::DescriptorSetLayout descLayout;
    std::vector<lumen::render::DescriptorBinding> bindings = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
          VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!descLayout.create(ctx, bindings)) {
        LUMEN_APP_LOG_ERROR("DescriptorSetLayout 创建失败");
        return -1;
    }

    // DescriptorPool 与 Per-frame DescriptorSet
    lumen::render::DescriptorPool descPool;
    if (!descPool.create(
            ctx,
            { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight },
              { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                kMaxFramesInFlight } },
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
        lumen::render::write_descriptor_image(
            ctx.device(), descriptorSets[i], 1, texture.view(),
            texture.sampler(), texture.descriptor_layout());
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
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
    pipeConfig.depthTest = false;
    pipeConfig.depthWrite = false;
    pipeConfig.cullMode = VK_CULL_MODE_NONE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(), renderPass.handle(), 0,
                         pipeConfig)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

    // CommandBuffers（cmdPool 已用于纹理加载）
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

    LUMEN_APP_LOG_INFO("引擎初始化完成，进入主循环 [WASD] 移动 [QE] 旋转");
    float lastLoggedTime = -10.0f; // 用于限速调试日志
    uint64_t frameCount = 0;       // 用于限速调试日志
    glm::vec2 rectPos { 0.0f, 0.0f };
    float rectRotation { 0.0f };
    double lastTime = lumen::core::get_time_seconds();
    constexpr float kMoveSpeed = 1.5f;
    constexpr float kRotSpeed = 2.5f;
    constexpr uint64_t kLogInterval = 60; // 每 N 帧输出一次阶段日志
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };

    lumen::platform::EventPump pump;
    uint32_t currentFrame { 0 };
    bool running { true };

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape) {
            running = false;
            return;
        }
        LUMEN_APP_LOG_DEBUG("按键按下: {} ({}){}",
                            lumen::platform::key_name(e.key), e.key,
                            e.repeat ? " 重复" : "");
    });
    pump.on_key_up([](const lumen::platform::EventKeyUp &e) {
        LUMEN_APP_LOG_DEBUG("按键松开: {} ({})",
                            lumen::platform::key_name(e.key), e.key);
    });
    pump.on_mouse_button_down(
        [](const lumen::platform::EventMouseButtonDown &e) {
            LUMEN_APP_LOG_DEBUG("鼠标按下: {} ({:.0f}, {:.0f})",
                                lumen::platform::mouse_button_name(e.button),
                                e.x, e.y);
        });
    pump.on_mouse_button_up([](const lumen::platform::EventMouseButtonUp &e) {
        LUMEN_APP_LOG_DEBUG("鼠标松开: {} ({:.0f}, {:.0f})",
                            lumen::platform::mouse_button_name(e.button), e.x,
                            e.y);
    });
    pump.on_mouse_wheel([](const lumen::platform::EventMouseWheel &e) {
        LUMEN_APP_LOG_DEBUG("滚轮: dx={:.1f} dy={:.1f}", e.deltaX, e.deltaY);
    });
    pump.on_mouse_move([](const lumen::platform::EventMouseMove &e) {
        LUMEN_APP_LOG_DEBUG("鼠标移动: ({:.0f}, {:.0f})", e.x, e.y);
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
        LUMEN_APP_LOG_DEBUG("窗口大小: {}x{}", r.width, r.height);
    });

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    while (running) {
        bool doLog = (frameCount < 5) || (frameCount % kLogInterval == 0);
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 循环开始", frameCount);
        }

        if (!pump.poll()) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 检测到退出", frameCount);
            break;
        }

        // 窗口 resize 或 Present 返回 OUT_OF_DATE 时重建 Swapchain
        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            lumen::render::recreate_swapchain_resources(
                ctx, swapchain, framebuffers, frameSync, renderPass.handle(),
                static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight),
                kMaxFramesInFlight, VK_NULL_HANDLE);
            needRecreateSwapchain = false;
            continue;
        }

        // 只等待即将复用的 currentFrame 的 fence，避免等待从未被 submit 的
        // prevFrame
        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 等待 fence curr={} ...", frameCount,
                                currentFrame);
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

        // 键盘控制：WASD 移动，QE 旋转
        double now = lumen::core::get_time_seconds();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        const auto &inp = pump.input();
        if (inp.is_key_down(lumen::platform::Key::W))
            rectPos.y -= kMoveSpeed * dt; // Vulkan NDC Y 向下，减为上
        if (inp.is_key_down(lumen::platform::Key::S))
            rectPos.y += kMoveSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::A))
            rectPos.x -= kMoveSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::D))
            rectPos.x += kMoveSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::Q))
            rectRotation -= kRotSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::E))
            rectRotation += kRotSpeed * dt;

        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] acquire 图像 ...", frameCount);
        }

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            if (doLog) {
                LUMEN_APP_LOG_DEBUG("[frame {}] acquire 失败/超时，跳过",
                                    frameCount);
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
            LUMEN_APP_LOG_DEBUG("[frame {}] vkBeginCommandBuffer 失败",
                                frameCount);
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
        ubo.position = rectPos;
        ubo.rotation = rectRotation;
        uniformBuffers[currentFrame].update(ubo);

        vkCmdBindDescriptorSets(cmdBuffers[currentFrame],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmdBuffers[currentFrame], 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmdBuffers[currentFrame], indexBuffer.handle(), 0,
                             indexBuffer.vk_index_type());
        vkCmdDrawIndexed(cmdBuffers[currentFrame], 6, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmdBuffers[currentFrame]);

        if (vkEndCommandBuffer(cmdBuffers[currentFrame]) != VK_SUCCESS) {
            LUMEN_APP_LOG_DEBUG("[frame {}] vkEndCommandBuffer 失败",
                                frameCount);
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
        } else if (presentResult != VK_SUCCESS &&
                   presentResult != VK_SUBOPTIMAL_KHR) {
            LUMEN_APP_LOG_DEBUG("[frame {}] Present 失败 result={}", frameCount,
                                static_cast<int>(presentResult));
        }

        if (doLog) {
            LUMEN_APP_LOG_DEBUG("[frame {}] 帧完成", frameCount);
        }
        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
        ++frameCount;
    }

    ctx.wait_idle();

    LUMEN_APP_LOG_INFO("Sandbox 退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    int result = run_sandbox();
    lumen::core::Logger::shutdown();
    return result;
}
