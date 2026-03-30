/**
 * @file main.cpp
 * @brief 最小 3D 示例：透视、深度缓冲、带 UV 的索引立方体贴图，MVP 经 UBO
 * 更新并旋转
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
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr uint32_t kMaxFramesInFlight { 3 };

struct Vertex {
    glm::vec3 position;
    glm::vec2 uv;
};

/// 与 `cube3d.vert` 中 `mat4 mvp` 一致（std140）
struct TransformUbo {
    glm::mat4 mvp { 1.0F };
};

constexpr const char *kTextureRelPath { "assets/textures/testTexture.png" };

/// 角速度（弧度/秒），绕世界 +Y
constexpr float kSpinRadPerSecond { 0.8F };

} // namespace

static int run_cube3d() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen — 3D Cube";
    winConfig.width = 960;
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
    rpConfig.useDepth = true;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    lumen::render::Image depthImage;
    if (!depthImage.create_depth_attachment(
            ctx, static_cast<uint32_t>(window_width),
            static_cast<uint32_t>(window_height))) {
        LUMEN_APP_LOG_ERROR("深度附件创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             depthImage.view())) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    const std::string vertPath =
        lumen::core::get_resource_path("shaders/cube3d.vert.spv");
    const std::string fragPath =
        lumen::core::get_resource_path("shaders/cube3d.frag.spv");

    lumen::render::ShaderModule vertShader;
    lumen::render::ShaderModule fragShader;
    if (!vertShader.create_from_file(ctx.device(), vertPath.c_str()) ||
        !fragShader.create_from_file(ctx.device(), fragPath.c_str())) {
        LUMEN_APP_LOG_ERROR("着色器加载失败 vert={} frag={}", vertPath,
                            fragPath);
        return -1;
    }

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    lumen::render::Texture texture;
    const std::string texPath = lumen::core::get_resource_path(kTextureRelPath);
    if (!texture.create_from_file(ctx, texPath.c_str(), ctx.graphics_queue(),
                                  cmdPool)) {
        LUMEN_APP_LOG_ERROR("纹理加载失败: {}", texPath);
        return -1;
    }

    // 单位立方体 [-0.5, 0.5]^3：每面 4 顶点独立 UV（与 testTexture
    // 角标一致），共 24 顶点
    const std::array<Vertex, 24> vertices { {
        { .position = { -0.5F, -0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, -0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, -0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, 0.5F, -0.5F }, .uv = { 0.0F, 0.0F } },
        { .position = { -0.5F, -0.5F, 0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, -0.5F, 0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, 0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
        { .position = { -0.5F, -0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, -0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, -0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, -0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
        { .position = { -0.5F, 0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, 0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
        { .position = { -0.5F, -0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { -0.5F, 0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { -0.5F, 0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, -0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
        { .position = { 0.5F, -0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, 0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { 0.5F, -0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
    } };

    // 每三角形 (v0,v1,v2) 满足 cross(p1-p0, p2-p0)
    // 与面外法线同向（右手系、从外面看为 CCW）； 配合 `proj[1][1] *= -1` 使用
    // VK_FRONT_FACE_CLOCKWISE + 背面剔除。
    const std::array<uint16_t, 36> indices { {
        0,  2,  1,  0,  3,  2,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 19, 18, 16, 18, 17, 20, 21, 22, 20, 22, 23,
    } };

    lumen::render::VertexBuffer vertexBuffer;
    if (!vertexBuffer.create_device_local_and_upload(
            ctx, ctx.graphics_queue(), cmdPool, vertices.data(),
            sizeof(vertices))) {
        LUMEN_APP_LOG_ERROR("VertexBuffer 创建失败");
        return -1;
    }

    lumen::render::IndexBuffer indexBuffer;
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint16);
    if (!indexBuffer.create_device_local_and_upload(
            ctx, ctx.graphics_queue(), cmdPool, indices.data(),
            sizeof(indices))) {
        LUMEN_APP_LOG_ERROR("IndexBuffer 创建失败");
        return -1;
    }

    lumen::render::DescriptorSetLayout descLayout;
    const std::vector<lumen::render::DescriptorBinding> descBindings = {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .count = 1,
          .stages = VK_SHADER_STAGE_VERTEX_BIT },
        { .binding = 1,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!descLayout.create(ctx, descBindings)) {
        LUMEN_APP_LOG_ERROR("DescriptorSetLayout 创建失败");
        return -1;
    }

    lumen::render::DescriptorPool descPool;
    if (!descPool.create(ctx,
                         { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                             .count = kMaxFramesInFlight },
                           { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             .count = kMaxFramesInFlight } },
                         kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
    }

    constexpr size_t kUboSize { sizeof(TransformUbo) };
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight>
        uniformBuffers {};
    for (auto &ub : uniformBuffers) {
        if (!ub.create_persistent(ctx, kUboSize)) {
            LUMEN_APP_LOG_ERROR("UniformBuffer 创建失败");
            return -1;
        }
    }

    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!descPool.allocate(ctx.device(), descLayout.handle(),
                               descriptorSets[i])) {
            LUMEN_APP_LOG_ERROR("DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_set(
            ctx.device(), descriptorSets[i],
            { { .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .buffer = uniformBuffers[i].handle(),
                .offset = 0,
                .range = kUboSize } },
            { { .binding = 1,
                .imageView = texture.view(),
                .sampler = texture.sampler(),
                .imageLayout = texture.descriptor_layout() } });
    }

    lumen::render::PipelineLayout pipelineLayout;
    if (!pipelineLayout.create(ctx, { descLayout.handle() }, {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.shaderStages.push_back({ .module = vertShader.handle(),
                                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                        .entryPoint = "main" });
    pipeConfig.shaderStages.push_back({ .module = fragShader.handle(),
                                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                        .entryPoint = "main" });
    pipeConfig.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(Vertex),
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    pipeConfig.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec2,
          .offset = offsetof(Vertex, uv) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    // 与 `docs/reference/glm-vulkan.md`：投影 Y 翻转后正面为 CLOCKWISE
    pipeConfig.cullMode = VK_CULL_MODE_NONE;
    pipeConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout, renderPass, 0, pipeConfig)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
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

    LUMEN_APP_LOG_INFO("按 Esc 退出");

    lumen::platform::EventPump pump;
    uint32_t currentFrame { 0 };
    bool running { true };
    int fbWidth { window_width };
    int fbHeight { window_height };
    bool needRecreateSwapchain { false };

    pump.push_layer([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventQuit>([&](lumen::platform::EventQuit &) {
            running = false;
            return false;
        });
        d.dispatch<lumen::platform::EventKeyDown>(
            [&](lumen::platform::EventKeyDown &e) {
                if (e.key == lumen::platform::Key::Escape) {
                    running = false;
                }
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
            if (fbWidth > 0 && fbHeight > 0) {
                lumen::render::Image newDepth;
                if (!newDepth.create_depth_attachment(
                        ctx, static_cast<uint32_t>(fbWidth),
                        static_cast<uint32_t>(fbHeight))) {
                    LUMEN_APP_LOG_ERROR("深度附件重建失败");
                    needRecreateSwapchain = false;
                    continue;
                }
                lumen::render::recreate_swapchain_resources(
                    ctx, swapchain, framebuffers, frameSync, renderPass,
                    static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), kMaxFramesInFlight,
                    newDepth.view());
                depthImage = std::move(newDepth);
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
        if (!running) {
            break;
        }

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        auto &cmdBuf = cmdBuffers[currentFrame];
        if (!cmdBuf.reset()) {
            continue;
        }

        const float seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
        const float aspect =
            static_cast<float>(swapchain.extent().width) /
            std::max(1.0F, static_cast<float>(swapchain.extent().height));
        glm::mat4 model =
            glm::rotate(glm::mat4(1.0F), seconds * kSpinRadPerSecond,
                        glm::vec3(0.0F, 1.0F, 0.0F));
        glm::mat4 view =
            glm::lookAt(glm::vec3(1.35F, 1.0F, 1.35F), glm::vec3(0.0F),
                        glm::vec3(0.0F, 1.0F, 0.0F));
        glm::mat4 proj =
            glm::perspective(glm::radians(55.0F), aspect, 0.1F, 100.0F);
        proj[1][1] *= -1.0F;
        TransformUbo ubo { .mvp = proj * view * model };
        uniformBuffers[currentFrame].update(ubo);

        if (!cmdBuf.begin()) {
            continue;
        }

        VkClearValue clearValues[2] {};
        clearValues[0].color = { { 0.07F, 0.08F, 0.11F, 1.0F } };
        clearValues[1].depthStencil = { 1.0F, 0 };

        VkRenderPassBeginInfo rpBegin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = { .x = 0, .y = 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmdBuf, &rpBegin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { .x = 0, .y = 0 };
        scissor.extent = swapchain.extent();
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        vkCmdBindPipeline(cmdBuf,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        vkCmdBindDescriptorSets(cmdBuf,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmdBuf, indexBuffer.handle(), 0,
                             indexBuffer.vk_index_type());
        vkCmdDrawIndexed(cmdBuf,
                         static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        vkCmdEndRenderPass(cmdBuf);

        if (!cmdBuf.end()) {
            continue;
        }

        VkSemaphore waitSem = frameSync.image_available(currentFrame);
        VkSemaphore signalSem = frameSync.render_finished(imageIndex);

        VkCommandBuffer submit_buf = cmdBuf.handle();
        VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSem;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &submit_buf;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSem;

        if (vkQueueSubmit(ctx.graphics_queue(), 1, &submitInfo,
                          frameSync.in_flight_fence(currentFrame)) !=
            VK_SUCCESS) {
            continue;
        }

        const VkResult presentResult =
            swapchain.present(ctx.present_queue(), imageIndex,
                              frameSync.render_finished(imageIndex));
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
            needRecreateSwapchain = true;
        } else if (presentResult != VK_SUCCESS &&
                   presentResult != VK_SUBOPTIMAL_KHR) {
            LUMEN_APP_LOG_WARN("Present 失败 result={}",
                               static_cast<int>(presentResult));
        }

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int result = run_cube3d();
    lumen::core::Logger::shutdown();
    return result;
}
