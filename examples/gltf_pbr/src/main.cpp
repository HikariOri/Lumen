/**
 * @file main.cpp
 * @brief PBR — IBL 烘焙、HDR 天空盒、Sponza（glTF）与 ImGui 预览
 */

#include "ibl_bake.hpp"

#include "asset/asset_registry.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"
#include "scene/scene_mesh_asset.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/frame_sync.hpp"
#include "render/material/material.hpp"
#include "render/material/pbr_forward_ubo.hpp"
#include "render/material/pbr_material_bind.hpp"
#include "render/pbr_forward_record_render_items.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/surface.hpp"
#include "render/swapchain.hpp"
#include "scene/scene_mesh_spawn.hpp"
#include "asset/geometry/mesh_asset.hpp"
#include "scene/render.hpp"
#include "scene/scene.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_keyboard.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ghc/filesystem.hpp>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr const char *kHdrRelPath { "assets/environment_maps/meadow_2_2k.hdr" };

constexpr const char *kSponzaGltfRel {
    "assets/models/Sponza/glTF/Sponza.gltf"
};
/// 居中后按包围盒最长边缩放到该世界单位长度（越大场景在画面里越大）
constexpr float k_sponza_fit_max_extent { 9.0F };

[[nodiscard]] bool sdl_alt_down() {
    const SDL_Keymod m = SDL_GetModState();
    return (static_cast<uint32_t>(m) & static_cast<uint32_t>(SDL_KMOD_ALT)) !=
           0U;
}

[[nodiscard]] glm::vec3 cam_forward_yaw_pitch_deg(float yaw_deg,
                                                  float pitch_deg) {
    const float y = glm::radians(yaw_deg);
    const float p = glm::radians(pitch_deg);
    glm::vec3 f {};
    f.x = std::cos(y) * std::cos(p);
    f.y = std::sin(p);
    f.z = std::sin(y) * std::cos(p);
    return glm::normalize(f);
}

/// Unity Scene 视口式：右键按住时 WASD 飞行，E/Q 升降（与 Unity 一致）
void apply_scene_view_fly_wasd(glm::vec3 &eye, float yaw_deg, float pitch_deg,
                               float dt) {
    const bool *const ks = SDL_GetKeyboardState(nullptr);
    glm::vec3 f = cam_forward_yaw_pitch_deg(yaw_deg, pitch_deg);
    const glm::vec3 world_up { 0.0F, 1.0F, 0.0F };
    glm::vec3 r = glm::normalize(glm::cross(f, world_up));
    if (glm::length(r) < 1e-5F) {
        r = glm::vec3 { 1.0F, 0.0F, 0.0F };
    }
    glm::vec3 move {};
    if (ks[SDL_SCANCODE_W]) {
        move += f;
    }
    if (ks[SDL_SCANCODE_S]) {
        move -= f;
    }
    if (ks[SDL_SCANCODE_D]) {
        move += r;
    }
    if (ks[SDL_SCANCODE_A]) {
        move -= r;
    }
    if (ks[SDL_SCANCODE_E]) {
        move += world_up;
    }
    if (ks[SDL_SCANCODE_Q]) {
        move -= world_up;
    }
    if (glm::length(move) < 1e-6F) {
        return;
    }
    constexpr float k_fly_speed { 5.0F };
    move = glm::normalize(move) * (k_fly_speed * dt);
    eye += move;
}

// 单位立方体（与 ibl_bake 天空捕获一致）
constexpr std::array<float, 36U * 3U> kSkyCubePositions { {
    -1.0F, 1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  -1.0F, -1.0F,
    1.0F,  -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, 1.0F,  -1.0F,

    -1.0F, -1.0F, 1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  -1.0F,
    -1.0F, 1.0F,  -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, 1.0F,

    1.0F,  -1.0F, -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F, -1.0F,

    -1.0F, -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F, -1.0F, 1.0F,

    -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F,

    -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,
    1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, -1.0F,
} };

struct SkyPush {
    glm::mat4 sky_mvp {};
    glm::vec4 params {}; // x: exposure
};

struct HelmVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
    glm::vec4 tangent { 1.0F, 0.0F, 0.0F, 1.0F };
};

[[nodiscard]] VkImageView
create_cubemap_plus_x_face_view(VkDevice dev, VkImage img, VkFormat fmt,
                                uint32_t mip_level, const char *label) {
    VkImageViewCreateInfo vi { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = mip_level;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount = 1;
    VkImageView v { VK_NULL_HANDLE };
    const VkResult rc = vkCreateImageView(dev, &vi, nullptr, &v);
    if (rc != VK_SUCCESS) {
        LUMEN_APP_LOG_ERROR("vkCreateImageView 失败 ({}) result={}", label,
                            static_cast<int>(rc));
        return VK_NULL_HANDLE;
    }
    return v;
}

void destroy_view(VkDevice dev, VkImageView v) {
    if (v != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, v, nullptr);
    }
}

} // namespace

