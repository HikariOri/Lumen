/**
 * @file main.cpp
 * @brief 索引矩形 + `assets/textures/testTexture.png`（UV 与图角标 0,0 / 1,1
 * 对齐）
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
#include "render/resource/descriptor.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t kMaxFramesInFlight { 3 };

/// 与 testTexture.png 一致：图像左上 (0,0)、右下 (1,1)；屏幕 Y 向上时左上顶点用
/// (0,0)
struct Vertex {
    glm::vec2 position;
    glm::vec2 uv;
};

constexpr const char *kTextureRelPath { "assets/textures/testTexture.png" };

} // namespace

static int run_rectangle_textured() {
    // Create window
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;

    winConfig.title = "Lumen — 索引矩形 + testTexture";
    winConfig.width = 800;
    winConfig.height = 600;
    winConfig.icon_path =
        lumen::core::get_resource_path("assets/icons/哈士奇.png");

    if (!window.create(winConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    // Create Vulkan context
    lumen::render::ContextConfig ctxConfig;
    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig, window)) {
        LUMEN_APP_LOG_ERROR("Vulkan Instance 创建失败");
        return -1;
    }

    // Create Vulkan surface
    lumen::render::Surface surface(ctx, window);
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Vulkan Surface 创建失败");
        return -1;
    }

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 创建失败");
        return -1;
    }

    // Get window size
    int window_width { 0 };
    int window_height { 0 };
    window.get_framebuffer_size(&window_width, &window_height);

    // Create Swapchain
    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(),
                          static_cast<uint32_t>(window_width),
                          static_cast<uint32_t>(window_height))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    // Create RenderPass
    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = false;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    // Create framebuffer
    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             VK_NULL_HANDLE)) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    // Create shaders
    const std::string vertPath = lumen::core::get_resource_path(
        "shaders/rectangle_indexed_texture.vert.spv");
    const std::string fragPath = lumen::core::get_resource_path(
        "shaders/rectangle_indexed_texture.frag.spv");

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

    // 左下、右下、右上、左上（与索引 0,1,2 / 0,2,3 一致）
    const std::array<Vertex, 4> vertices { {
        { .position = { -0.5F, -0.5F }, .uv = { 0.0F, 1.0F } },
        { .position = { 0.5F, -0.5F }, .uv = { 1.0F, 1.0F } },
        { .position = { 0.5F, 0.5F }, .uv = { 1.0F, 0.0F } },
        { .position = { -0.5F, 0.5F }, .uv = { 0.0F, 0.0F } },
    } };

    const std::array<uint16_t, 6> indices { { 0, 1, 2, 0, 2, 3 } };

    // Create command pool
    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    // Create texture
    lumen::render::Texture texture;
    const std::string texPath = lumen::core::get_resource_path(kTextureRelPath);
    if (!texture.create_from_file(ctx, texPath.c_str(), ctx.graphics_queue(),
                                  cmdPool)) {
        LUMEN_APP_LOG_ERROR("纹理加载失败: {}", texPath);
        return -1;
    }
    LUMEN_APP_LOG_INFO("纹理: {}", texPath);

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

    // Create descriptor set layout
    lumen::render::DescriptorSetLayout descLayout;
    const std::vector<lumen::render::DescriptorBinding> descBindings = {
        { .binding = 0,
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
                         { { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             .count = 1 } },
                         1)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
    }

    VkDescriptorSet descriptorSet { VK_NULL_HANDLE };
    if (!descPool.allocate(ctx.device(), descLayout.handle(), descriptorSet)) {
        LUMEN_APP_LOG_ERROR("DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::write_descriptor_image(ctx.device(), descriptorSet, 0,
                                          texture.view(), texture.sampler(),
                                          texture.descriptor_layout());

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
          .kind = lumen::render::VertexAttributeKind::F32Vec2,
          .offset = offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .kind = lumen::render::VertexAttributeKind::F32Vec2,
          .offset = offsetof(Vertex, uv) });
    pipeConfig.depthTest = false;
    pipeConfig.depthWrite = false;
    pipeConfig.cullMode = VK_CULL_MODE_NONE;

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
            lumen::render::recreate_swapchain_resources(
                ctx, swapchain, framebuffers, frameSync, renderPass,
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
        if (!running) {
            break;
        }

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        vkResetCommandBuffer(cmdBuffers[currentFrame], 0);

        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmdBuffers[currentFrame], &beginInfo) !=
            VK_SUCCESS) {
            continue;
        }

        VkRenderPassBeginInfo rpBegin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = { .x = 0, .y = 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        VkClearValue clearColor {};
        clearColor.color = { { 0.06F, 0.07F, 0.10F, 1.0F } };
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmdBuffers[currentFrame], &rpBegin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(cmdBuffers[currentFrame], 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { .x = 0, .y = 0 };
        scissor.extent = swapchain.extent();
        vkCmdSetScissor(cmdBuffers[currentFrame], 0, 1, &scissor);

        vkCmdBindPipeline(cmdBuffers[currentFrame],
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        vkCmdBindDescriptorSets(
            cmdBuffers[currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout.handle(), 0, 1, &descriptorSet, 0, nullptr);

        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmdBuffers[currentFrame], 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmdBuffers[currentFrame], indexBuffer.handle(), 0,
                             indexBuffer.vk_index_type());
        vkCmdDrawIndexed(cmdBuffers[currentFrame],
                         static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        vkCmdEndRenderPass(cmdBuffers[currentFrame]);

        if (vkEndCommandBuffer(cmdBuffers[currentFrame]) != VK_SUCCESS) {
            continue;
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
    const int result = run_rectangle_textured();
    lumen::core::Logger::shutdown();
    return result;
}
