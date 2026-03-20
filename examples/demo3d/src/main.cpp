/**
 * @file main.cpp
 * @brief Demo3D：进入 3D 世界 - 透视、深度缓冲、OBJ 模型加载
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "ui/imgui_backend.hpp"
#include "core/obj_loader.hpp"
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
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"

#include <array>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Vertex = lumen::core::ObjVertex;

/// UBO：mat4 mvp + mat3 normalMatrix + 4 光源 (std140 对齐)
struct UBO {
    glm::mat4 mvp;
    glm::mat3 normalMatrix;
    glm::vec4 light0;  // xyz=方向 w=强度
    glm::vec4 light1;
    glm::vec4 light2;
    glm::vec4 light3;
};

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };
constexpr const char* kObjPath { "assets/meshes/monkey/monkey.obj" };

} // namespace

static int run_demo3d() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Demo3D - OBJ 模型";
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

    // RenderPass：主屏（呈现）
    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = true;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        return -1;
    }

    // 离屏 RenderPass：3D 场景渲染到纹理（finalLayout=SHADER_READ）
    lumen::render::RenderPassConfig sceneRpConfig;
    sceneRpConfig.useDepth = true;
    sceneRpConfig.colorAttachment.format = swapchain.image_format();
    sceneRpConfig.colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::RenderPass sceneRenderPass;
    if (!sceneRenderPass.create(ctx.device(), sceneRpConfig)) {
        return -1;
    }

    // 离屏：场景颜色 + 深度
    lumen::render::ImageCreateInfo sceneColorInfo;
    sceneColorInfo.width = static_cast<uint32_t>(w);
    sceneColorInfo.height = static_cast<uint32_t>(h);
    sceneColorInfo.format = swapchain.image_format();
    sceneColorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT;
    lumen::render::Image sceneColorImage;
    if (!sceneColorImage.create(ctx, sceneColorInfo)) {
        LUMEN_APP_LOG_ERROR("场景颜色附件创建失败");
        return -1;
    }
    lumen::render::Image sceneDepthImage;
    if (!sceneDepthImage.create_depth_attachment(ctx, static_cast<uint32_t>(w),
                                                 static_cast<uint32_t>(h))) {
        return -1;
    }

    std::vector<VkImageView> sceneAttachments { sceneColorImage.view(),
                                                sceneDepthImage.view() };
    lumen::render::Framebuffer sceneFramebuffer;
    if (!sceneFramebuffer.create_offscreen(
            ctx.device(), sceneRenderPass.handle(), static_cast<uint32_t>(w),
            static_cast<uint32_t>(h), sceneAttachments)) {
        return -1;
    }

    lumen::render::Sampler sceneSampler;
    if (!sceneSampler.create(ctx)) {
        return -1;
    }

    // 深度附件（主屏）
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

    // OBJ 模型加载
    lumen::core::ObjMesh mesh;
    std::string objPath = lumen::core::get_resource_path(kObjPath);
    if (!lumen::core::load_obj(objPath, mesh)) {
        LUMEN_APP_LOG_ERROR("OBJ 加载失败: {}", objPath);
        return -1;
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        LUMEN_APP_LOG_ERROR("OBJ 模型为空: {}", objPath);
        return -1;
    }

    // 顶点 / 索引缓冲
    lumen::render::VertexBuffer vertexBuffer;
    lumen::render::IndexBuffer indexBuffer;
    const size_t vertexBytes = mesh.vertices.size() * sizeof(Vertex);
    const size_t indexBytes = mesh.indices.size() * sizeof(uint32_t);
    if (!vertexBuffer.create(ctx, vertexBytes) ||
        !indexBuffer.create(ctx, indexBytes)) {
        return -1;
    }
    vertexBuffer.upload(mesh.vertices.data(), vertexBytes);
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    indexBuffer.upload(mesh.indices.data(), indexBytes);

    const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());

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
                         VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT },
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
            ctx.device(), descriptorSets[i], 1, texture.view(), texture.sampler());
    }

    lumen::render::PipelineLayout pipelineLayout;
    VkPushConstantRange pushRange {};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(uint32_t);
    pipelineLayout.create(ctx, { descLayout.handle() }, { pushRange });

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
    pipeConfig.vertexAttributes.push_back(
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    pipeConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    pipeConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // Blender OBJ 外表面为 CCW

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(),
                         sceneRenderPass.handle(), 0, pipeConfig)) {
        return -1;
    }

    lumen::render::GraphicsPipelineConfig wireframeConfig = pipeConfig;
    wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;
    wireframeConfig.cullMode = VK_CULL_MODE_NONE;
    lumen::render::GraphicsPipeline wireframePipeline;
    if (!wireframePipeline.create(ctx, pipelineLayout.handle(),
                                  sceneRenderPass.handle(), 0,
                                  wireframeConfig)) {
        return -1;
    }

    auto cmdBuffers = cmdPool.allocate(kMaxFramesInFlight);
    lumen::render::FrameSync frameSync;
    frameSync.create(ctx.device(), swapchain.image_count(), kMaxFramesInFlight);

    lumen::platform::EventPump pump;
    uint32_t renderMode { 0 };
    float orbitYaw { 0.0f };
    float orbitPitch { 0.3f };
    float orbitRadius { 2.5f };
    float modelYaw { 0.0f };
    float modelPitch { 0.0f };
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };
    uint32_t currentFrame { 0 };
    bool running { true };
    double lastTime = lumen::core::get_time_seconds();

    // ImGui 后端
    lumen::ui::ImGuiBackendInitInfo imguiInfo;
    imguiInfo.ctx = &ctx;
    imguiInfo.swapchain = &swapchain;
    imguiInfo.renderPass = renderPass.handle();
    imguiInfo.window = window.sdl_window();
    if (!lumen::ui::imgui_backend_init(imguiInfo)) {
        LUMEN_APP_LOG_ERROR("ImGui 初始化失败");
        return -1;
    }

    ImTextureID sceneTextureId = reinterpret_cast<ImTextureID>(
        lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), sceneColorImage.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    pump.on_sdl_event([](const void* ev) {
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(ev));
    });

    LUMEN_APP_LOG_INFO(
        "Demo3D 启动 [WASD/右键拖拽] 相机 [左键拖拽] 模型 [滚轮] 缩放 "
        "[0] 光照 [1] 线框 [2] 法线 [3] 深度 [ESC] 退出");

    constexpr float kMouseSensitivity { 0.007f };
    constexpr float kZoomSpeed { 0.25f };
    constexpr float kMinOrbitRadius { 0.8f };
    constexpr float kMaxOrbitRadius { 20.0f };

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape) {
            running = false;
        } else if (e.key == lumen::platform::Key::Num0) {
            renderMode = 0;
        } else if (e.key == lumen::platform::Key::Num1) {
            renderMode = 1;
        } else if (e.key == lumen::platform::Key::Num2) {
            renderMode = 2;
        } else if (e.key == lumen::platform::Key::Num3) {
            renderMode = 3;
        }
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
    });
    // 拖拽时启用相对鼠标模式（ImGui 未占用时）
    pump.on_mouse_button_down([&](const lumen::platform::EventMouseButtonDown &e) {
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        if (e.button == lumen::platform::MouseButton::Left ||
            e.button == lumen::platform::MouseButton::Right) {
            SDL_SetWindowRelativeMouseMode(window.sdl_window(), true);
        }
    });
    pump.on_mouse_button_up([&](const lumen::platform::EventMouseButtonUp &e) {
        if (e.button == lumen::platform::MouseButton::Left ||
            e.button == lumen::platform::MouseButton::Right) {
            const auto &inp = pump.input();
            bool otherDown =
                (e.button == lumen::platform::MouseButton::Left
                     ? inp.is_mouse_button_down(lumen::platform::MouseButton::Right)
                     : inp.is_mouse_button_down(lumen::platform::MouseButton::Left));
            if (!otherDown) {
                SDL_SetWindowRelativeMouseMode(window.sdl_window(), false);
            }
        }
    });
    pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel &e) {
        if (ImGui::GetIO().WantCaptureMouse)
            return;
        orbitRadius =
            glm::clamp(orbitRadius - e.deltaY * kZoomSpeed, kMinOrbitRadius,
                       kMaxOrbitRadius);
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

                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void*>(sceneTextureId));

                lumen::render::ImageCreateInfo sceneColorInfo;
                sceneColorInfo.width = static_cast<uint32_t>(fbWidth);
                sceneColorInfo.height = static_cast<uint32_t>(fbHeight);
                sceneColorInfo.format = swapchain.image_format();
                sceneColorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT;
                sceneColorImage = lumen::render::Image();
                sceneColorImage.create(ctx, sceneColorInfo);
                sceneDepthImage = lumen::render::Image();
                sceneDepthImage.create_depth_attachment(
                    ctx, static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight));
                std::vector<VkImageView> newSceneAttachments {
                    sceneColorImage.view(), sceneDepthImage.view()
                };
                sceneFramebuffer = lumen::render::Framebuffer();
                sceneFramebuffer.create_offscreen(
                    ctx.device(), sceneRenderPass.handle(),
                    static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), newSceneAttachments);
                sceneTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), sceneColorImage.view(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
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

        lumen::ui::imgui_backend_new_frame();

        const auto &inp = pump.input();
        const bool imguiWantsInput =
            ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
        if (!imguiWantsInput) {
            if (inp.is_key_down(lumen::platform::Key::A))
                orbitYaw += kOrbitSpeed * dt;
            if (inp.is_key_down(lumen::platform::Key::D))
                orbitYaw -= kOrbitSpeed * dt;
            if (inp.is_key_down(lumen::platform::Key::W))
                orbitPitch =
                    glm::clamp(orbitPitch + kOrbitSpeed * dt, 0.1f, 1.4f);
            if (inp.is_key_down(lumen::platform::Key::S))
                orbitPitch =
                    glm::clamp(orbitPitch - kOrbitSpeed * dt, 0.1f, 1.4f);
        }

        // 左键拖拽旋转模型，右键拖拽旋转相机（ImGui 未占用时）
        if (!imguiWantsInput &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Right)) {
            modelYaw -= inp.mouse_delta_x() * kMouseSensitivity;
            modelPitch += inp.mouse_delta_y() * kMouseSensitivity;
            modelPitch = glm::clamp(modelPitch, -1.5f, 1.5f);
        }
        if (!imguiWantsInput &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Left)) {
            orbitYaw -= inp.mouse_delta_x() * kMouseSensitivity;
            orbitPitch =
                glm::clamp(orbitPitch + inp.mouse_delta_y() * kMouseSensitivity,
                           0.1f, 1.4f);
        }

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
            glm::radians(42.0f),
            static_cast<float>(fbWidth) / static_cast<float>(fbHeight), 0.1f,
            100.0f);
        proj[1][1] *= -1.0f;  // Vulkan NDC Y 向下；frontFace 与模型绕序匹配
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, modelYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, modelPitch, glm::vec3(1.0f, 0.0f, 0.0f));
        UBO ubo {};
        ubo.mvp = proj * view * model;
        ubo.normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));
        // 多光源：方向为从表面指向光源，Blender 猴头正面朝 -Z
        ubo.light0 = glm::vec4(0.0f, 0.5f, -1.0f, 1.2f);   // 主光：正前方偏上，强
        ubo.light1 = glm::vec4(-0.6f, 0.5f, -0.6f, 0.7f);  // 填充：左前方
        ubo.light2 = glm::vec4(0.5f, 0.3f, -0.8f, 0.6f);   // 右前方补光
        ubo.light3 = glm::vec4(0.0f, -0.5f, -0.9f, 0.5f);  // 底光：照下巴
        uniformBuffers[currentFrame].update(ubo);

        VkCommandBuffer cmd = cmdBuffers[currentFrame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            continue;
        }

        const VkExtent2D sceneExtent {
            static_cast<uint32_t>(fbWidth),
            static_cast<uint32_t>(fbHeight)
        };

        // Pass 1: render 3D to offscreen
        VkRenderPassBeginInfo sceneRpBegin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        sceneRpBegin.renderPass = sceneRenderPass.handle();
        sceneRpBegin.framebuffer = sceneFramebuffer.get(0);
        sceneRpBegin.renderArea = { { 0, 0 }, sceneExtent };
        VkClearValue sceneClearValues[2];
        sceneClearValues[0].color = { { 0.1f, 0.12f, 0.18f, 1.0f } };
        sceneClearValues[1].depthStencil = { 1.0f, 0 };
        sceneRpBegin.clearValueCount = 2;
        sceneRpBegin.pClearValues = sceneClearValues;
        vkCmdBeginRenderPass(cmd, &sceneRpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport sceneViewport {};
        sceneViewport.width = static_cast<float>(sceneExtent.width);
        sceneViewport.height = static_cast<float>(sceneExtent.height);
        sceneViewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &sceneViewport);
        VkRect2D sceneScissor { { 0, 0 }, sceneExtent };
        vkCmdSetScissor(cmd, 0, 1, &sceneScissor);

        VkPipeline activePipeline =
            (renderMode == 1u) ? wireframePipeline.handle() : pipeline.handle();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);
        vkCmdPushConstants(cmd, pipelineLayout.handle(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(renderMode),
                           &renderMode);
        VkDescriptorSet descSet = descriptorSets[currentFrame];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 1, &descSet, 0,
                                nullptr);
        VkBuffer vb = vertexBuffer.handle();
        VkDeviceSize vbOffset { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0,
                             indexBuffer.vk_index_type());
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);

        // Pass 2: render to swapchain (clear + ImGui)
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
        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor { { 0, 0 }, swapchain.extent() };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // ImGui (docking enabled)
        ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
        ImGui::Begin("Scene");
        ImGui::Image(sceneTextureId,
                    ImVec2(static_cast<float>(sceneExtent.width),
                           static_cast<float>(sceneExtent.height)));
        ImGui::End();

        ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Demo3D");
        ImGui::Text("Render: [0]Lit [1]Wireframe [2]Normal [3]Depth");
        ImGui::Separator();
        ImGui::SliderFloat("Camera Distance", &orbitRadius, kMinOrbitRadius,
                           kMaxOrbitRadius, "%.1f");
        ImGui::SliderFloat("Model Yaw", &modelYaw, -3.14f, 3.14f, "%.2f");
        ImGui::SliderFloat("Model Pitch", &modelPitch, -1.5f, 1.5f, "%.2f");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FPS: %.1f",
                          1.0f / (dt > 0.0f ? dt : 0.016f));
        ImGui::End();

        lumen::ui::imgui_backend_render(cmd);

        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
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
        submitInfo.pCommandBuffers = &cmd;
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
    lumen::ui::imgui_backend_shutdown();
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
