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
          .type = vk::DescriptorType::eUniformBuffer,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eVertex },
        { .binding = 1,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
    };
    if (!descLayout.create(ctx, descBindings)) {
        LUMEN_APP_LOG_ERROR("DescriptorSetLayout 创建失败");
        return -1;
    }

    lumen::render::DescriptorPool descPool;
    if (!descPool.create(ctx,
                         { { .type = vk::DescriptorType::eUniformBuffer,
                             .count = kMaxFramesInFlight },
                           { .type = vk::DescriptorType::eCombinedImageSampler,
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

    std::array<vk::DescriptorSet, kMaxFramesInFlight> descriptorSets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!descPool.allocate(ctx.device(), descLayout.handle(),
                               descriptorSets[i])) {
            LUMEN_APP_LOG_ERROR("DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_set(
            ctx.device(), descriptorSets[i],
            { { .binding = 0,
                .type = vk::DescriptorType::eUniformBuffer,
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
          .format = vk::Format::eR32G32B32Sfloat,
          .offset = offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = vk::Format::eR32G32Sfloat,
          .offset = offsetof(Vertex, uv) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    // 与 `docs/reference/glm-vulkan.md`：投影 Y 翻转后正面为 CLOCKWISE
    pipeConfig.cullMode = vk::CullModeFlagBits::eNone;
    pipeConfig.frontFace = vk::FrontFace::eCounterClockwise;

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
            frameSync.image_available(currentFrame), {},
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        auto &cmdBuf = cmdBuffers[currentFrame];
        cmdBuf.reset({});

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

        vk::CommandBufferBeginInfo frameBegin {};
        frameBegin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        if (cmdBuf.begin(&frameBegin) != vk::Result::eSuccess) {
            continue;
        }

        std::array<vk::ClearValue, 2> clearValues {};
        clearValues[0].color.float32[0] = 0.07F;
        clearValues[0].color.float32[1] = 0.08F;
        clearValues[0].color.float32[2] = 0.11F;
        clearValues[0].color.float32[3] = 1.0F;
        clearValues[1].depthStencil.depth = 1.0F;
        clearValues[1].depthStencil.stencil = 0;

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
        const vk::DescriptorSet ds_bind = descriptorSets[currentFrame];
        cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   pipelineLayout.handle(), 0, { ds_bind }, {});

        const vk::Buffer vb = vertexBuffer.handle();
        const vk::DeviceSize vbo { 0 };
        cmdBuf.bindVertexBuffers(0, { vb }, { vbo });
        cmdBuf.bindIndexBuffer(indexBuffer.handle(), vk::DeviceSize { 0 },
                              indexBuffer.vk_index_type());
        cmdBuf.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

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
