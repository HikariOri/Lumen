/**
 * @file main.cpp
 * @brief 立方体渲染到离屏目标，经 ImGui 可停靠视口显示；交换链仅用于 UI
 */

#include "engine.hpp"

#include "core/log/logger.hpp"
#include "core/path.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

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

/// 与 `cube3d_imgui.vert` 中 `mat4 mvp` 一致（std140）
struct TransformUbo {
    glm::mat4 mvp { 1.0F };
};

constexpr const char *kTextureRelPath { "assets/textures/testTexture.png" };

/// 角速度（弧度/秒），绕世界 +Y
constexpr float kSpinRadPerSecond { 0.8F };

} // namespace

static int run_cube3d_imgui() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen — 3D Cube + ImGui";
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

    // 交换链：仅颜色附件，供 ImGui 合成（无深度）
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

    lumen::render::RenderPassConfig offscreenRpCfg;
    offscreenRpCfg.useDepth = true;
    offscreenRpCfg.colorAttachment.format = swapchain.image_format();
    offscreenRpCfg.colorAttachment.finalLayout =
        vk::ImageLayout::eShaderReadOnlyOptimal;
    lumen::render::RenderPass offscreenRenderPass;
    if (!offscreenRenderPass.create(ctx.device(), offscreenRpCfg)) {
        LUMEN_APP_LOG_ERROR("离屏 RenderPass 创建失败");
        return -1;
    }

    lumen::render::OffscreenRenderTarget sceneTarget;
    {
        lumen::render::OffscreenRenderTargetConfig sceneCfg;
        sceneCfg.width =
            static_cast<uint32_t>(std::max(2, window_width * 3 / 4));
        sceneCfg.height =
            static_cast<uint32_t>(std::max(2, window_height * 3 / 4));
        sceneCfg.format = swapchain.image_format();
        sceneCfg.useDepth = true;
        sceneCfg.colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (!sceneTarget.create(ctx, sceneCfg, &offscreenRenderPass)) {
            LUMEN_APP_LOG_ERROR("场景离屏渲染目标创建失败");
            return -1;
        }
    }

    const std::string vertPath =
        lumen::core::get_resource_path("shaders/cube3d_imgui.vert.spv");
    const std::string fragPath =
        lumen::core::get_resource_path("shaders/cube3d_imgui.frag.spv");

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
    if (!vertexBuffer.create_device_local_and_upload(ctx, ctx.graphics_queue(),
                                                     cmdPool, vertices.data(),
                                                     sizeof(vertices))) {
        LUMEN_APP_LOG_ERROR("VertexBuffer 创建失败");
        return -1;
    }

    lumen::render::IndexBuffer indexBuffer;
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint16);
    if (!indexBuffer.create_device_local_and_upload(ctx, ctx.graphics_queue(),
                                                    cmdPool, indices.data(),
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
    if (!pipeline.create(ctx, pipelineLayout, sceneTarget.render_pass_ref(), 0,
                         pipeConfig)) {
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

    lumen::ui::ImGuiBackendInitInfo imguiInfo;
    imguiInfo.ctx = &ctx;
    imguiInfo.swapchain = &swapchain;
    imguiInfo.renderPass = renderPass.handle();
    imguiInfo.window = window.sdl_window();
    static std::string imgui_font_sc_path;
    static std::string imgui_font_jp_path;
    imgui_font_sc_path = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/SimplifiedChinese/"
        "SourceHanSansSC-Bold.otf");
    imgui_font_jp_path = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/Japanese/SourceHanSans-Bold.otf");
    imguiInfo.cjk_font_ttf_path = imgui_font_sc_path.c_str();
    imguiInfo.cjk_font_japanese_merge_path = imgui_font_jp_path.c_str();
    if (!lumen::ui::imgui_backend_init(imguiInfo)) {
        LUMEN_APP_LOG_ERROR("ImGui 初始化失败");
        return -1;
    }

    lumen::render::Sampler sceneSampler;
    if (!sceneSampler.create(ctx)) {
        LUMEN_APP_LOG_ERROR("场景采样器创建失败");
        return -1;
    }

    ImTextureID scene_tex_id =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), sceneTarget.color_view(),
            static_cast<VkImageLayout>(sceneTarget.color_sample_layout())));

    LUMEN_APP_LOG_INFO(
        "按 Esc 退出；ImGui 已启用 Docking，可将「视口」停靠到任意边");

    lumen::platform::EventPump pump;
    uint32_t currentFrame { 0 };
    bool running { true };
    int fbWidth { window_width };
    int fbHeight { window_height };
    bool needRecreateSwapchain { false };

    lumen::ui::ImGuiLayer imgui_layer;
    imgui_layer.attach(pump);
    pump.set_on_application_event([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventQuit>(
            [&](lumen::platform::EventQuit &) {
                running = false;
                return true;
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

    float spin_rad_per_sec { kSpinRadPerSecond };

    /// 上一帧「视口」窗口期望的离屏尺寸；在 acquire 之前应用，避免与
    /// ImGui::Image 同帧换纹理
    uint32_t pending_viewport_w { sceneTarget.width() };
    uint32_t pending_viewport_h { sceneTarget.height() };

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    while (running) {
        if (!pump.poll()) {
            break;
        }

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            if (fbWidth > 0 && fbHeight > 0) {
                lumen::render::recreate_swapchain_resources(
                    ctx, swapchain, framebuffers, frameSync, renderPass,
                    static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), kMaxFramesInFlight,
                    vk::ImageView {});
                lumen::ui::imgui_backend_set_min_image_count(
                    swapchain.image_count());
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

        if (pending_viewport_w >= 2U && pending_viewport_h >= 2U &&
            (pending_viewport_w != sceneTarget.width() ||
             pending_viewport_h != sceneTarget.height())) {
            ctx.wait_idle();
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(scene_tex_id));
            scene_tex_id = static_cast<ImTextureID>(0);
            if (!sceneTarget.resize(pending_viewport_w, pending_viewport_h)) {
                LUMEN_APP_LOG_ERROR("场景离屏目标 resize 失败");
                running = false;
                break;
            }
            scene_tex_id = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(), sceneTarget.color_view(),
                    static_cast<VkImageLayout>(
                        sceneTarget.color_sample_layout())));
        }

        const uint32_t scene_w = sceneTarget.width();
        const uint32_t scene_h = sceneTarget.height();

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), {},
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        imgui_layer.begin_frame();

        if (ImGui::Begin("视口")) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            pending_viewport_w =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, avail.x)));
            pending_viewport_h =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, avail.y)));
            ImGui::Image(scene_tex_id, ImVec2(static_cast<float>(scene_w),
                                              static_cast<float>(scene_h)));
        }
        ImGui::End();

        if (ImGui::Begin("参数")) {
            ImGui::SliderFloat("角速度（弧度/秒）", &spin_rad_per_sec, 0.0F,
                               3.0F, "%.2f");
            ImGui::Text("Esc：退出");
        }
        ImGui::End();

        auto &cmdBuf = cmdBuffers[currentFrame];
        cmdBuf.reset({});

        const float seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
        const float aspect = static_cast<float>(scene_w) /
                             std::max(1.0F, static_cast<float>(scene_h));
        glm::mat4 model =
            glm::rotate(glm::mat4(1.0F), seconds * spin_rad_per_sec,
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

        // 1) 离屏：立方体 + 深度
        std::array<vk::ClearValue, 2> sceneClears {};
        sceneClears[0].color.float32[0] = 0.05F;
        sceneClears[0].color.float32[1] = 0.06F;
        sceneClears[0].color.float32[2] = 0.09F;
        sceneClears[0].color.float32[3] = 1.0F;
        sceneClears[1].depthStencil.depth = 1.0F;
        sceneClears[1].depthStencil.stencil = 0;

        vk::RenderPassBeginInfo sceneRp {};
        sceneRp.renderPass = sceneTarget.render_pass();
        sceneRp.framebuffer = sceneTarget.framebuffer();
        sceneRp.renderArea.offset = vk::Offset2D { 0, 0 };
        sceneRp.renderArea.extent = sceneTarget.extent();
        sceneRp.clearValueCount =
            static_cast<uint32_t>(sceneClears.size());
        sceneRp.pClearValues = sceneClears.data();

        cmdBuf.beginRenderPass(sceneRp, vk::SubpassContents::eInline);

        const vk::Viewport sceneVp {
            0.0F, 0.0F,
            static_cast<float>(sceneTarget.extent().width),
            static_cast<float>(sceneTarget.extent().height),
            0.0F, 1.0F};
        cmdBuf.setViewport(0, { sceneVp });

        const vk::Rect2D sceneSc { vk::Offset2D { 0, 0 },
                                   sceneTarget.extent() };
        cmdBuf.setScissor(0, { sceneSc });

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

        // 2) 交换链：仅 ImGui
        std::array<vk::ClearValue, 1> swapClears {};
        swapClears[0].color.float32[0] = 0.07F;
        swapClears[0].color.float32[1] = 0.08F;
        swapClears[0].color.float32[2] = 0.11F;
        swapClears[0].color.float32[3] = 1.0F;

        vk::RenderPassBeginInfo rpBegin {};
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = vk::Offset2D { 0, 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount =
            static_cast<uint32_t>(swapClears.size());
        rpBegin.pClearValues = swapClears.data();

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

        imgui_layer.end_frame(cmdBuf);

        cmdBuf.endRenderPass();

        if (cmdBuf.end() != vk::Result::eSuccess) {
            LUMEN_LOG_ERROR("CommandBuffer::end 失败");
            continue;
        }

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
    if (scene_tex_id != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(scene_tex_id));
    }
    lumen::ui::imgui_backend_shutdown();
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int result = run_cube3d_imgui();
    lumen::core::Logger::shutdown();
    return result;
}
