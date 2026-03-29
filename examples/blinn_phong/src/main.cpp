/**
 * @file main.cpp
 * @brief 带纹理的单位立方体 + 多光源 Blinn-Phong（定向 / 点 /
 * 聚光）；场景轨道相机； 离屏渲染后经 ImGui 视口显示
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
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"
#include "scene/scene_camera.hpp"
#include "scene/scene_orbit_controller.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"
#include "ui/texture_view_panel.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

constexpr uint32_t kMaxFramesInFlight { 3 };

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

constexpr int kMaxLights { 8 };

/// 与着色器中 `GpuLight` 一致（std140，4×vec4）
struct GpuLight {
    glm::vec4 param0 {};
    glm::vec4 color_intensity {};
    glm::vec4 spot_axis_inv_range {};
    glm::vec4 spot_cos_inner_outer {};
};

/// 与 `blinn_phong.vert` / `.frag` 中 `SceneUBO` 一致（std140）
struct SceneUbo {
    glm::mat4 model { 1.0F };
    glm::mat4 view { 1.0F };
    glm::mat4 proj { 1.0F };
    glm::vec4 cameraWorld { 0.0F };
    /// x=ambient 系数, y=diffuse, z=specular, w=Blinn shininess
    glm::vec4 materialParams { 0.12F, 0.75F, 0.45F, 48.0F };
    glm::ivec4 lightCount_ { 0, 0, 0, 0 };
    std::array<GpuLight, kMaxLights> lights {};
};

static_assert(sizeof(GpuLight) == 64);
static_assert(sizeof(SceneUbo) ==
              (sizeof(glm::mat4) * 3) + (sizeof(glm::vec4) * 2) +
                  sizeof(glm::ivec4) + (sizeof(GpuLight) * kMaxLights));

/// 0 = 定向光, 1 = 点光源, 2 = 聚光灯（与 `param0.w` 对应）
struct EditLight {
    int type { 0 };
    bool enabled { false };
    glm::vec3 position { 0.0F };
    /// 定向/聚光：世界空间中「指向被照亮的方向」（聚光为锥轴）
    glm::vec3 direction { 0.4F, 0.85F, 0.35F };
    glm::vec3 color { 1.0F, 1.0F, 1.0F };
    float intensity { 0.5F };
    float range { 10.0F };
    float spot_outer_deg { 35.0F };
    float spot_inner_deg { 20.0F };
};

void pack_gpu_light(GpuLight &g, const EditLight &e) {
    const glm::vec3 rgb = e.color * e.intensity;
    g.color_intensity = glm::vec4(rgb, e.intensity);
    const float inv_r = (e.range > 1.0e-4F) ? 1.0F / e.range : 0.0F;
    float half_outer_deg = e.spot_outer_deg * 0.5F;
    float half_inner_deg = e.spot_inner_deg * 0.5F;
    if (e.type == 2) {
        half_inner_deg =
            std::min(half_inner_deg, std::max(0.5F, half_outer_deg - 1.0F));
    }
    const float cos_o = std::cos(glm::radians(half_outer_deg));
    const float cos_i = std::cos(glm::radians(half_inner_deg));
    g.spot_cos_inner_outer = glm::vec4(cos_o, cos_i, 0.0F, 0.0F);

    if (e.type == 0) {
        glm::vec3 d = e.direction;
        const float len = glm::length(d);
        if (len > 1.0e-8F) {
            d /= len;
        } else {
            d = glm::vec3(0.0F, 1.0F, 0.0F);
        }
        g.param0 = glm::vec4(d, 0.0F);
        g.spot_axis_inv_range = glm::vec4(0.0F);
    } else if (e.type == 1) {
        g.param0 = glm::vec4(e.position, 1.0F);
        g.spot_axis_inv_range = glm::vec4(0.0F, 0.0F, 0.0F, inv_r);
    } else {
        glm::vec3 axis = e.direction;
        const float alen = glm::length(axis);
        if (alen > 1.0e-8F) {
            axis /= alen;
        } else {
            axis = glm::vec3(0.0F, -1.0F, 0.0F);
        }
        g.param0 = glm::vec4(e.position, 2.0F);
        g.spot_axis_inv_range = glm::vec4(axis, inv_r);
    }
}

constexpr const char *kTextureRelPath { "assets/textures/testTexture.png" };

/// 角速度（弧度/秒），绕世界 +Y
constexpr float kSpinRadPerSecond { 0.8F };

constexpr float kSceneScrollZoomSpeed { 0.25F };

} // namespace

static int run_blinn_phong() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen — 3D Cube + Blinn-Phong";
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
                             VK_NULL_HANDLE)) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig offscreenRpCfg;
    offscreenRpCfg.useDepth = true;
    offscreenRpCfg.colorAttachment.format = swapchain.image_format();
    offscreenRpCfg.colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
        sceneCfg.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!sceneTarget.create(ctx, sceneCfg, &offscreenRenderPass)) {
            LUMEN_APP_LOG_ERROR("场景离屏渲染目标创建失败");
            return -1;
        }
    }

    const std::string vertPath =
        lumen::core::get_resource_path("shaders/blinn_phong.vert.spv");
    const std::string fragPath =
        lumen::core::get_resource_path("shaders/blinn_phong.frag.spv");

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

    // 单位立方体 [-0.5, 0.5]^3：每面 4 顶点独立 UV + 面法线，共 24 顶点
    const glm::vec3 n_b { 0.0F, 0.0F, -1.0F };
    const glm::vec3 n_f { 0.0F, 0.0F, 1.0F };
    const glm::vec3 n_dn { 0.0F, -1.0F, 0.0F };
    const glm::vec3 n_up { 0.0F, 1.0F, 0.0F };
    const glm::vec3 n_l { -1.0F, 0.0F, 0.0F };
    const glm::vec3 n_r { 1.0F, 0.0F, 0.0F };
    const std::array<Vertex, 24> vertices { {
        { { -0.5F, -0.5F, -0.5F }, n_b, { 0.0F, 1.0F } },
        { { 0.5F, -0.5F, -0.5F }, n_b, { 1.0F, 1.0F } },
        { { 0.5F, 0.5F, -0.5F }, n_b, { 1.0F, 0.0F } },
        { { -0.5F, 0.5F, -0.5F }, n_b, { 0.0F, 0.0F } },
        { { -0.5F, -0.5F, 0.5F }, n_f, { 0.0F, 1.0F } },
        { { 0.5F, -0.5F, 0.5F }, n_f, { 1.0F, 1.0F } },
        { { 0.5F, 0.5F, 0.5F }, n_f, { 1.0F, 0.0F } },
        { { -0.5F, 0.5F, 0.5F }, n_f, { 0.0F, 0.0F } },
        { { -0.5F, -0.5F, -0.5F }, n_dn, { 0.0F, 1.0F } },
        { { 0.5F, -0.5F, -0.5F }, n_dn, { 1.0F, 1.0F } },
        { { 0.5F, -0.5F, 0.5F }, n_dn, { 1.0F, 0.0F } },
        { { -0.5F, -0.5F, 0.5F }, n_dn, { 0.0F, 0.0F } },
        { { -0.5F, 0.5F, -0.5F }, n_up, { 0.0F, 1.0F } },
        { { 0.5F, 0.5F, -0.5F }, n_up, { 1.0F, 1.0F } },
        { { 0.5F, 0.5F, 0.5F }, n_up, { 1.0F, 0.0F } },
        { { -0.5F, 0.5F, 0.5F }, n_up, { 0.0F, 0.0F } },
        { { -0.5F, -0.5F, -0.5F }, n_l, { 0.0F, 1.0F } },
        { { -0.5F, 0.5F, -0.5F }, n_l, { 1.0F, 1.0F } },
        { { -0.5F, 0.5F, 0.5F }, n_l, { 1.0F, 0.0F } },
        { { -0.5F, -0.5F, 0.5F }, n_l, { 0.0F, 0.0F } },
        { { 0.5F, -0.5F, -0.5F }, n_r, { 0.0F, 1.0F } },
        { { 0.5F, 0.5F, -0.5F }, n_r, { 1.0F, 1.0F } },
        { { 0.5F, 0.5F, 0.5F }, n_r, { 1.0F, 0.0F } },
        { { 0.5F, -0.5F, 0.5F }, n_r, { 0.0F, 0.0F } },
    } };

    // 每三角形 (v0,v1,v2) 满足 cross(p1-p0, p2-p0)
    // 与面外法线同向（右手系、从外面看为 CCW）； 配合 `proj[1][1] *= -1` 使用
    // VK_FRONT_FACE_CLOCKWISE + 背面剔除。
    const std::array<uint16_t, 36> indices { {
        0,  2,  1,  0,  3,  2,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 19, 18, 16, 18, 17, 20, 21, 22, 20, 22, 23,
    } };

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

    lumen::render::DescriptorSetLayout descLayout;
    const std::vector<lumen::render::DescriptorBinding> descBindings = {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .count = 1,
          .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
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

    constexpr size_t kUboSize { sizeof(SceneUbo) };
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight>
        uniformBuffers {};
    for (auto &ub : uniformBuffers) {
        if (!ub.create(ctx, kUboSize)) {
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
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(Vertex, normal) });
    pipeConfig.vertexAttributes.push_back(
        { .location = 2,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec2,
          .offset = offsetof(Vertex, uv) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    // 外法线 + 索引绕序为从外面看 CCW，与 `proj[1][1] *= -1` 及 CCW 正面一致
    pipeConfig.cullMode = VK_CULL_MODE_NONE;
    pipeConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
        "assets/font/SourceHanSansSC/OTF/SimplifiedChinese/"
        "SourceHanSansSC-Bold.otf");
    imgui_font_jp_path = lumen::core::get_resource_path(
        "assets/font/SourceHanSansSC/OTF/Japanese/SourceHanSans-Bold.otf");
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
            sceneTarget.color_sample_layout()));

    LUMEN_APP_LOG_INFO(
        "按 Esc 退出；场景相机：Alt+左键环绕、Alt+中键平移、Alt+右键/滚轮缩放；"
        "右键拖拽环视，右键+WASD 平移枢轴、E/Q 升降、Shift 加速；"
        "多光源：定向光 / 点光 / 聚光灯（侧栏）；Docking 可停靠「视口」");

    lumen::platform::EventPump pump;
    uint32_t currentFrame { 0 };
    bool running { true };
    int fbWidth { window_width };
    int fbHeight { window_height };
    bool needRecreateSwapchain { false };

    lumen::ui::TextureViewRect scene_viewport_rect {};
    lumen::scene::SceneCamera scene_cam;
    scene_cam.set_projection_perspective(55.0F, 0.1F, 100.0F);
    lumen::scene::SceneOrbitController scene_orbit;
    scene_orbit.set_pivot(glm::vec3(0.0F));
    scene_orbit.sync_from_view(glm::lookAt(glm::vec3(1.35F, 1.0F, 1.35F),
                                           glm::vec3(0.0F),
                                           glm::vec3(0.0F, 1.0F, 0.0F)));
    scene_orbit.apply_to(scene_cam);

    lumen::ui::ImGuiLayer imgui_layer;
    imgui_layer.attach(pump);

    pump.set_on_application_event([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventQuit>(
            [&](lumen::platform::EventQuit &) {
                running = false;
                window.set_relative_mouse_mode(false);
                return true;
            });
        d.dispatch<lumen::platform::EventKeyDown>(
            [&](lumen::platform::EventKeyDown &e) {
                if (e.key == lumen::platform::Key::Escape) {
                    running = false;
                    window.set_relative_mouse_mode(false);
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

    pump.push_layer([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventMouseWheel>(
            [&](lumen::platform::EventMouseWheel &e) {
                const auto hover = lumen::ui::viewport_mouse_state(
                    scene_viewport_rect, pump.input().mouse_x(),
                    pump.input().mouse_y());
                if (lumen::ui::imgui_wants_mouse() && !hover.inViewport) {
                    return false;
                }
                scene_orbit.apply_scroll_zoom(e.deltaY, kSceneScrollZoomSpeed);
                return false;
            });
    });

    float spin_rad_per_sec { kSpinRadPerSecond };

    std::array<EditLight, kMaxLights> edit_lights {};
    edit_lights[0] = { .type = 0,
                       .enabled = true,
                       .direction = { 0.45F, 0.78F, 0.35F },
                       .color = { 1.0F, 0.94F, 0.82F },
                       .intensity = 0.55F };
    edit_lights[1] = { .type = 1,
                       .enabled = true,
                       .position = { -2.2F, 1.3F, 1.6F },
                       .color = { 0.35F, 0.58F, 1.0F },
                       .intensity = 0.42F,
                       .range = 11.0F };
    edit_lights[2] = { .type = 2,
                       .enabled = true,
                       .position = { 2.0F, 2.9F, -0.4F },
                       .direction = { -0.15F, -0.92F, 0.2F },
                       .color = { 1.0F, 0.88F, 0.65F },
                       .intensity = 0.85F,
                       .range = 14.0F,
                       .spot_outer_deg = 26.0F,
                       .spot_inner_deg = 14.0F };
    edit_lights[3] = { .type = 1,
                       .enabled = true,
                       .position = { 0.0F, -1.5F, 2.5F },
                       .color = { 0.5F, 0.55F, 0.62F },
                       .intensity = 0.25F,
                       .range = 9.0F };

    float mat_ambient { 0.12F };
    float mat_diffuse { 0.75F };
    float mat_specular { 0.45F };
    float mat_shininess { 48.0F };

    /// 上一帧「视口」窗口期望的离屏尺寸；在 acquire 之前应用，避免与
    /// ImGui::Image 同帧换纹理
    uint32_t pending_viewport_w { sceneTarget.width() };
    uint32_t pending_viewport_h { sceneTarget.height() };

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;

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
                    VK_NULL_HANDLE);
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
                    sceneTarget.color_sample_layout()));
        }

        const uint32_t scene_w = sceneTarget.width();
        const uint32_t scene_h = sceneTarget.height();

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX) {
            continue;
        }

        imgui_layer.begin_frame();

        const auto dt = static_cast<float>(frame_dt.tick_seconds());

        const auto &inp = pump.input();
        const auto scene_nav_mouse = lumen::ui::viewport_mouse_state(
            scene_viewport_rect, inp.mouse_x(), inp.mouse_y());
        const bool cam_nav_ok = scene_nav_mouse.inViewport;
        const bool imgui_blocks_mouse =
            lumen::ui::imgui_wants_mouse() && !scene_nav_mouse.inViewport;

        static bool scene_relative_mouse { false };
        const bool want_rel = scene_orbit.apply_per_frame_editor_navigation(
            inp, cam_nav_ok, imgui_blocks_mouse, dt);
        if (want_rel != scene_relative_mouse) {
            window.set_relative_mouse_mode(want_rel);
            scene_relative_mouse = want_rel;
        }

        if (ImGui::Begin("视口")) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            pending_viewport_w =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, avail.x)));
            pending_viewport_h =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, avail.y)));
            ImGui::Image(scene_tex_id, ImVec2(static_cast<float>(scene_w),
                                              static_cast<float>(scene_h)));
            {
                const ImVec2 min_p = ImGui::GetItemRectMin();
                const ImVec2 max_p = ImGui::GetItemRectMax();
                scene_viewport_rect.minX = min_p.x;
                scene_viewport_rect.minY = min_p.y;
                scene_viewport_rect.maxX = max_p.x;
                scene_viewport_rect.maxY = max_p.y;
            }
            ImGui::Separator();
            ImGui::TextWrapped(
                "场景相机（鼠标需在图像上）：Alt+左键环绕 · Alt+中键平移 · "
                "Alt+右键/滚轮缩放 · 右键环视 · 右键+WASD 平移 · E/Q 升降 · "
                "Shift 加速");
        }
        ImGui::End();

        if (ImGui::Begin("光照 / 材质")) {
            ImGui::SliderFloat("角速度（弧度/秒）", &spin_rad_per_sec, 0.0F,
                               3.0F, "%.2f");
            ImGui::Separator();
            ImGui::Text("光源（最多 %d 盏，仅「启用」的参与累加）", kMaxLights);
            static const char *kLightTypeItems = "定向光\0点光源\0聚光灯\0\0";
            for (int li = 0; li < kMaxLights; ++li) {
                EditLight &L = edit_lights[static_cast<size_t>(li)];
                ImGui::PushID(li);
                ImGuiTreeNodeFlags tree_flags =
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                if (li < 4) {
                    tree_flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }
                if (ImGui::TreeNodeEx("", tree_flags, "光源 %d", li + 1)) {
                    ImGui::Checkbox("启用", &L.enabled);
                    ImGui::Combo("类型", &L.type, kLightTypeItems);
                    ImGui::ColorEdit3("颜色",
                                      reinterpret_cast<float *>(&L.color));
                    ImGui::SliderFloat("强度", &L.intensity, 0.0F, 3.0F);
                    if (L.type == 0) {
                        ImGui::DragFloat3(
                            "方向（世界，指向光照方向）",
                            reinterpret_cast<float *>(&L.direction), 0.02F);
                    } else if (L.type == 1) {
                        ImGui::DragFloat3(
                            "位置（世界）",
                            reinterpret_cast<float *>(&L.position), 0.05F);
                        ImGui::SliderFloat("作用范围", &L.range, 0.5F, 40.0F);
                    } else {
                        ImGui::DragFloat3(
                            "位置（世界）",
                            reinterpret_cast<float *>(&L.position), 0.05F);
                        ImGui::DragFloat3(
                            "锥轴（世界，从灯位指向照亮方向）",
                            reinterpret_cast<float *>(&L.direction), 0.02F);
                        ImGui::SliderFloat("作用范围", &L.range, 0.5F, 40.0F);
                        ImGui::SliderFloat("锥角外缘（度）", &L.spot_outer_deg,
                                           5.0F, 80.0F);
                        ImGui::SliderFloat("锥角内缘（度）", &L.spot_inner_deg,
                                           1.0F, 75.0F);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::SliderFloat("环境光系数", &mat_ambient, 0.0F, 1.0F);
            ImGui::SliderFloat("漫反射系数", &mat_diffuse, 0.0F, 2.0F);
            ImGui::SliderFloat("高光系数", &mat_specular, 0.0F, 2.0F);
            ImGui::SliderFloat("高光锐度（shininess）", &mat_shininess, 1.0F,
                               256.0F);
            ImGui::Text("Esc：退出");
        }
        ImGui::End();

        VkCommandBuffer cmdBuf = cmdBuffers[currentFrame];
        vkResetCommandBuffer(cmdBuf, 0);

        const float seconds = static_cast<float>(SDL_GetTicks()) * 0.001F;
        const float aspect = static_cast<float>(scene_w) /
                             std::max(1.0F, static_cast<float>(scene_h));
        glm::mat4 model =
            glm::rotate(glm::mat4(1.0F), seconds * spin_rad_per_sec,
                        glm::vec3(0.0F, 1.0F, 0.0F));
        scene_orbit.apply_to(scene_cam);
        const glm::mat4 view = scene_cam.view_matrix();
        const glm::mat4 proj = scene_cam.projection_matrix(aspect);
        const glm::vec3 camera_world = scene_cam.eye_position();

        SceneUbo ubo {};
        ubo.model = model;
        ubo.view = view;
        ubo.proj = proj;
        ubo.cameraWorld = glm::vec4(camera_world, 1.0F);
        ubo.materialParams =
            glm::vec4(mat_ambient, mat_diffuse, mat_specular, mat_shininess);
        int active_lights { 0 };
        for (int i = 0; i < kMaxLights; ++i) {
            if (!edit_lights[static_cast<size_t>(i)].enabled) {
                continue;
            }
            if (active_lights >= kMaxLights) {
                break;
            }
            pack_gpu_light(ubo.lights[static_cast<size_t>(active_lights)],
                           edit_lights[static_cast<size_t>(i)]);
            ++active_lights;
        }
        ubo.lightCount_ = glm::ivec4(active_lights, 0, 0, 0);
        uniformBuffers[currentFrame].update(ubo);

        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmdBuf, &beginInfo) !=
            VK_SUCCESS) {
            continue;
        }

        // 1) 离屏：立方体 + 深度
        VkClearValue scene_clears[2] {};
        scene_clears[0].color = { { 0.05F, 0.06F, 0.09F, 1.0F } };
        scene_clears[1].depthStencil = { 1.0F, 0 };

        VkRenderPassBeginInfo scene_rp {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        scene_rp.renderPass = sceneTarget.render_pass();
        scene_rp.framebuffer = sceneTarget.framebuffer();
        scene_rp.renderArea.offset = { .x = 0, .y = 0 };
        scene_rp.renderArea.extent = sceneTarget.extent();
        scene_rp.clearValueCount = 2;
        scene_rp.pClearValues = scene_clears;

        vkCmdBeginRenderPass(cmdBuf, &scene_rp,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport scene_vp {};
        scene_vp.x = 0.0F;
        scene_vp.y = 0.0F;
        scene_vp.width = static_cast<float>(sceneTarget.extent().width);
        scene_vp.height = static_cast<float>(sceneTarget.extent().height);
        scene_vp.minDepth = 0.0F;
        scene_vp.maxDepth = 1.0F;
        vkCmdSetViewport(cmdBuf, 0, 1, &scene_vp);

        VkRect2D scene_sc {};
        scene_sc.offset = { .x = 0, .y = 0 };
        scene_sc.extent = sceneTarget.extent();
        vkCmdSetScissor(cmdBuf, 0, 1, &scene_sc);

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

        // 2) 交换链：仅 ImGui
        VkClearValue swap_clear {};
        swap_clear.color = { { 0.07F, 0.08F, 0.11F, 1.0F } };

        VkRenderPassBeginInfo rpBegin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpBegin.renderPass = renderPass.handle();
        rpBegin.framebuffer = framebuffers.get(imageIndex);
        rpBegin.renderArea.offset = { .x = 0, .y = 0 };
        rpBegin.renderArea.extent = swapchain.extent();
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &swap_clear;

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

        imgui_layer.end_frame(cmdBuf);

        vkCmdEndRenderPass(cmdBuf);

        if (vkEndCommandBuffer(cmdBuf) != VK_SUCCESS) {
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
        submitInfo.pCommandBuffers = &cmdBuf;
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

    window.set_relative_mouse_mode(false);
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
    const int result = run_blinn_phong();
    lumen::core::Logger::shutdown();
    return result;
}
