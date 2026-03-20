/**
 * @file main.cpp
 * @brief Demo3D：进入 3D 世界 - 透视、深度缓冲、纹理立方体
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
#include "render/resource/image.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <array>
#include <string>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;
};

/// UBO：mat4 mvp (std140 对齐，64 字节)
struct UBO {
    glm::mat4 mvp;
};

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };

// 立方体：24 顶点（每面 4 个，便于 UV 映射），36 索引
const std::array<Vertex, 24> kCubeVertices = { {
    // 前 (+Z)
    { { -0.5f, -0.5f, 0.5f }, { 0.0f, 1.0f } },
    { { -0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f } },
    { { 0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f } },
    { { 0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f } },
    // 后 (-Z)
    { { 0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
    { { 0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f } },
    { { -0.5f, 0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
    // 右 (+X)
    { { 0.5f, -0.5f, 0.5f }, { 0.0f, 1.0f } },
    { { 0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f } },
    { { 0.5f, 0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { 0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
    // 左 (-X)
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
    { { -0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f } },
    { { -0.5f, 0.5f, 0.5f }, { 1.0f, 0.0f } },
    { { -0.5f, -0.5f, 0.5f }, { 1.0f, 1.0f } },
    // 上 (+Y)
    { { -0.5f, 0.5f, 0.5f }, { 0.0f, 1.0f } },
    { { -0.5f, 0.5f, -0.5f }, { 0.0f, 0.0f } },
    { { 0.5f, 0.5f, -0.5f }, { 1.0f, 0.0f } },
    { { 0.5f, 0.5f, 0.5f }, { 1.0f, 1.0f } },
    // 下 (-Y)
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } },
    { { -0.5f, -0.5f, 0.5f }, { 0.0f, 0.0f } },
    { { 0.5f, -0.5f, 0.5f }, { 1.0f, 0.0f } },
    { { 0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
} };

const std::array<uint16_t, 36> kCubeIndices = {
    0,  1,  2,  0,  2,  3,   // 前
    4,  5,  6,  4,  6,  7,   // 后
    8,  9,  10, 8,  10, 11,  // 右
    12, 13, 14, 12, 14, 15,  // 左
    16, 17, 18, 16, 18, 19,  // 上
    20, 21, 22, 20, 22, 23,  // 下
};

} // namespace

static int run_demo3d() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Demo3D - 纹理立方体";
    winConfig.width = 1280;
    winConfig.height = 720;

    if (!window.create(winConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    auto extensions = window.get_vulkan_instance_extensions();
    lumen::render::ContextConfig ctxConfig;
    ctxConfig.instanceExtensions.assign(extensions.begin(), extensions.end());

    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig)) {
        LUMEN_APP_LOG_ERROR("Vulkan Instance 初始化失败");
        return -1;
    }

    lumen::render::Surface surface(ctx.instance(),
                                   window.create_vulkan_surface(ctx.instance()));
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Surface 创建失败");
        return -1;
    }

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 初始化失败");
        return -1;
    }

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        return -1;
    }

    // RenderPass：开启深度缓冲
    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = true;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        return -1;
    }

    // 深度附件
    lumen::render::Image depthImage;
    if (!depthImage.create_depth_attachment(ctx, static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h))) {
        LUMEN_APP_LOG_ERROR("深度附件创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             depthImage.view())) {
        return -1;
    }

    // 着色器
    std::string vertPath =
        lumen::core::get_resource_path("shaders/cube.vert.spv");
    std::string fragPath =
        lumen::core::get_resource_path("shaders/cube.frag.spv");
    lumen::render::ShaderModule vertShader;
    lumen::render::ShaderModule fragShader;
    if (!vertShader.create_from_file(ctx.device(), vertPath.c_str()) ||
        !fragShader.create_from_file(ctx.device(), fragPath.c_str())) {
        LUMEN_APP_LOG_ERROR("着色器加载失败");
        return -1;
    }

    // 顶点 / 索引缓冲
    lumen::render::VertexBuffer vertexBuffer;
    lumen::render::IndexBuffer indexBuffer;
    if (!vertexBuffer.create(ctx, sizeof(kCubeVertices)) ||
        !indexBuffer.create(ctx, sizeof(kCubeIndices))) {
        return -1;
    }
    vertexBuffer.upload(kCubeVertices.data(), sizeof(kCubeVertices));
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint16);
    indexBuffer.upload(kCubeIndices.data(), sizeof(kCubeIndices));

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        return -1;
    }

    // 纹理
    lumen::render::Texture texture;
    std::string texPath =
        lumen::core::get_resource_path("assets/textures/ikun2026_happy_new_year.jpg");
    if (!texture.create_from_file(ctx, texPath.c_str(), ctx.graphics_queue(),
                                  cmdPool)) {
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
        texture.create_from_memory(ctx, pixels.data(), pixels.size(), kTexSize,
                                  kTexSize, ctx.graphics_queue(), cmdPool);
    }

    // Descriptor
    lumen::render::DescriptorSetLayout descLayout;
    descLayout.create(ctx,
                      { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                         VK_SHADER_STAGE_VERTEX_BIT },
                        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                          VK_SHADER_STAGE_FRAGMENT_BIT } });

    lumen::render::DescriptorPool descPool;
    descPool.create(
        ctx,
        { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight },
          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFramesInFlight } },
        kMaxFramesInFlight);

    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        uniformBuffers[i].create(ctx, sizeof(UBO));
        descPool.allocate(ctx.device(), descLayout.handle(), descriptorSets[i]);
        lumen::render::write_descriptor_buffer(
            ctx.device(), descriptorSets[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBuffers[i].handle(), 0,
            sizeof(UBO));
        lumen::render::write_descriptor_image(
            ctx.device(), descriptorSets[i], 1, texture.view(),
            texture.sampler(), texture.descriptor_layout());
    }

    lumen::render::PipelineLayout pipelineLayout;
    pipelineLayout.create(ctx, { descLayout.handle() }, {});

    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.stages.push_back(
        { vertShader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipeConfig.stages.push_back(
        { fragShader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipeConfig.vertexBindings.push_back(
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
    pipeConfig.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    pipeConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    pipeConfig.frontFace = VK_FRONT_FACE_CLOCKWISE;  // 投影 Y 翻转后需配合

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(), renderPass.handle(), 0,
                         pipeConfig)) {
        return -1;
    }

    auto cmdBuffers = cmdPool.allocate(kMaxFramesInFlight);
    lumen::render::FrameSync frameSync;
    frameSync.create(ctx.device(), swapchain.image_count(), kMaxFramesInFlight);

    LUMEN_APP_LOG_INFO("Demo3D 启动 [WASD] 旋转 [ESC] 退出");

    float orbitYaw { 0.0f };
    float orbitPitch { 0.3f };
    float orbitRadius { 2.5f };
    float modelRotation { 0.0f };
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };
    uint32_t currentFrame { 0 };
    bool running { true };
    double lastTime = lumen::core::get_time_seconds();

    lumen::platform::EventPump pump;
    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape)
            running = false;
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
    });

    constexpr float kOrbitSpeed = 1.2f;
    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    while (running) {
        if (!pump.poll())
            break;

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            if (fbWidth > 0 && fbHeight > 0) {
                lumen::render::Image newDepth;
                newDepth.create_depth_attachment(ctx,
                                                 static_cast<uint32_t>(fbWidth),
                                                 static_cast<uint32_t>(fbHeight));
                depthImage = std::move(newDepth);
                lumen::render::recreate_swapchain_resources(
                    ctx, swapchain, framebuffers, frameSync, renderPass.handle(),
                    static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), kMaxFramesInFlight,
                    depthImage.view());
            }
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

        double now = lumen::core::get_time_seconds();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;

        const auto &inp = pump.input();
        if (inp.is_key_down(lumen::platform::Key::A))
            orbitYaw += kOrbitSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::D))
            orbitYaw -= kOrbitSpeed * dt;
        if (inp.is_key_down(lumen::platform::Key::W))
            orbitPitch = glm::clamp(orbitPitch + kOrbitSpeed * dt, 0.1f, 1.4f);
        if (inp.is_key_down(lumen::platform::Key::S))
            orbitPitch = glm::clamp(orbitPitch - kOrbitSpeed * dt, 0.1f, 1.4f);

        modelRotation += dt * 0.8f;

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX)
            continue;

        // MVP 矩阵
        glm::vec3 cameraPos =
            orbitRadius *
            glm::vec3(std::sin(orbitYaw) * std::cos(orbitPitch),
                      std::sin(orbitPitch),
                      std::cos(orbitYaw) * std::cos(orbitPitch));
        glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(
            glm::radians(60.0f),
            static_cast<float>(swapchain.extent().width) /
                static_cast<float>(swapchain.extent().height),
            0.1f, 100.0f);
        proj[1][1] *= -1.0f;  // Vulkan NDC Y 向下，需配合 frontFace=CW
        glm::mat4 model =
            glm::rotate(glm::mat4(1.0f), modelRotation,
                        glm::vec3(0.4f, 0.6f, 0.2f));
        UBO ubo {};
        ubo.mvp = proj * view * model;
        uniformBuffers[currentFrame].update(ubo);

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
        rpBegin.renderArea = { { 0, 0 }, swapchain.extent() };
        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.1f, 0.12f, 0.18f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmdBuffers[currentFrame], &rpBegin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuffers[currentFrame], 0, 1, &viewport);

        VkRect2D scissor { { 0, 0 }, swapchain.extent() };
        vkCmdSetScissor(cmdBuffers[currentFrame], 0, 1, &scissor);

        vkCmdBindPipeline(cmdBuffers[currentFrame],
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        vkCmdBindDescriptorSets(cmdBuffers[currentFrame],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmdBuffers[currentFrame], 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmdBuffers[currentFrame], indexBuffer.handle(), 0,
                             indexBuffer.vk_index_type());
        vkCmdDrawIndexed(cmdBuffers[currentFrame], 36, 1, 0, 0, 0);

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
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
            needRecreateSwapchain = true;

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    LUMEN_APP_LOG_INFO("Demo3D 退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init())
        return -1;
    int result = run_demo3d();
    lumen::core::Logger::shutdown();
    return result;
}