static int run_pbr(int, char **) {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen — PBR Sponza (glTF) + IBL";
    winConfig.width = 1280;
    winConfig.height = 800;
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
    if (window_width <= 0 || window_height <= 0) {
        LUMEN_APP_LOG_WARN("窗口帧缓冲尺寸无效 {}x{}，使用配置 {}x{}",
                           window_width, window_height, winConfig.width,
                           winConfig.height);
        window_width = winConfig.width;
        window_height = winConfig.height;
    }

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(),
                          static_cast<uint32_t>(window_width),
                          static_cast<uint32_t>(window_height))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    // 交换链：仅 ImGui 合成（无深度）
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

    lumen::render::RenderPassConfig offscreen_rp_cfg;
    offscreen_rp_cfg.useDepth = true;
    offscreen_rp_cfg.colorAttachment.format = swapchain.image_format();
    offscreen_rp_cfg.colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::RenderPass offscreen_render_pass;
    if (!offscreen_render_pass.create(ctx.device(), offscreen_rp_cfg)) {
        LUMEN_APP_LOG_ERROR("离屏 RenderPass 创建失败");
        return -1;
    }

    lumen::render::OffscreenRenderTarget scene_target;
    {
        lumen::render::OffscreenRenderTargetConfig scene_cfg;
        scene_cfg.width =
            static_cast<uint32_t>(std::max(2, window_width * 3 / 4));
        scene_cfg.height =
            static_cast<uint32_t>(std::max(2, window_height * 3 / 4));
        scene_cfg.format = swapchain.image_format();
        scene_cfg.useDepth = true;
        scene_cfg.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!scene_target.create(ctx, scene_cfg, &offscreen_render_pass)) {
            LUMEN_APP_LOG_ERROR("场景离屏渲染目标创建失败");
            return -1;
        }
    }

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    const std::string hdr_path = lumen::core::get_resource_path(kHdrRelPath);
    LUMEN_APP_LOG_INFO("IBL 烘焙 HDR: {}", hdr_path);

    pbr::IblTextures ibl {};
    std::string bake_err;
    if (!pbr::bake_ibl(ctx, cmdPool, ctx.graphics_queue(), hdr_path.c_str(),
                       ibl, bake_err)) {
        LUMEN_APP_LOG_ERROR("IBL 烘焙失败: {}", bake_err);
        return -1;
    }

    const float k_prefilter_max_lod = static_cast<float>(
        ibl.prefilter.mip_levels() > 0 ? ibl.prefilter.mip_levels() - 1 : 0);

    VkDevice dev = ctx.device();
    const VkFormat ibl_fmt = VK_FORMAT_R32G32B32A32_SFLOAT;

    VkImageView v_env = create_cubemap_plus_x_face_view(
        dev, ibl.environment.image(), ibl_fmt, 0, "IBL environment +X");
    VkImageView v_irr = create_cubemap_plus_x_face_view(
        dev, ibl.irradiance.image(), ibl_fmt, 0, "IBL irradiance +X");
    VkImageView v_pre = create_cubemap_plus_x_face_view(
        dev, ibl.prefilter.image(), ibl_fmt, 0, "IBL prefilter +X");
    VkImageView v_brdf = ibl.brdf_lut.view();

    if (v_env == VK_NULL_HANDLE || v_irr == VK_NULL_HANDLE ||
        v_pre == VK_NULL_HANDLE || v_brdf == VK_NULL_HANDLE) {
        LUMEN_APP_LOG_ERROR(
            "ImGui 预览用 ImageView 无效: env={} irr={} pre={} brdf={}",
            v_env != VK_NULL_HANDLE, v_irr != VK_NULL_HANDLE,
            v_pre != VK_NULL_HANDLE, v_brdf != VK_NULL_HANDLE);
        return -1;
    }

    namespace fs = ghc::filesystem;

    const std::string scene_gltf_path =
        lumen::core::get_resource_path(kSponzaGltfRel);
    if (!fs::exists(fs::path(scene_gltf_path))) {
        LUMEN_APP_LOG_ERROR("未找到 Sponza: {}（需同目录 Sponza.bin 与纹理）",
                            scene_gltf_path);
        return -1;
    }

    VkQueue gq = ctx.graphics_queue();

    lumen::render::PbrPlaceholderTextures pbr_placeholders;
    if (!pbr_placeholders.create(ctx, gq, cmdPool) ||
        !pbr_placeholders.is_complete()) {
        LUMEN_APP_LOG_ERROR("PBR 占位贴图创建失败");
        return -1;
    }

    lumen::scene::SceneMeshLoadOptions scene_load_opts {};
    scene_load_opts.recenterToOrigin = true;
    scene_load_opts.uniformScaleMaxAxis = k_sponza_fit_max_extent;
    std::string scene_load_err;
    const lumen::asset::SceneMeshAssetHandle scene_handle =
        lumen::asset::AssetRegistry::instance().load_scene_mesh_handle(
            ctx, gq, cmdPool, scene_gltf_path, scene_load_opts, &scene_load_err);
    if (!scene_handle.valid()) {
        LUMEN_APP_LOG_ERROR("glTF 场景加载失败: {}", scene_load_err.empty()
                                                         ? "unknown"
                                                         : scene_load_err);
        return -1;
    }
    const std::shared_ptr<lumen::scene::SceneMeshAsset> scene_asset_sp =
        lumen::asset::AssetRegistry::instance().try_get_scene_mesh(scene_handle);
    if (!scene_asset_sp) {
        LUMEN_APP_LOG_ERROR("glTF 场景句柄解析失败");
        return -1;
    }
    lumen::scene::SceneMeshAsset &scene_asset = *scene_asset_sp;
    if (scene_asset.materials.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 无材质: {}", scene_gltf_path);
        return -1;
    }

    std::unordered_map<const lumen::render::Material *, uint32_t>
        scene_material_to_ds_index;
    std::vector<const lumen::render::Material *> scene_unique_materials;
    for (const auto &mat_inst : scene_asset.materials) {
        if (!mat_inst) {
            continue;
        }
        const lumen::render::Material *const p = &mat_inst->material;
        if (scene_material_to_ds_index.find(p) !=
            scene_material_to_ds_index.end()) {
            continue;
        }
        const uint32_t idx =
            static_cast<uint32_t>(scene_unique_materials.size());
        scene_material_to_ds_index[p] = idx;
        scene_unique_materials.push_back(p);
    }
    if (scene_unique_materials.empty()) {
        LUMEN_APP_LOG_ERROR("glTF 无有效材质实例: {}", scene_gltf_path);
        return -1;
    }

    std::size_t scene_prim_definitions = 0;
    for (const lumen::asset::geometry::Mesh &m : scene_asset.model) {
        scene_prim_definitions += m.primitives.size();
    }
    lumen::scene::Scene gltf_ecs_scene;
    lumen::scene::SceneMeshSpawnOptions scene_spawn_opts {};
    scene_spawn_opts.owning_scene = scene_asset_sp;
    scene_spawn_opts.scene_mesh_handle = scene_handle;
    const lumen::scene::SceneMeshSpawnResult scene_spawn_r =
        lumen::scene::spawn_scene_mesh_hierarchy(gltf_ecs_scene, scene_asset,
                                                 "SceneSub", scene_spawn_opts);
    LUMEN_APP_LOG_INFO(
        "glTF 已加载: 顶点={} 索引={} 三角≈{} mesh={} primitive定义={} "
        "primitive实例={} 场景节点={} 材质={} 路径={}",
        scene_asset.statsVertexCount, scene_asset.statsIndexCount,
        scene_asset.statsIndexCount / 3U, scene_asset.model.size(),
        scene_prim_definitions, scene_spawn_r.drawable_primitive_instances,
        scene_asset.scene_nodes.size(), scene_asset.materials.size(),
        scene_gltf_path);

    auto cmd_buffers = cmdPool.allocate(3);
    if (cmd_buffers.size() != 3) {
        LUMEN_APP_LOG_ERROR("CommandBuffer 分配失败");
        return -1;
    }

    lumen::render::FrameSync frameSync;
    if (!frameSync.create(ctx.device(), swapchain.image_count(), 3)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    lumen::ui::ImGuiBackendInitInfo imguiInfo;
    imguiInfo.ctx = &ctx;
    imguiInfo.swapchain = &swapchain;
    imguiInfo.renderPass = renderPass.handle();
    imguiInfo.window = window.sdl_window();
    static std::string font_sc;
    static std::string font_jp;
    font_sc = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/SimplifiedChinese/"
        "SourceHanSansSC-Bold.otf");
    font_jp = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/Japanese/SourceHanSans-Bold.otf");
    imguiInfo.cjk_font_ttf_path = font_sc.c_str();
    imguiInfo.cjk_font_japanese_merge_path = font_jp.c_str();
    imguiInfo.enable_docking = true;
    if (!lumen::ui::imgui_backend_init(imguiInfo)) {
        LUMEN_APP_LOG_ERROR("ImGui 初始化失败");
        return -1;
    }

    lumen::render::Sampler uiSampler;
    if (!uiSampler.create(ctx)) {
        LUMEN_APP_LOG_ERROR("UI Sampler 创建失败");
        return -1;
    }

    lumen::render::Sampler scene_sampler;
    if (!scene_sampler.create(ctx)) {
        LUMEN_APP_LOG_ERROR("场景采样器创建失败");
        return -1;
    }

    ImTextureID scene_tex_id =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            scene_sampler.handle(), scene_target.color_view(),
            scene_target.color_sample_layout()));

    const std::string sky_vs_path =
        lumen::core::get_resource_path("shaders/skybox.vert.spv");
    const std::string sky_fs_path =
        lumen::core::get_resource_path("shaders/skybox.frag.spv");
    lumen::render::ShaderModule sm_sky_vs;
    lumen::render::ShaderModule sm_sky_fs;
    if (!sm_sky_vs.create_from_file(dev, sky_vs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("天空盒顶点着色器加载失败: {}", sky_vs_path);
        return -1;
    }
    if (!sm_sky_fs.create_from_file(dev, sky_fs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("天空盒片元着色器加载失败: {}", sky_fs_path);
        return -1;
    }

    lumen::render::DescriptorSetLayout sky_dsl;
    std::vector<lumen::render::DescriptorBinding> sky_binds = {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!sky_dsl.create(ctx, sky_binds)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorSetLayout 失败");
        return -1;
    }

    lumen::render::DescriptorPool sky_dpool;
    if (!sky_dpool.create(ctx,
                          { { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              .count = 1 } },
                          1)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorPool 失败");
        return -1;
    }

    VkDescriptorSet sky_ds { VK_NULL_HANDLE };
    if (!sky_dpool.allocate(dev, sky_dsl.handle(), sky_ds)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::write_descriptor_set(
        dev, sky_ds, {},
        { { .binding = 0,
            .imageView = ibl.environment.view(),
            .sampler = ibl.environment.sampler(),
            .imageLayout = ibl.environment.descriptor_layout() } });

    VkPushConstantRange sky_pc {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = static_cast<uint32_t>(sizeof(SkyPush)),
    };
    lumen::render::PipelineLayout sky_pl;
    if (!sky_pl.create(ctx, { sky_dsl.handle() }, { sky_pc })) {
        LUMEN_APP_LOG_ERROR("天空盒 PipelineLayout 失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig sky_cfg {};
    sky_cfg.shaderStages.push_back(
        { sm_sky_vs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    sky_cfg.shaderStages.push_back(
        { sm_sky_fs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    sky_cfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(float) * 3,
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    sky_cfg.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = 0 });
    sky_cfg.depthTest = true;
    sky_cfg.depthWrite = false;
    sky_cfg.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sky_cfg.cullMode = VK_CULL_MODE_FRONT_BIT;
    sky_cfg.frontFace = VK_FRONT_FACE_CLOCKWISE;

    lumen::render::GraphicsPipeline sky_pipe;
    if (!sky_pipe.create(ctx, sky_pl, offscreen_render_pass, 0, sky_cfg)) {
        LUMEN_APP_LOG_ERROR("天空盒 GraphicsPipeline 失败");
        return -1;
    }

    lumen::render::VertexBuffer sky_vbuf;
    if (!sky_vbuf.create_device_local_and_upload(
            ctx, ctx.graphics_queue(), cmdPool, kSkyCubePositions.data(),
            sizeof(kSkyCubePositions))) {
        LUMEN_APP_LOG_ERROR("天空盒顶点缓冲上传失败");
        return -1;
    }

    const std::size_t helmet_obj_stride =
        lumen::render::pbr_object_ubo_dynamic_stride(static_cast<std::size_t>(
            ctx.physical_device_properties()
                .limits.minUniformBufferOffsetAlignment));
    const uint32_t gltf_draw_slots = (std::max)(
        scene_spawn_r.drawable_primitive_instances, 1u);
    const VkDeviceSize helmet_object_ubo_bytes =
        static_cast<VkDeviceSize>(helmet_obj_stride) *
        static_cast<VkDeviceSize>(gltf_draw_slots);

    const std::string helmet_vs_path = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_VERT_SPV_RELATIVE));
    const std::string helmet_fs_path = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_FRAG_SPV_RELATIVE));
    lumen::render::ShaderModule sm_helmet_vs;
    lumen::render::ShaderModule sm_helmet_fs;
    if (!sm_helmet_vs.create_from_file(dev, helmet_vs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("PBR 前向顶点着色器加载失败: {}", helmet_vs_path);
        return -1;
    }
    if (!sm_helmet_fs.create_from_file(dev, helmet_fs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("PBR 前向片元着色器加载失败: {}", helmet_fs_path);
        return -1;
    }

    lumen::render::DescriptorSetLayout helmet_frame_dsl;
    if (!helmet_frame_dsl.create(
            ctx, lumen::render::pbr_frame_ibl_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Frame DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmet_material_dsl;
    if (!helmet_material_dsl.create(
            ctx, lumen::render::pbr_material_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR 材质 DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmet_object_dsl;
    if (!helmet_object_dsl.create(
            ctx, lumen::render::pbr_object_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Object DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmet_light_dsl;
    if (!helmet_light_dsl.create(
            ctx, lumen::render::pbr_light_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Light DescriptorSetLayout 失败");
        return -1;
    }

    const auto mat_count_u32 =
        static_cast<uint32_t>(scene_unique_materials.size());
    const uint32_t pbr_ubo_static = 3u + mat_count_u32 + 3u;
    const uint32_t pbr_set_count = 3u + mat_count_u32 + 1u + 3u;
    const uint32_t pbr_combined_for_materials = mat_count_u32 * 5U;
    const uint32_t pbr_combined_total = 9U + pbr_combined_for_materials;

    lumen::render::DescriptorPool helmet_dpool;
    if (!helmet_dpool.create(
            ctx,
            { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = pbr_ubo_static },
              { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .count = 1 },
              { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .count = pbr_combined_total } },
            pbr_set_count)) {
        LUMEN_APP_LOG_ERROR("PBR DescriptorPool 失败 (材质数={})",
                            mat_count_u32);
        return -1;
    }

    std::vector<lumen::render::UniformBuffer> sponza_material_ubos(
        mat_count_u32);
    for (uint32_t mi = 0; mi < mat_count_u32; ++mi) {
        if (!sponza_material_ubos[mi].create_persistent(
                ctx, sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 失败 mi={}", mi);
            return -1;
        }
    }

    std::vector<VkDescriptorSet> sponza_material_ds(mat_count_u32);
    for (uint32_t mi = 0; mi < mat_count_u32; ++mi) {
        if (!helmet_dpool.allocate(dev, helmet_material_dsl.handle(),
                                   sponza_material_ds[mi])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败 mi={}", mi);
            return -1;
        }
        lumen::render::PbrMaterialUbo mu {};
        lumen::render::pack_pbr_material_ubo(mu, *scene_unique_materials[mi],
                                             3.0F);
        sponza_material_ubos[mi].update(mu);
        lumen::render::write_pbr_material_descriptor_set(
            dev, sponza_material_ds[mi], sponza_material_ubos[mi].handle(),
            sizeof(lumen::render::PbrMaterialUbo), *scene_unique_materials[mi],
            pbr_placeholders);
    }

    std::array<VkDescriptorSet, 3> helmet_frame_ds {};
    for (uint32_t i = 0; i < helmet_frame_ds.size(); ++i) {
        if (!helmet_dpool.allocate(dev, helmet_frame_dsl.handle(),
                                   helmet_frame_ds[i])) {
            LUMEN_APP_LOG_ERROR("Frame DescriptorSet 分配失败");
            return -1;
        }
    }

    std::array<lumen::render::UniformBuffer, 3> helmet_frame_ubos {};
    for (uint32_t i = 0; i < helmet_frame_ubos.size(); ++i) {
        if (!helmet_frame_ubos[i].create_persistent(
                ctx, sizeof(lumen::render::PbrFrameUbo))) {
            LUMEN_APP_LOG_ERROR("Frame UniformBuffer 失败");
            return -1;
        }
    }

    for (uint32_t i = 0; i < helmet_frame_ds.size(); ++i) {
        lumen::render::write_pbr_frame_ibl_descriptor_set(
            dev, helmet_frame_ds[i], helmet_frame_ubos[i].handle(),
            sizeof(lumen::render::PbrFrameUbo), ibl.irradiance, ibl.prefilter,
            ibl.brdf_lut);
    }

    VkDescriptorSet helmet_object_ds { VK_NULL_HANDLE };
    if (!helmet_dpool.allocate(dev, helmet_object_dsl.handle(),
                               helmet_object_ds)) {
        LUMEN_APP_LOG_ERROR("Object DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::UniformBuffer helmet_object_ubo;
    if (!helmet_object_ubo.create_persistent(
            ctx, static_cast<size_t>(helmet_object_ubo_bytes))) {
        LUMEN_APP_LOG_ERROR("Object UniformBuffer 失败");
        return -1;
    }
    lumen::render::write_pbr_object_descriptor_set_dynamic(
        dev, helmet_object_ds, helmet_object_ubo.handle(),
        sizeof(lumen::render::PbrObjectUbo));

    std::array<VkDescriptorSet, 3> helmet_light_ds {};
    std::array<lumen::render::UniformBuffer, 3> helmet_light_ubos {};
    for (uint32_t i = 0; i < helmet_light_ds.size(); ++i) {
        if (!helmet_dpool.allocate(dev, helmet_light_dsl.handle(),
                                   helmet_light_ds[i])) {
            LUMEN_APP_LOG_ERROR("Light DescriptorSet 分配失败");
            return -1;
        }
        if (!helmet_light_ubos[i].create_persistent(
                ctx, sizeof(lumen::render::PbrLightUbo))) {
            LUMEN_APP_LOG_ERROR("Light UniformBuffer 失败");
            return -1;
        }
        lumen::render::write_pbr_light_descriptor_set(
            dev, helmet_light_ds[i], helmet_light_ubos[i].handle(),
            sizeof(lumen::render::PbrLightUbo));
    }

    lumen::render::PipelineLayout helmet_pl;
    if (!helmet_pl.create(
            ctx,
            { helmet_frame_dsl.handle(), helmet_material_dsl.handle(),
              helmet_object_dsl.handle(), helmet_light_dsl.handle() },
            {})) {
        LUMEN_APP_LOG_ERROR("PBR PipelineLayout 失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig helmet_cfg {};
    helmet_cfg.shaderStages.push_back(
        { sm_helmet_vs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    helmet_cfg.shaderStages.push_back(
        { sm_helmet_fs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    helmet_cfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(HelmVertex),
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    helmet_cfg.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(HelmVertex, position) });
    helmet_cfg.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(HelmVertex, normal) });
    helmet_cfg.vertexAttributes.push_back(
        { .location = 2,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec2,
          .offset = offsetof(HelmVertex, uv) });
    helmet_cfg.vertexAttributes.push_back(
        { .location = 3,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec4,
          .offset = offsetof(HelmVertex, tangent) });
    // 关闭剔除，避免绕序/双面导致整模不可见；确认后可改回 BACK + CCW/CW
    helmet_cfg.cullMode = VK_CULL_MODE_NONE;
    helmet_cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline helmet_pipe;
    if (!helmet_pipe.create(ctx, helmet_pl, offscreen_render_pass, 0,
                            helmet_cfg)) {
        LUMEN_APP_LOG_ERROR("头盔 GraphicsPipeline 失败");
        return -1;
    }

    LUMEN_APP_LOG_INFO(
        "pbr 资源就绪: 顶点={} 索引={} 三角≈{} 材质={} 着色器 {} | {}",
        scene_asset.statsVertexCount, scene_asset.statsIndexCount,
        scene_asset.statsIndexCount / 3U, scene_asset.materials.size(),
        helmet_vs_path,
        helmet_fs_path);

    float sky_exposure { 0.35F };
    float ibl_strength { 1.0F };
    float emissive_scale { 3.0F };
    int point_light_count { lumen::render::PBR_LEGACY_POINT_LIGHT_CAP };
    float point_direct_strength { 1.15F };
    int helmet_debug_view { 0 };
    bool pbr_debug_tile_grid { true };
    /// Unity Scene 视口：Alt+左键环绕、Alt+中键平移、Alt+右键缩放；滚轮缩放；
    /// 右键拖拽环视；右键+WASD 飞行，E/Q 升降。
    struct CamState {
        glm::vec3 pivot { 0.0F, 0.0F, 0.0F };
        glm::vec3 eye { 0.0F, 0.0F, 12.0F };
        float yaw { -90.0F };
        float pitch { 0.0F };
        float orbit_distance { 12.0F };
        bool lmb_down { false };
        bool mmb_down { false };
        bool rmb_down { false };
    } cam {};
    {
        const glm::vec3 f0 = cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
        cam.eye = cam.pivot - f0 * cam.orbit_distance;
    }

    uint32_t pending_scene_w { scene_target.width() };
    uint32_t pending_scene_h { scene_target.height() };

    auto tex_env =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), v_env,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto tex_irr =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), v_irr,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto tex_pre =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), v_pre,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto tex_brdf =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), v_brdf,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    auto im_tex_or_ph = [&](const lumen::render::Texture *tex,
                            const lumen::render::Texture &ph) {
        const lumen::render::Texture &use = tex != nullptr ? *tex : ph;
        return reinterpret_cast<ImTextureID>(
            lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), use.view(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    };
    const lumen::render::Material &preview_mat = *scene_unique_materials[0];
    auto img_albedo =
        im_tex_or_ph(preview_mat.baseColorTex, pbr_placeholders.albedo());
    auto img_normal =
        im_tex_or_ph(preview_mat.normalTex, pbr_placeholders.normal());
    auto img_metallic = im_tex_or_ph(preview_mat.metallicRoughnessTex,
                                     pbr_placeholders.metallic_roughness());
    auto img_roughness = img_metallic;
    auto img_ao = im_tex_or_ph(preview_mat.occlusionTex, pbr_placeholders.ao());
    auto img_emissive =
        im_tex_or_ph(preview_mat.emissiveTex, pbr_placeholders.emissive());

    lumen::platform::EventPump pump;
    uint32_t frameIdx { 0 };
    bool running { true };
    bool need_recreate_swapchain { false };

    lumen::ui::ImGuiLayer imgui_layer;
    imgui_layer.attach(pump);

    bool cam_nav_active { false };
    bool last_scene_view_hovered { false };

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
        d.dispatch<lumen::platform::EventMouseButtonDown>(
            [&](lumen::platform::EventMouseButtonDown &e) {
                if (!cam_nav_active) {
                    return false;
                }
                if (e.button == lumen::platform::MouseButton::Left) {
                    cam.lmb_down = true;
                } else if (e.button == lumen::platform::MouseButton::Middle) {
                    cam.mmb_down = true;
                } else if (e.button == lumen::platform::MouseButton::Right) {
                    cam.rmb_down = true;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventMouseButtonUp>(
            [&](lumen::platform::EventMouseButtonUp &e) {
                if (e.button == lumen::platform::MouseButton::Left) {
                    cam.lmb_down = false;
                } else if (e.button == lumen::platform::MouseButton::Middle) {
                    cam.mmb_down = false;
                } else if (e.button == lumen::platform::MouseButton::Right) {
                    cam.rmb_down = false;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventMouseMove>(
            [&](lumen::platform::EventMouseMove &e) {
                if (!cam_nav_active) {
                    return false;
                }
                constexpr float k_orbit_sens { 0.22F };
                constexpr float k_pan_scale { 0.0035F };
                constexpr float k_zoom_drag { 0.008F };
                const bool alt = sdl_alt_down();
                const glm::vec3 f =
                    cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
                const glm::vec3 world_up { 0.0F, 1.0F, 0.0F };
                glm::vec3 right = glm::normalize(glm::cross(f, world_up));
                if (glm::length(right) < 1e-5F) {
                    right = glm::vec3 { 1.0F, 0.0F, 0.0F };
                }
                const glm::vec3 cam_up = glm::normalize(glm::cross(right, f));

                if (alt && cam.lmb_down) {
                    cam.yaw += e.deltaX * k_orbit_sens;
                    cam.pitch -= e.deltaY * k_orbit_sens;
                    cam.pitch = std::clamp(cam.pitch, -89.0F, 89.0F);
                    float d = glm::length(cam.pivot - cam.eye);
                    d = std::max(d, 0.05F);
                    const glm::vec3 fn =
                        cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
                    cam.eye = cam.pivot - fn * d;
                    cam.orbit_distance = d;
                } else if (alt && cam.mmb_down) {
                    const glm::vec3 pan =
                        (-right * e.deltaX + cam_up * e.deltaY) * k_pan_scale *
                        std::max(glm::length(cam.pivot - cam.eye), 1.0F);
                    cam.pivot += pan;
                    cam.eye += pan;
                } else if (alt && cam.rmb_down) {
                    float d = glm::length(cam.pivot - cam.eye);
                    d = std::max(d, 0.05F);
                    d *= 1.0F + e.deltaY * k_zoom_drag;
                    d = std::clamp(d, 0.45F, 200.0F);
                    const glm::vec3 fn =
                        cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
                    cam.eye = cam.pivot - fn * d;
                    cam.orbit_distance = d;
                } else if (cam.rmb_down && !alt) {
                    cam.yaw += e.deltaX * k_orbit_sens;
                    cam.pitch -= e.deltaY * k_orbit_sens;
                    cam.pitch = std::clamp(cam.pitch, -89.0F, 89.0F);
                }
                return false;
            });
        d.dispatch<lumen::platform::EventWindowResize>(
            [&](lumen::platform::EventWindowResize &) {
                need_recreate_swapchain = true;
                return false;
            });
    });

    constexpr uint64_t kAcquireTimeoutNs { 100'000'000 };
    constexpr uint64_t kFenceWaitNs { 16'000'000 };
    bool acquire_fail_logged { false };

    while (running) {
        cam_nav_active = last_scene_view_hovered;
        if (!pump.poll()) {
            LUMEN_APP_LOG_INFO("事件泵结束，退出主循环");
            break;
        }

        if (need_recreate_swapchain) {
            window.get_framebuffer_size(&window_width, &window_height);
            if (window_width <= 0 || window_height <= 0) {
                LUMEN_APP_LOG_WARN("Swapchain 重建跳过: 帧缓冲尺寸无效 {}x{}",
                                   window_width, window_height);
            } else if (!lumen::render::recreate_swapchain_resources(
                           ctx, swapchain, framebuffers, frameSync, renderPass,
                           static_cast<uint32_t>(window_width),
                           static_cast<uint32_t>(window_height), 3,
                           VK_NULL_HANDLE)) {
                LUMEN_APP_LOG_ERROR("recreate_swapchain_resources 失败 {}x{}",
                                    window_width, window_height);
            } else {
                lumen::ui::imgui_backend_set_min_image_count(
                    swapchain.image_count());
                LUMEN_APP_LOG_INFO("Swapchain 已重建 {}x{}", window_width,
                                   window_height);
            }
            need_recreate_swapchain = false;
            frameIdx = 0;
            continue;
        }

        while (!frameSync.wait_fence(frameIdx, kFenceWaitNs)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running) {
            break;
        }

        if (pending_scene_w >= 2U && pending_scene_h >= 2U &&
            (pending_scene_w != scene_target.width() ||
             pending_scene_h != scene_target.height())) {
            ctx.wait_idle();
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(scene_tex_id));
            scene_tex_id = static_cast<ImTextureID>(0);
            if (!scene_target.resize(pending_scene_w, pending_scene_h)) {
                LUMEN_APP_LOG_ERROR("场景离屏目标 resize 失败");
                running = false;
                break;
            }
            scene_tex_id = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    scene_sampler.handle(), scene_target.color_view(),
                    scene_target.color_sample_layout()));
        }

        const uint32_t scene_w = scene_target.width();
        const uint32_t scene_h = scene_target.height();

        if (swapchain.extent().width == 0 || swapchain.extent().height == 0) {
            SDL_Delay(16);
            continue;
        }

        const uint32_t img_index =
            swapchain.acquire_next_image(frameSync.image_available(frameIdx),
                                         VK_NULL_HANDLE, kAcquireTimeoutNs);
        if (img_index == UINT32_MAX) {
            if (!acquire_fail_logged) {
                LUMEN_APP_LOG_WARN(
                    "acquire_next_image 未取到图像 (超时/最小化/OUT_OF_DATE)，"
                    "跳过本帧 (连续失败仅打本条，恢复后重置)");
                acquire_fail_logged = true;
            }
            continue;
        }
        acquire_fail_logged = false;

        imgui_layer.begin_frame();

        if (ImGui::Begin("3D 视口")) {
            const ImVec2 vp_avail = ImGui::GetContentRegionAvail();
            pending_scene_w =
                (std::max)(2U,
                           static_cast<uint32_t>((std::max)(1.0F, vp_avail.x)));
            pending_scene_h =
                (std::max)(2U,
                           static_cast<uint32_t>((std::max)(1.0F, vp_avail.y)));
            if (scene_tex_id != static_cast<ImTextureID>(0)) {
                ImGui::Image(scene_tex_id, ImVec2(static_cast<float>(scene_w),
                                                  static_cast<float>(scene_h)));
                // 指针在画面上时滚轮缩放（悬停 Image 时 WantCaptureMouse 为
                // true， 若在 SDL 事件里用 imgui_wants_mouse 过滤会永远进不来）
                if (ImGui::IsItemHovered()) {
                    constexpr float k_orbit_min { 0.45F };
                    constexpr float k_orbit_max { 80.0F };
                    constexpr float k_wheel_scale { 0.14F };
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0F) {
                        float d = glm::length(cam.pivot - cam.eye);
                        d = (std::max)(d, 0.05F);
                        d -= wheel * k_wheel_scale;
                        d = std::clamp(d, k_orbit_min, k_orbit_max);
                        const glm::vec3 fn =
                            cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
                        cam.eye = cam.pivot - fn * d;
                        cam.orbit_distance = d;
                    }
                }
            }
            last_scene_view_hovered =
                ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
            if (pbr_debug_tile_grid &&
                scene_tex_id != static_cast<ImTextureID>(0)) {
                constexpr int k_dbg_cols = 4;
                constexpr int k_dbg_rows = 4;
                ImDrawList *const dl = ImGui::GetWindowDrawList();
                const ImU32 lab_col = IM_COL32(255, 255, 120, 255);
                const ImU32 sh_col = IM_COL32(0, 0, 0, 180);
                static constexpr const char
                    *k_dbg_zh[k_dbg_cols * k_dbg_rows] = {
                        "PBR 完整", "几何法线",   "法线贴图",  "反照率",
                        "金属度",   "粗糙度",     "AO",        "漫反射 IBL",
                        "镜面 IBL", "Irradiance", "Prefilter", "N·V",
                        "F0",       "BRDF LUT",   "自发光",    "Base Color",
                    };
                const ImVec2 p0 = ImGui::GetItemRectMin();
                const ImVec2 p1 = ImGui::GetItemRectMax();
                const float disp_w = p1.x - p0.x;
                const float disp_h = p1.y - p0.y;
                for (int ti = 0; ti < k_dbg_cols * k_dbg_rows; ++ti) {
                    const int col = ti % k_dbg_cols;
                    const int row = ti / k_dbg_cols;
                    const float x =
                        p0.x + (disp_w / static_cast<float>(k_dbg_cols)) *
                                   static_cast<float>(col);
                    const float y =
                        p0.y + (disp_h / static_cast<float>(k_dbg_rows)) *
                                   static_cast<float>(row);
                    ImVec2 a(x + 3.0F, y + 4.0F);
                    ImVec2 b(x + 2.0F, y + 3.0F);
                    dl->AddText(b, sh_col, k_dbg_zh[ti]);
                    dl->AddText(a, lab_col, k_dbg_zh[ti]);
                }
            }
        } else {
            last_scene_view_hovered = false;
        }
        ImGui::End();

        if (ImGui::Begin("IBL 预览")) {
            ImGui::TextUnformatted(
                "与 Unity Scene "
                "类似：Alt+左键环绕、Alt+中键平移、Alt+右键缩放；"
                "视口内滚轮缩放；右键拖拽环视；右键按住时 WASD 飞行，E 上升 Q "
                "下降");
            if (ImGui::SliderFloat("视距（枢轴—相机）", &cam.orbit_distance,
                                   0.45F, 80.0F, "%.2f")) {
                const glm::vec3 fn =
                    cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
                cam.eye = cam.pivot - fn * cam.orbit_distance;
            }
            ImGui::SliderFloat("天空曝光", &sky_exposure, 0.05F, 4.0F, "%.2f");
            ImGui::SliderFloat("IBL 强度", &ibl_strength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("自发光倍率", &emissive_scale, 0.0F, 12.0F,
                               "%.1f");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "点光源（GGX 直射，仅「PBR 完整 / 分屏首格」）");
            ImGui::SliderInt("点光数量", &point_light_count, 0,
                             lumen::render::PBR_FORWARD_MAX_LIGHTS);
            ImGui::SliderFloat("点光强度", &point_direct_strength, 0.0F, 6.0F,
                               "%.2f");
            ImGui::Separator();
            ImGui::Checkbox("分屏光照调试 (4×4，场景)", &pbr_debug_tile_grid);
            ImGui::TextUnformatted("关闭分屏后可用单项模式：");
            ImGui::BeginDisabled(pbr_debug_tile_grid);
            ImGui::RadioButton("PBR（最终）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_NONE);
            ImGui::RadioButton("法线（世界空间，着色）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_NORMAL_WS);
            ImGui::RadioButton("法线（切线空间，贴图）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_NORMAL_TS);
            ImGui::RadioButton("深度（距相机归一化）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_DEPTH);
            ImGui::RadioButton("反照率", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_ALBEDO);
            ImGui::RadioButton("金属度", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_METALLIC);
            ImGui::RadioButton("粗糙度", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_ROUGHNESS);
            ImGui::RadioButton("AO", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_AO);
            ImGui::RadioButton("直射漫反射", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_DIRECT_DIFFUSE);
            ImGui::RadioButton("直射镜面", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_DIRECT_SPECULAR);
            ImGui::RadioButton("IBL 漫反射", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_IBL_DIFFUSE);
            ImGui::RadioButton("IBL 镜面", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_IBL_SPECULAR);
            ImGui::RadioButton("自发光", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_EMISSIVE);
            ImGui::RadioButton("合成（无 IBL）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_FINAL_NO_IBL);
            ImGui::RadioButton("合成（无直射）", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_FINAL_NO_DIRECT);
            ImGui::RadioButton("热力：直射能量", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_HEAT_LIGHT_INTENSITY);
            ImGui::RadioButton("热力：N·L 最大", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_HEAT_NDOTL);
            ImGui::RadioButton("热力：光源数量", &helmet_debug_view,
                               lumen::render::PBR_DEBUG_HEAT_LIGHT_COUNT);
            char dbg_line[64];
            std::snprintf(dbg_line, sizeof dbg_line, "当前模式号: %d",
                          helmet_debug_view);
            ImGui::TextUnformatted(dbg_line);
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::TextUnformatted("Environment (+X)");
            ImGui::Image(tex_env, ImVec2(220, 220));
            ImGui::TextUnformatted("Irradiance");
            ImGui::Image(tex_irr, ImVec2(160, 160));
            ImGui::TextUnformatted("Prefilter");
            ImGui::Image(tex_pre, ImVec2(160, 160));
            ImGui::TextUnformatted("BRDF LUT");
            ImGui::Image(tex_brdf, ImVec2(200, 200));
        }
        ImGui::End();

        if (last_scene_view_hovered && cam.rmb_down && !sdl_alt_down()) {
            apply_scene_view_fly_wasd(cam.eye, cam.yaw, cam.pitch,
                                      ImGui::GetIO().DeltaTime);
        }

        if (ImGui::Begin("Sponza 贴图（首材质）")) {
            const ImVec2 thumb(200.F, 200.F);
            ImGui::TextUnformatted("Base Color (sRGB)");
            ImGui::Image(img_albedo, thumb);
            ImGui::TextUnformatted("Normal");
            ImGui::Image(img_normal, thumb);
            ImGui::TextUnformatted("Metallic/Roughness (glTF 合并)");
            ImGui::Image(img_metallic, thumb);
            ImGui::TextUnformatted("Roughness（同左 G 通道）");
            ImGui::Image(img_roughness, thumb);
            ImGui::TextUnformatted("Ambient Occlusion");
            ImGui::Image(img_ao, thumb);
            ImGui::TextUnformatted("Emissive");
            ImGui::Image(img_emissive, thumb);
        }
        ImGui::End();

        auto &cmd_buf = cmd_buffers[frameIdx];
        if (!cmd_buf.reset()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::reset 失败 frameIdx={}",
                                frameIdx);
            continue;
        }
        if (!cmd_buf.begin()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::begin 失败 frameIdx={}",
                                frameIdx);
            continue;
        }

        std::array<VkClearValue, 2> scene_clears {};
        scene_clears[0].color = { { 0.08F, 0.08F, 0.1F, 1.0F } };
        scene_clears[1].depthStencil = { 1.0F, 0 };

        VkRenderPassBeginInfo scene_rp {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        scene_rp.renderPass = scene_target.render_pass();
        scene_rp.framebuffer = scene_target.framebuffer();
        scene_rp.renderArea.offset = { 0, 0 };
        scene_rp.renderArea.extent = scene_target.extent();
        scene_rp.clearValueCount = static_cast<uint32_t>(scene_clears.size());
        scene_rp.pClearValues = scene_clears.data();

        vkCmdBeginRenderPass(cmd_buf.handle(), &scene_rp,
                             VK_SUBPASS_CONTENTS_INLINE);

        {
            const VkExtent2D ext = scene_target.extent();
            const float wf = static_cast<float>(ext.width);
            const float hf = static_cast<float>(ext.height);

            const glm::vec3 forward =
                cam_forward_yaw_pitch_deg(cam.yaw, cam.pitch);
            const glm::vec3 eye = cam.eye;
            const glm::mat4 view =
                glm::lookAt(eye, eye + forward, glm::vec3(0.0F, 1.0F, 0.0F));
            const glm::mat4 sky_v = glm::mat4(glm::mat3(view));

            VkCommandBuffer cb = cmd_buf.handle();
            const auto material_ds_index =
                [&](const lumen::render::Material *m) -> uint32_t {
                if (m == nullptr) {
                    return 0U;
                }
                const auto it = scene_material_to_ds_index.find(m);
                if (it == scene_material_to_ds_index.end()) {
                    return 0U;
                }
                return it->second;
            };

            if (!pbr_debug_tile_grid) {
                const float aspect =
                    wf / static_cast<float>((std::max)(1U, ext.height));
                glm::mat4 proj = glm::perspective(glm::radians(55.0F), aspect,
                                                  0.05F, 120.0F);
                proj[1][1] *= -1.0F;

                for (uint32_t mi = 0; mi < mat_count_u32; ++mi) {
                    lumen::render::PbrMaterialUbo mu {};
                    lumen::render::pack_pbr_material_ubo(
                        mu, *scene_unique_materials[mi], emissive_scale);
                    sponza_material_ubos[mi].update(mu);
                }

                lumen::render::PbrFrameUbo frame_u {};
                lumen::render::pack_pbr_frame_ubo(
                    frame_u, view, proj, eye, sky_exposure, ibl_strength,
                    k_prefilter_max_lod, 0.0F, helmet_debug_view);
                helmet_frame_ubos[frameIdx].update(frame_u);

                lumen::render::PbrLightUbo light_u {};
                lumen::render::fill_pbr_light_ubo_default_points(
                    light_u, point_light_count, point_direct_strength);
                helmet_light_ubos[frameIdx].update(light_u);

                VkViewport vp { 0.0F, 0.0F, wf, hf, 0.0F, 1.0F };
                vkCmdSetViewport(cb, 0, 1, &vp);
                VkRect2D scissor { { 0, 0 }, ext };
                vkCmdSetScissor(cb, 0, 1, &scissor);

                SkyPush sky_push {};
                sky_push.sky_mvp = proj * sky_v;
                sky_push.params = glm::vec4(sky_exposure, 0.0F, 0.0F, 0.0F);

                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  sky_pipe.handle());
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        sky_pl.handle(), 0, 1, &sky_ds, 0,
                                        nullptr);
                vkCmdPushConstants(
                    cb, sky_pl.handle(),
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, static_cast<uint32_t>(sizeof(SkyPush)), &sky_push);
                VkDeviceSize sky_off { 0 };
                VkBuffer sky_vbh = sky_vbuf.handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &sky_vbh, &sky_off);
                vkCmdDraw(cb, 36, 1, 0, 0);

                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  helmet_pipe.handle());
                VkDeviceSize hv_off { 0 };
                VkBuffer hvb = scene_asset.vertexBuffer->handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &hvb, &hv_off);
                vkCmdBindIndexBuffer(cb, scene_asset.indexBuffer->handle(), 0,
                                     scene_asset.indexBuffer->vk_index_type());
                std::vector<lumen::scene::RenderItem> sponza_render_items;
                lumen::scene::collect_render_items(gltf_ecs_scene.registry(),
                                                   sponza_render_items);
                lumen::scene::sort_render_items_for_minimal_state_change(
                    sponza_render_items);
                lumen::render::PbrForwardRecordContext pbr_rec_ctx {};
                pbr_rec_ctx.command_buffer = cb;
                pbr_rec_ctx.pipeline_layout = helmet_pl.handle();
                pbr_rec_ctx.frame_descriptor_set = helmet_frame_ds[frameIdx];
                pbr_rec_ctx.light_descriptor_set = helmet_light_ds[frameIdx];
                pbr_rec_ctx.object_descriptor_set = helmet_object_ds;
                pbr_rec_ctx.default_material = nullptr;
                pbr_rec_ctx.object_dynamic_stride =
                    static_cast<std::uint32_t>(helmet_obj_stride);
                pbr_rec_ctx.bind_vertex_and_index_buffers_per_item = false;
                lumen::render::record_pbr_forward_render_items(
                    pbr_rec_ctx, sponza_render_items, helmet_object_ubo,
                    [&](const lumen::render::Material *m) {
                        const uint32_t mids = material_ds_index(m);
                        return sponza_material_ds[mids];
                    });
            } else {
                constexpr int k_dbg_tile_cols = 4;
                constexpr int k_dbg_tile_rows = 4;
                constexpr int k_dbg_tile_count =
                    k_dbg_tile_cols * k_dbg_tile_rows;
                const float cw = wf / static_cast<float>(k_dbg_tile_cols);
                const float ch = hf / static_cast<float>(k_dbg_tile_rows);
                VkBuffer hvb = scene_asset.vertexBuffer->handle();
                VkDeviceSize hv_off_zero { 0 };

                for (int ti = 0; ti < k_dbg_tile_count; ++ti) {
                    const int col = ti % k_dbg_tile_cols;
                    const int row = ti / k_dbg_tile_cols;
                    const float aspect =
                        cw / static_cast<float>((std::max)(1.0F, ch));
                    glm::mat4 proj = glm::perspective(glm::radians(55.0F),
                                                      aspect, 0.05F, 120.0F);
                    proj[1][1] *= -1.0F;

                    for (uint32_t mi = 0; mi < mat_count_u32; ++mi) {
                        lumen::render::PbrMaterialUbo mu_tile {};
                        lumen::render::pack_pbr_material_ubo(
                            mu_tile, *scene_unique_materials[mi],
                            emissive_scale);
                        sponza_material_ubos[mi].update(mu_tile);
                    }

                    lumen::render::PbrFrameUbo frame_tile {};
                    lumen::render::pack_pbr_frame_ubo(
                        frame_tile, view, proj, eye, sky_exposure, ibl_strength,
                        k_prefilter_max_lod, 0.0F,
                        lumen::render::PBR_FORWARD_DEBUG_TILE_MODES.at(
                            static_cast<std::size_t>(ti)));
                    helmet_frame_ubos[frameIdx].update(frame_tile);

                    lumen::render::PbrLightUbo light_tile {};
                    lumen::render::fill_pbr_light_ubo_default_points(
                        light_tile, point_light_count, point_direct_strength);
                    helmet_light_ubos[frameIdx].update(light_tile);

                    VkViewport vp {};
                    vp.x = static_cast<float>(col) * cw;
                    // 与「3D 视口」里 ImGui 角标自上而下（row 0
                    // 在上）一致；勿用 (rows-1-row)，否则常见 ImGui Vulkan
                    // 纹理取向下整网与文字会上下对不齐。
                    vp.y = static_cast<float>(row) * ch;
                    vp.width = cw;
                    vp.height = ch;
                    vp.minDepth = 0.0F;
                    vp.maxDepth = 1.0F;
                    vkCmdSetViewport(cb, 0, 1, &vp);
                    const int32_t sx = static_cast<int32_t>(std::lround(vp.x));
                    const int32_t sy = static_cast<int32_t>(std::lround(vp.y));
                    const uint32_t sc_w = static_cast<uint32_t>(
                        (std::max)(1L, std::lround(static_cast<double>(cw))));
                    const uint32_t sc_h = static_cast<uint32_t>(
                        (std::max)(1L, std::lround(static_cast<double>(ch))));
                    VkRect2D scissor { { sx, sy }, { sc_w, sc_h } };
                    vkCmdSetScissor(cb, 0, 1, &scissor);

                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      helmet_pipe.handle());
                    vkCmdBindVertexBuffers(cb, 0, 1, &hvb, &hv_off_zero);
                    vkCmdBindIndexBuffer(
                        cb, scene_asset.indexBuffer->handle(), 0,
                        scene_asset.indexBuffer->vk_index_type());
                    std::vector<lumen::scene::RenderItem>
                        sponza_render_items_dbg;
                    lumen::scene::collect_render_items(
                        gltf_ecs_scene.registry(), sponza_render_items_dbg);
                    lumen::scene::sort_render_items_for_minimal_state_change(
                        sponza_render_items_dbg);
                    lumen::render::PbrForwardRecordContext pbr_dbg_ctx {};
                    pbr_dbg_ctx.command_buffer = cb;
                    pbr_dbg_ctx.pipeline_layout = helmet_pl.handle();
                    pbr_dbg_ctx.frame_descriptor_set = helmet_frame_ds[frameIdx];
                    pbr_dbg_ctx.light_descriptor_set = helmet_light_ds[frameIdx];
                    pbr_dbg_ctx.object_descriptor_set = helmet_object_ds;
                    pbr_dbg_ctx.default_material = nullptr;
                    pbr_dbg_ctx.object_dynamic_stride =
                        static_cast<std::uint32_t>(helmet_obj_stride);
                    pbr_dbg_ctx.bind_vertex_and_index_buffers_per_item = false;
                    lumen::render::record_pbr_forward_render_items(
                        pbr_dbg_ctx, sponza_render_items_dbg, helmet_object_ubo,
                        [&](const lumen::render::Material *m) {
                            const uint32_t mids = material_ds_index(m);
                            return sponza_material_ds[mids];
                        });
                }
            }
        }

        vkCmdEndRenderPass(cmd_buf.handle());

        VkClearValue swap_clear {};
        swap_clear.color = { { 0.07F, 0.08F, 0.11F, 1.0F } };
        VkRenderPassBeginInfo swap_rp {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        swap_rp.renderPass = renderPass.handle();
        swap_rp.framebuffer = framebuffers.get(img_index);
        swap_rp.renderArea.offset = { 0, 0 };
        swap_rp.renderArea.extent = swapchain.extent();
        swap_rp.clearValueCount = 1;
        swap_rp.pClearValues = &swap_clear;
        vkCmdBeginRenderPass(cmd_buf.handle(), &swap_rp,
                             VK_SUBPASS_CONTENTS_INLINE);

        {
            const VkExtent2D swap_ext = swapchain.extent();
            VkViewport fb_vp {};
            fb_vp.x = 0.0F;
            fb_vp.y = 0.0F;
            fb_vp.width = static_cast<float>(swap_ext.width);
            fb_vp.height = static_cast<float>(swap_ext.height);
            fb_vp.minDepth = 0.0F;
            fb_vp.maxDepth = 1.0F;
            vkCmdSetViewport(cmd_buf.handle(), 0, 1, &fb_vp);
            VkRect2D fb_sc { { 0, 0 }, swap_ext };
            vkCmdSetScissor(cmd_buf.handle(), 0, 1, &fb_sc);
        }

        imgui_layer.end_frame(cmd_buf.handle());

        vkCmdEndRenderPass(cmd_buf.handle());
        if (!cmd_buf.end()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::end 失败 frameIdx={}",
                                frameIdx);
            continue;
        }

        VkSemaphore wait_sem = frameSync.image_available(frameIdx);
        VkSemaphore signal_sem = frameSync.render_finished(img_index);
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkCommandBuffer submit_cb = cmd_buf.handle();
        VkSubmitInfo sub { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &wait_sem;
        sub.pWaitDstStageMask = &wait_stage;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &submit_cb;
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &signal_sem;

        if (!frameSync.reset_fence(frameIdx)) {
            LUMEN_APP_LOG_ERROR("vkResetFences 失败 frameIdx={}", frameIdx);
            continue;
        }
        const VkResult submit_rc = vkQueueSubmit(
            ctx.graphics_queue(), 1, &sub, frameSync.in_flight_fence(frameIdx));
        if (submit_rc != VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("vkQueueSubmit 失败 result={}",
                                static_cast<int>(submit_rc));
            continue;
        }

        const VkResult pr =
            swapchain.present(ctx.present_queue(), img_index, signal_sem);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
            LUMEN_APP_LOG_WARN("present OUT_OF_DATE，将重建 Swapchain");
            need_recreate_swapchain = true;
        } else if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) {
            LUMEN_APP_LOG_ERROR("present 失败 result={}", static_cast<int>(pr));
        }

        frameIdx = (frameIdx + 1) % 3;
    }

    vkDeviceWaitIdle(dev);
    if (scene_tex_id != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(scene_tex_id));
    }
    lumen::ui::imgui_backend_shutdown();
    // 单例缓存的 SceneMesh / 材质 / 贴图仍持有 VMA；须在销毁 Context 前释放
    lumen::asset::AssetRegistry::instance().clear_all();

    destroy_view(dev, v_env);
    destroy_view(dev, v_irr);
    destroy_view(dev, v_pre);

    std::vector<lumen::render::CommandBuffer> free_bufs;
    for (auto &c : cmd_buffers) {
        free_bufs.push_back(std::move(c));
    }
    cmdPool.free(free_bufs);

    return 0;
}

int main(int argc, char **argv) {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int result = run_pbr(argc, argv);
    lumen::core::Logger::shutdown();
    return result;
}
