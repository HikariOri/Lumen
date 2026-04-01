/**
 * @file main.cpp
 * @brief PBR — IBL 烘焙、HDR 天空盒、Sponza（glTF 多 primitive / 多材质）
 */

#include "ibl_bake.hpp"

#include "core/logger.hpp"
#include "core/path.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/frame_sync.hpp"
#include "render/material/material.hpp"
#include "render/material/pbr_forward_ubo.hpp"
#include "render/material/pbr_material_bind.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/pick_id_render_target.hpp"
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/surface.hpp"
#include "render/swapchain.hpp"
#include "gltf/gltf_scene_mesh.hpp"
#include "scene/mesh.hpp"
#include "scene/render_item.hpp"
#include "scene/scene.hpp"
#include "scene/submesh.hpp"
#include "scene/scene_camera.hpp"
#include "scene/scene_orbit_controller.hpp"
#include "scene/transform.hpp"
#include "scene/components.hpp"
#include "scene/id_lookup.hpp"
#include "scene/pick.hpp"
#include "ui/editor_selection.hpp"
#include "ui/gizmo.hpp"
#include "ui/gpu_capabilities_panel.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"
#include "ui/log_panel.hpp"
#include "ui/panel.hpp"
#include "ui/scene_hierarchy_panel.hpp"
#include "ui/scene_inspector_panel.hpp"
#include "ui/scene_viewport_panel.hpp"
#include "ui/texture_view_panel.hpp"

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr const char *HDR_REL_PATH {
    "assets/environment_maps/meadow_2_2k.hdr"
};

constexpr const char *SPONZA_GLTF_REL {
    // "assets/models/Sponza/glTF/Sponza.gltf"
    // "assets/models/rex_master/scene.gltf"
    // "assets/models/chisa_wuthering_waves/scene.gltf"
    // "assets/models/chisa_wuthering_waves/scene.gltf"
    // "assets/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"
    "assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf"
};

/// 「Scene」视口 ImGuizmo 模式（须在本帧 `imgui_scene_viewport_panel` 之前写入）
int scene_gizmo_operation { static_cast<int>(ImGuizmo::TRANSLATE) };

// 单位立方体（与 ibl_bake 天空捕获一致）
constexpr std::array<float, 36U * 3U> SKY_CUBE_POSITIONS { {
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
    glm::mat4 skyMvp {};
    glm::vec4 params {}; // x: exposure
};

/// 与 glTF PBR 上传 / `pbr_forward.vert` 交错顶点布局一致（管线 vertex
/// 描述用）
struct HelmVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
    glm::vec4 tangent { 1.0F, 0.0F, 0.0F, 1.0F };
};

[[nodiscard]] VkDescriptorSet vk_descriptor_set_for_pbr_material(
    const lumen::render::Material *material,
    const std::vector<lumen::render::Material> &materialsStorage,
    const std::vector<VkDescriptorSet> &materialSets) {
    if (materialSets.empty()) {
        return VK_NULL_HANDLE;
    }
    if (material == nullptr || materialsStorage.empty()) {
        return materialSets[0];
    }
    const ptrdiff_t indexInStorage = material - materialsStorage.data();
    if (indexInStorage < 0 ||
        static_cast<size_t>(indexInStorage) >= materialsStorage.size()) {
        return materialSets[0];
    }
    return materialSets[static_cast<size_t>(indexInStorage)];
}

/** @brief 立方体贴图某一面的 2D 视图（@a base_array_layer 0…5 对应 +X −X +Y −Y
 * +Z −Z） */
[[nodiscard]] VkImageView
create_cubemap_face_2d_view(VkDevice device, VkImage img, VkFormat format,
                            uint32_t mipLevel, uint32_t baseArrayLayer,
                            const char *label) {
    VkImageViewCreateInfo createInfo {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };
    createInfo.image = img;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = mipLevel;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    createInfo.subresourceRange.layerCount = 1;
    VkImageView outView { VK_NULL_HANDLE };
    const VkResult createResult =
        vkCreateImageView(device, &createInfo, nullptr, &outView);
    if (createResult != VK_SUCCESS) {
        LUMEN_APP_LOG_ERROR("vkCreateImageView 失败 ({}) result={}", label,
                            static_cast<int>(createResult));
        return VK_NULL_HANDLE;
    }
    return outView;
}

void destroy_image_view(VkDevice device, VkImageView imageView) {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }
}

void imgui_draw_cubemap_face_grid(const std::array<ImTextureID, 6> &faces,
                                  const ImVec2 &faceSize) {
    static constexpr const char *CUBEMAP_FACE_LABELS[6] = { "+X", "-X", "+Y",
                                                            "-Y", "+Z", "-Z" };
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int faceIndex = row * 3 + col;
            ImGui::BeginGroup();
            ImGui::TextUnformatted(CUBEMAP_FACE_LABELS[faceIndex]);
            ImGui::Image(faces[static_cast<size_t>(faceIndex)], faceSize);
            ImGui::EndGroup();
            if (col < 2) {
                ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
            }
        }
    }
}

} // namespace

static int run_pbr(int, char **) {
    lumen::platform::Window window;
    lumen::platform::WindowConfig windowConfig;
    windowConfig.title = "Lumen — PBR DamagedHelmet + IBL";
    windowConfig.width = 1280;
    windowConfig.height = 800;
    windowConfig.icon_path =
        lumen::core::get_resource_path("assets/icons/哈士奇.png");
    if (!window.create(windowConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    lumen::render::ContextConfig contextConfig;
    lumen::render::Context ctx;
    if (!ctx.init_instance(contextConfig, window)) {
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

    int windowWidth { 0 };
    int windowHeight { 0 };
    window.get_framebuffer_size(&windowWidth, &windowHeight);
    if (windowWidth <= 0 || windowHeight <= 0) {
        LUMEN_APP_LOG_WARN("窗口帧缓冲尺寸无效 {}x{}，使用配置 {}x{}",
                           windowWidth, windowHeight, windowConfig.width,
                           windowConfig.height);
        windowWidth = windowConfig.width;
        windowHeight = windowConfig.height;
    }

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(),
                          static_cast<uint32_t>(windowWidth),
                          static_cast<uint32_t>(windowHeight))) {
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    // 交换链：仅 ImGui 合成（无深度）
    lumen::render::RenderPassConfig renderPassConfig;
    renderPassConfig.useDepth = false;
    renderPassConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), renderPassConfig)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             VK_NULL_HANDLE)) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig offscreenRpConfig;
    offscreenRpConfig.useDepth = true;
    offscreenRpConfig.colorAttachment.format = swapchain.image_format();
    offscreenRpConfig.colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::RenderPass offscreenRenderPass;
    if (!offscreenRenderPass.create(ctx.device(), offscreenRpConfig)) {
        LUMEN_APP_LOG_ERROR("离屏 RenderPass 创建失败");
        return -1;
    }

    lumen::render::OffscreenRenderTarget sceneTarget;
    {
        lumen::render::OffscreenRenderTargetConfig sceneCfg;
        sceneCfg.width =
            static_cast<uint32_t>((std::max)(2, windowWidth * 3 / 4));
        sceneCfg.height =
            static_cast<uint32_t>((std::max)(2, windowHeight * 3 / 4));
        sceneCfg.format = swapchain.image_format();
        sceneCfg.useDepth = true;
        sceneCfg.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!sceneTarget.create(ctx, sceneCfg, &offscreenRenderPass)) {
            LUMEN_APP_LOG_ERROR("场景离屏渲染目标创建失败");
            return -1;
        }
    }

    lumen::render::PickIdRenderTarget pickIdTarget;
    if (!pickIdTarget.create(ctx, sceneTarget.width(), sceneTarget.height())) {
        LUMEN_APP_LOG_ERROR("PickIdRenderTarget 创建失败");
        return -1;
    }

    lumen::render::OffscreenRenderTarget debugTileTarget;
    {
        lumen::render::OffscreenRenderTargetConfig debugTargetCfg;
        debugTargetCfg.width =
            static_cast<uint32_t>((std::max)(2, windowWidth * 3 / 4));
        debugTargetCfg.height =
            static_cast<uint32_t>((std::max)(2, windowHeight * 3 / 4));
        debugTargetCfg.format = swapchain.image_format();
        debugTargetCfg.useDepth = true;
        debugTargetCfg.colorFinalLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!debugTileTarget.create(ctx, debugTargetCfg,
                                    &offscreenRenderPass)) {
            LUMEN_APP_LOG_ERROR("分屏调试用离屏目标创建失败");
            return -1;
        }
    }

    lumen::render::OffscreenRenderTarget idMapVizTarget;
    {
        lumen::render::OffscreenRenderTargetConfig vizCfg;
        vizCfg.width =
            static_cast<uint32_t>((std::max)(2, windowWidth * 3 / 4));
        vizCfg.height =
            static_cast<uint32_t>((std::max)(2, windowHeight * 3 / 4));
        vizCfg.format = swapchain.image_format();
        vizCfg.useDepth = true;
        vizCfg.colorFinalLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!idMapVizTarget.create(ctx, vizCfg, &offscreenRenderPass)) {
            LUMEN_APP_LOG_ERROR("ID Map 可视化离屏目标创建失败");
            return -1;
        }
    }

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    const std::string hdrPath = lumen::core::get_resource_path(HDR_REL_PATH);
    LUMEN_APP_LOG_INFO("IBL 烘焙 HDR: {}", hdrPath);

    pbr::IblTextures ibl {};
    std::string bakeErr;
    if (!pbr::bake_ibl(ctx, cmdPool, ctx.graphics_queue(), hdrPath.c_str(), ibl,
                       bakeErr)) {
        LUMEN_APP_LOG_ERROR("IBL 烘焙失败: {}", bakeErr);
        return -1;
    }

    const float PREFILTER_MAX_LOD = static_cast<float>(
        ibl.prefilter.mip_levels() > 0 ? ibl.prefilter.mip_levels() - 1 : 0);

    VkDevice dev = ctx.device();
    const VkFormat IBL_FORMAT = VK_FORMAT_R32G32B32A32_SFLOAT;

    std::array<VkImageView, 6> envFaceViews {};
    std::array<VkImageView, 6> irrFaceViews {};
    std::array<VkImageView, 6> preFaceViews {};
    for (uint32_t face = 0; face < 6; ++face) {
        char envLabel[48];
        std::snprintf(envLabel, sizeof envLabel, "IBL environment face %u",
                      face);
        envFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.environment.image(), IBL_FORMAT, 0, face, envLabel);
        char irrLabel[48];
        std::snprintf(irrLabel, sizeof irrLabel, "IBL irradiance face %u",
                      face);
        irrFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.irradiance.image(), IBL_FORMAT, 0, face, irrLabel);
        char preLabel[48];
        std::snprintf(preLabel, sizeof preLabel, "IBL prefilter face %u", face);
        preFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.prefilter.image(), IBL_FORMAT, 0, face, preLabel);
    }
    VkImageView brdfLutView = ibl.brdf_lut.view();

    auto allSixValid = [](const std::array<VkImageView, 6> &a) -> bool {
        for (VkImageView v : a) {
            if (v == VK_NULL_HANDLE) {
                return false;
            }
        }
        return true;
    };
    const bool envViewsOk = allSixValid(envFaceViews);
    const bool irrViewsOk = allSixValid(irrFaceViews);
    const bool preViewsOk = allSixValid(preFaceViews);
    if (!envViewsOk || !irrViewsOk || !preViewsOk ||
        brdfLutView == VK_NULL_HANDLE) {
        LUMEN_APP_LOG_ERROR(
            "ImGui 预览用 ImageView 无效: env6={} irr6={} pre6={} brdf={}",
            envViewsOk, irrViewsOk, preViewsOk, brdfLutView != VK_NULL_HANDLE);
        for (VkImageView fv : envFaceViews) {
            destroy_image_view(dev, fv);
        }
        for (VkImageView fv : irrFaceViews) {
            destroy_image_view(dev, fv);
        }
        for (VkImageView fv : preFaceViews) {
            destroy_image_view(dev, fv);
        }
        return -1;
    }

    const std::string sponzaPath =
        lumen::core::get_resource_path(SPONZA_GLTF_REL);
    if (!std::filesystem::exists(sponzaPath)) {
        LUMEN_APP_LOG_ERROR("未找到 Sponza glTF: {}（含同级纹理）", sponzaPath);
        return -1;
    }

    VkQueue gq = ctx.graphics_queue();

    lumen::render::PbrPlaceholderTextures pbrPlaceholders;
    if (!pbrPlaceholders.create(ctx, gq, cmdPool) ||
        !pbrPlaceholders.is_complete()) {
        LUMEN_APP_LOG_ERROR("PBR 占位贴图创建失败");
        return -1;
    }

    lumen::scene::GltfSceneMesh sponzaAsset {};
    lumen::scene::GltfSceneMeshLoadOptions sponzaLoadOpts {};
    sponzaLoadOpts.recenterToOrigin = true;
    sponzaLoadOpts.uniformScaleMaxAxis = 1.8F;
    std::string sponzaLoadErr;
    if (!lumen::scene::load_gltf_scene_mesh(ctx, gq, cmdPool, sponzaPath,
                                            sponzaAsset, sponzaLoadOpts,
                                            &sponzaLoadErr)) {
        LUMEN_APP_LOG_ERROR("Sponza 加载失败: {}",
                            sponzaLoadErr.empty() ? "unknown" : sponzaLoadErr);
        return -1;
    }
    if (sponzaAsset.materials.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 无材质");
        return -1;
    }

    std::vector<lumen::render::Material> &pbrMaterials = sponzaAsset.materials;
    lumen::scene::Mesh *sponzaInspectorMesh = nullptr;
    for (lumen::scene::Mesh &m : sponzaAsset.model) {
        if (!m.primitives.empty()) {
            sponzaInspectorMesh = &m;
            break;
        }
    }
    if (sponzaInspectorMesh == nullptr) {
        LUMEN_APP_LOG_ERROR("Sponza 无 mesh/primitive");
        return -1;
    }

    lumen::scene::Scene editorScene;
    lumen::ui::EditorSelection editorSelection {};
    const entt::entity sponzaRootEntity = editorScene.create_entity("Sponza");
    editorScene.registry().emplace<lumen::scene::MeshRendererComponent>(
        sponzaRootEntity,
        lumen::scene::MeshRendererComponent{ .mesh = sponzaInspectorMesh });
    editorSelection.entity = sponzaRootEntity;
    /// 为层级与 SubMesh 检视挂子实体（每 primitive 一条）；主渲染对首个检视 mesh
    /// 走 SubMesh 路径以便子实体变换可见。
    (void)lumen::scene::attach_submesh_children(
        editorScene, sponzaRootEntity, *sponzaInspectorMesh, "SponzaSub");

    uint32_t sponzaDrawSlots = 0;
    for (const lumen::scene::Mesh &sm : sponzaAsset.model) {
        for (const lumen::scene::Primitive &sp : sm.primitives) {
            if (sp.is_drawable()) {
                ++sponzaDrawSlots;
            }
        }
    }
    const std::size_t helmetObjStride =
        lumen::render::pbr_object_ubo_dynamic_stride(static_cast<std::size_t>(
            ctx.physical_device_properties()
                .limits.minUniformBufferOffsetAlignment));
    const VkDeviceSize helmetObjectUboBytes =
        static_cast<VkDeviceSize>(helmetObjStride) *
        static_cast<VkDeviceSize>((std::max)(sponzaDrawSlots, 1u));

    LUMEN_APP_LOG_INFO(
        "Sponza: 顶点={} 索引={} 三角≈{} mesh={} 可绘制 primitive={} 材质={} GPU "
        "贴图实例={}",
        sponzaAsset.statsVertexCount, sponzaAsset.statsIndexCount,
        sponzaAsset.statsIndexCount / 3U, sponzaAsset.model.size(), sponzaDrawSlots,
        sponzaAsset.materials.size(), sponzaAsset.textures.size());

    auto commandBuffers = cmdPool.allocate(3);
    if (commandBuffers.size() != 3) {
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
    static std::string fontSc;
    static std::string fontJp;
    fontSc = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/SimplifiedChinese/"
        "SourceHanSansSC-Bold.otf");
    fontJp = lumen::core::get_resource_path(
        "assets/fonts/SourceHanSansSC/OTF/Japanese/SourceHanSans-Bold.otf");
    imguiInfo.cjk_font_ttf_path = fontSc.c_str();
    imguiInfo.cjk_font_japanese_merge_path = fontJp.c_str();
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

    lumen::render::Sampler sceneSampler;
    if (!sceneSampler.create(ctx)) {
        LUMEN_APP_LOG_ERROR("场景采样器创建失败");
        return -1;
    }

    auto sceneTexId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), sceneTarget.color_view(),
            sceneTarget.color_sample_layout()));
    auto debugSceneTexId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), debugTileTarget.color_view(),
            debugTileTarget.color_sample_layout()));

    auto idMapVizTexId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), idMapVizTarget.color_view(),
            idMapVizTarget.color_sample_layout()));

    lumen::render::Sampler pickIdNearestSampler;
    {
        lumen::render::SamplerConfig sc {};
        sc.magFilter = VK_FILTER_NEAREST;
        sc.minFilter = VK_FILTER_NEAREST;
        sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (!pickIdNearestSampler.create(ctx, sc)) {
            LUMEN_APP_LOG_ERROR("Pick ID 最近邻 Sampler 创建失败");
            return -1;
        }
    }

    const std::string skyVsPath =
        lumen::core::get_resource_path("shaders/skybox.vert.spv");
    const std::string skyFsPath =
        lumen::core::get_resource_path("shaders/skybox.frag.spv");
    lumen::render::ShaderModule smSkyVs;
    lumen::render::ShaderModule smSkyFs;
    if (!smSkyVs.create_from_file(dev, skyVsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("天空盒顶点着色器加载失败: {}", skyVsPath);
        return -1;
    }
    if (!smSkyFs.create_from_file(dev, skyFsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("天空盒片元着色器加载失败: {}", skyFsPath);
        return -1;
    }

    lumen::render::DescriptorSetLayout skyDsl;
    std::vector<lumen::render::DescriptorBinding> skyBinds = {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!skyDsl.create(ctx, skyBinds)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorSetLayout 失败");
        return -1;
    }

    lumen::render::DescriptorPool skyDpool;
    if (!skyDpool.create(ctx,
                         { { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             .count = 1 } },
                         1)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorPool 失败");
        return -1;
    }

    VkDescriptorSet skyDs { VK_NULL_HANDLE };
    if (!skyDpool.allocate(dev, skyDsl.handle(), skyDs)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::write_descriptor_set(
        dev, skyDs, {},
        { { .binding = 0,
            .imageView = ibl.environment.view(),
            .sampler = ibl.environment.sampler(),
            .imageLayout = ibl.environment.descriptor_layout() } });

    VkPushConstantRange skyPc {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = static_cast<uint32_t>(sizeof(SkyPush)),
    };
    lumen::render::PipelineLayout skyPl;
    if (!skyPl.create(ctx, { skyDsl.handle() }, { skyPc })) {
        LUMEN_APP_LOG_ERROR("天空盒 PipelineLayout 失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig skyCfg {};
    skyCfg.shaderStages.push_back(
        { smSkyVs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    skyCfg.shaderStages.push_back(
        { smSkyFs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    skyCfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(float) * 3,
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    skyCfg.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = 0 });
    skyCfg.depthTest = true;
    skyCfg.depthWrite = false;
    skyCfg.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyCfg.cullMode = VK_CULL_MODE_FRONT_BIT;
    skyCfg.frontFace = VK_FRONT_FACE_CLOCKWISE;

    lumen::render::GraphicsPipeline skyPipe;
    if (!skyPipe.create(ctx, skyPl, offscreenRenderPass, 0, skyCfg)) {
        LUMEN_APP_LOG_ERROR("天空盒 GraphicsPipeline 失败");
        return -1;
    }

    lumen::render::VertexBuffer skyVbuf;
    if (!skyVbuf.create_device_local_and_upload(
            ctx, ctx.graphics_queue(), cmdPool, SKY_CUBE_POSITIONS.data(),
            sizeof(SKY_CUBE_POSITIONS))) {
        LUMEN_APP_LOG_ERROR("天空盒顶点缓冲上传失败");
        return -1;
    }

    const std::string helmetVsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_VERT_SPV_RELATIVE));
    const std::string helmetFsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_FRAG_SPV_RELATIVE));
    lumen::render::ShaderModule smHelmetVs;
    lumen::render::ShaderModule smHelmetFs;
    if (!smHelmetVs.create_from_file(dev, helmetVsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("头盔顶点着色器加载失败: {}", helmetVsPath);
        return -1;
    }
    if (!smHelmetFs.create_from_file(dev, helmetFsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("头盔片元着色器加载失败: {}", helmetFsPath);
        return -1;
    }

    const std::string pickIdVsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PICK_ID_VERT_SPV_RELATIVE));
    const std::string pickIdFsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PICK_ID_FRAG_SPV_RELATIVE));
    lumen::render::ShaderModule smPickIdVs;
    lumen::render::ShaderModule smPickIdFs;
    if (!smPickIdVs.create_from_file(dev, pickIdVsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("pick_id 顶点着色器加载失败: {}", pickIdVsPath);
        return -1;
    }
    if (!smPickIdFs.create_from_file(dev, pickIdFsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("pick_id 片元着色器加载失败: {}", pickIdFsPath);
        return -1;
    }

    lumen::render::DescriptorSetLayout helmetFrameDsl;
    if (!helmetFrameDsl.create(
            ctx, lumen::render::pbr_frame_ibl_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Frame DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmetMaterialDsl;
    if (!helmetMaterialDsl.create(
            ctx, lumen::render::pbr_material_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR 材质 DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmetObjectDsl;
    if (!helmetObjectDsl.create(
            ctx, lumen::render::pbr_object_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Object DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorSetLayout helmetLightDsl;
    if (!helmetLightDsl.create(
            ctx, lumen::render::pbr_light_descriptor_bindings())) {
        LUMEN_APP_LOG_ERROR("PBR Light DescriptorSetLayout 失败");
        return -1;
    }

    const auto sponzaMatCount = static_cast<uint32_t>(pbrMaterials.size());
    const uint32_t PBR_UBO_STATIC = 3u + sponzaMatCount + 3u;
    const uint32_t PBR_SET_COUNT = 3u + sponzaMatCount + 1u + 3u;
    const uint32_t PBR_COMBINED_FOR_MATERIALS = sponzaMatCount * 5u;
    const uint32_t PBR_COMBINED_TOTAL = 9u + PBR_COMBINED_FOR_MATERIALS;

    lumen::render::DescriptorPool pbrDpool;
    if (!pbrDpool.create(
            ctx,
            { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = PBR_UBO_STATIC },
              { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .count = 1 },
              { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .count = PBR_COMBINED_TOTAL } },
            PBR_SET_COUNT)) {
        LUMEN_APP_LOG_ERROR("PBR DescriptorPool 失败 (材质数={})",
                            sponzaMatCount);
        return -1;
    }

    std::vector<lumen::render::UniformBuffer> sponzaMaterialUbos(
        sponzaMatCount);
    for (uint32_t mi = 0; mi < sponzaMatCount; ++mi) {
        if (!sponzaMaterialUbos[mi].create_persistent(
                ctx, sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 失败 mi={}", mi);
            return -1;
        }
    }

    std::vector<VkDescriptorSet> sponzaMaterialDs(sponzaMatCount);
    for (uint32_t mi = 0; mi < sponzaMatCount; ++mi) {
        if (!pbrDpool.allocate(dev, helmetMaterialDsl.handle(),
                               sponzaMaterialDs[mi])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败 mi={}", mi);
            return -1;
        }
        lumen::render::PbrMaterialUbo mu {};
        lumen::render::pack_pbr_material_ubo(mu, pbrMaterials[mi], 3.0F);
        sponzaMaterialUbos[mi].update(mu);
        lumen::render::write_pbr_material_descriptor_set(
            dev, sponzaMaterialDs[mi], sponzaMaterialUbos[mi].handle(),
            sizeof(lumen::render::PbrMaterialUbo), pbrMaterials[mi],
            pbrPlaceholders);
    }

    std::array<VkDescriptorSet, 3> helmetFrameDs {};
    for (uint32_t i = 0; i < helmetFrameDs.size(); ++i) {
        if (!pbrDpool.allocate(dev, helmetFrameDsl.handle(),
                               helmetFrameDs[i])) {
            LUMEN_APP_LOG_ERROR("PBR Frame DescriptorSet 分配失败");
            return -1;
        }
    }

    std::array<lumen::render::UniformBuffer, 3> helmetFrameUbos {};
    for (auto & helmetFrameUbo : helmetFrameUbos) {
        if (!helmetFrameUbo.create_persistent(
                ctx, sizeof(lumen::render::PbrFrameUbo))) {
            LUMEN_APP_LOG_ERROR("PBR Frame UniformBuffer 失败");
            return -1;
        }
    }

    for (uint32_t i = 0; i < helmetFrameDs.size(); ++i) {
        lumen::render::write_pbr_frame_ibl_descriptor_set(
            dev, helmetFrameDs[i], helmetFrameUbos[i].handle(),
            sizeof(lumen::render::PbrFrameUbo), ibl.irradiance, ibl.prefilter,
            ibl.brdf_lut);
    }

    VkDescriptorSet helmetObjectDs { VK_NULL_HANDLE };
    if (!pbrDpool.allocate(dev, helmetObjectDsl.handle(), helmetObjectDs)) {
        LUMEN_APP_LOG_ERROR("PBR Object DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::UniformBuffer helmetObjectUbo;
    if (!helmetObjectUbo.create_persistent(
            ctx, static_cast<size_t>(helmetObjectUboBytes))) {
        LUMEN_APP_LOG_ERROR("PBR Object UniformBuffer 失败");
        return -1;
    }
    lumen::render::write_pbr_object_descriptor_set_dynamic(
        dev, helmetObjectDs, helmetObjectUbo.handle(),
        sizeof(lumen::render::PbrObjectUbo));

    std::array<VkDescriptorSet, 3> helmetLightDs {};
    std::array<lumen::render::UniformBuffer, 3> helmetLightUbos {};
    for (uint32_t i = 0; i < helmetLightDs.size(); ++i) {
        if (!pbrDpool.allocate(dev, helmetLightDsl.handle(),
                               helmetLightDs[i])) {
            LUMEN_APP_LOG_ERROR("PBR Light DescriptorSet 分配失败");
            return -1;
        }
        if (!helmetLightUbos[i].create_persistent(
                ctx, sizeof(lumen::render::PbrLightUbo))) {
            LUMEN_APP_LOG_ERROR("PBR Light UniformBuffer 失败");
            return -1;
        }
        lumen::render::write_pbr_light_descriptor_set(
            dev, helmetLightDs[i], helmetLightUbos[i].handle(),
            sizeof(lumen::render::PbrLightUbo));
    }

    lumen::render::PipelineLayout helmetPl;
    if (!helmetPl.create(ctx,
                         { helmetFrameDsl.handle(), helmetMaterialDsl.handle(),
                           helmetObjectDsl.handle(), helmetLightDsl.handle() },
                         {})) {
        LUMEN_APP_LOG_ERROR("PBR PipelineLayout 失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig helmetCfg {};
    helmetCfg.shaderStages.push_back(
        { smHelmetVs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    helmetCfg.shaderStages.push_back(
        { smHelmetFs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    helmetCfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(HelmVertex),
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    helmetCfg.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(HelmVertex, position) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = offsetof(HelmVertex, normal) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 2,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec2,
          .offset = offsetof(HelmVertex, uv) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 3,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec4,
          .offset = offsetof(HelmVertex, tangent) });
    // 关闭剔除，避免绕序/双面导致整模不可见；确认后可改回 BACK + CCW/CW
    helmetCfg.cullMode = VK_CULL_MODE_NONE;
    helmetCfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline helmetPipe;
    if (!helmetPipe.create(ctx, helmetPl, offscreenRenderPass, 0, helmetCfg)) {
        LUMEN_APP_LOG_ERROR("头盔 GraphicsPipeline 失败");
        return -1;
    }

    lumen::render::PipelineLayout pickIdPl;
    {
        VkPushConstantRange pcRange {};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(std::uint32_t);
        if (!pickIdPl.create(ctx,
                             { helmetFrameDsl.handle(),
                               helmetObjectDsl.handle() },
                             { pcRange })) {
            LUMEN_APP_LOG_ERROR("Pick ID PipelineLayout 失败");
            return -1;
        }
    }
    lumen::render::GraphicsPipelineConfig pickIdCfg {};
    pickIdCfg.shaderStages.push_back(
        { smPickIdVs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pickIdCfg.shaderStages.push_back(
        { smPickIdFs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pickIdCfg.vertexBindings = helmetCfg.vertexBindings;
    pickIdCfg.vertexAttributes = helmetCfg.vertexAttributes;
    pickIdCfg.cullMode = VK_CULL_MODE_NONE;
    pickIdCfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pickIdCfg.depthTest = true;
    pickIdCfg.depthWrite = true;
    pickIdCfg.alphaBlend = false;
    lumen::render::GraphicsPipeline pickIdPipe;
    if (!pickIdPipe.create(ctx, pickIdPl, pickIdTarget.render_pass_ref(), 0,
                           pickIdCfg)) {
        LUMEN_APP_LOG_ERROR("Pick ID GraphicsPipeline 失败");
        return -1;
    }

    lumen::render::Buffer pickReadbackBuffer;
    {
        lumen::render::BufferCreateInfo bi {};
        bi.size = sizeof(std::uint32_t);
        bi.usage = lumen::render::BufferUsage::TransferDst;
        bi.hostVisible = true;
        bi.hostRandomAccess = true;
        if (!pickReadbackBuffer.create(ctx, bi)) {
            LUMEN_APP_LOG_ERROR("Pick 读回缓冲创建失败");
            return -1;
        }
    }

    const std::string pickVizVsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PICK_ID_VISUALIZE_VERT_SPV_RELATIVE));
    const std::string pickVizFsPath = lumen::core::get_resource_path(
        std::string(lumen::render::PICK_ID_VISUALIZE_FRAG_SPV_RELATIVE));
    lumen::render::ShaderModule smPickVizVs;
    lumen::render::ShaderModule smPickVizFs;
    if (!smPickVizVs.create_from_file(dev, pickVizVsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("pick_id_visualize 顶点着色器加载失败: {}",
                            pickVizVsPath);
        return -1;
    }
    if (!smPickVizFs.create_from_file(dev, pickVizFsPath.c_str())) {
        LUMEN_APP_LOG_ERROR("pick_id_visualize 片元着色器加载失败: {}",
                            pickVizFsPath);
        return -1;
    }

    lumen::render::DescriptorSetLayout idMapVizDsl;
    if (!idMapVizDsl.create(
            ctx,
            { { .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .count = 1,
                .stages = VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorPool idMapVizDpool;
    if (!idMapVizDpool.create(
            ctx,
            { { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .count = 1 } },
            1)) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorPool 失败");
        return -1;
    }
    VkDescriptorSet idMapVizDs { VK_NULL_HANDLE };
    if (!idMapVizDpool.allocate(dev, idMapVizDsl.handle(), idMapVizDs)) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::write_descriptor_image(
        dev, idMapVizDs, 0, pickIdTarget.color_image().view(),
        pickIdNearestSampler.handle(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    lumen::render::PipelineLayout pickVizPl;
    if (!pickVizPl.create(ctx, { idMapVizDsl.handle() }, {})) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 PipelineLayout 失败");
        return -1;
    }
    lumen::render::GraphicsPipelineConfig pickVizCfg {};
    pickVizCfg.shaderStages.push_back(
        { smPickVizVs.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pickVizCfg.shaderStages.push_back(
        { smPickVizFs.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pickVizCfg.cullMode = VK_CULL_MODE_NONE;
    pickVizCfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pickVizCfg.depthTest = false;
    pickVizCfg.depthWrite = false;
    pickVizCfg.alphaBlend = false;
    lumen::render::GraphicsPipeline pickVizPipe;
    if (!pickVizPipe.create(ctx, pickVizPl, offscreenRenderPass, 0,
                            pickVizCfg)) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 GraphicsPipeline 失败");
        return -1;
    }

    LUMEN_APP_LOG_INFO("PBR 场景资源就绪，着色器 {} | {}", helmetVsPath,
                       helmetFsPath);

    float skyExposure { 1.0F };
    float iblStrength { 1.0F };
    float emissiveScale { 3.0F };
    int pointLightCount { lumen::render::PBR_LEGACY_POINT_LIGHT_CAP };
    float pointDirectStrength { 1.15F };
    int helmetDebugView { 0 };
    bool pbrDebugTileGrid { false };
    bool show_id_map_viz { false };

    lumen::scene::SceneCamera sceneCamera;
    lumen::scene::SceneOrbitController orbit;
    sceneCamera.set_projection_perspective(55.0F, 0.05F, 120.0F);
    orbit.set_pivot(glm::vec3(0.0F));
    orbit.set_world_up(glm::vec3(0.0F, 1.0F, 0.0F));
    orbit.set_yaw(0.0F);
    orbit.set_pitch(0.0F);
    orbit.set_radius(9.0F);
    {
        lumen::scene::SceneOrbitController::Limits lim {};
        lim.min_radius = 0.08F;
        lim.max_radius = 28.0F;
        lim.min_pitch = glm::radians(-89.0F);
        lim.max_pitch = glm::radians(89.0F);
        orbit.set_limits(lim);
        lumen::scene::SceneOrbitController::Settings st {};
        st.rmb_look_sensitivity = glm::radians(0.18F);
        st.mouse_smooth_time_seconds = 0.0F;
        orbit.set_settings(st);
    }

    uint32_t pendingSceneWidth { sceneTarget.width() };
    uint32_t pendingSceneHeight { sceneTarget.height() };

    std::array<ImTextureID, 6> texEnvFaces {};
    std::array<ImTextureID, 6> texIrrFaces {};
    std::array<ImTextureID, 6> texPreFaces {};
    for (uint32_t face = 0; face < 6; ++face) {
        texEnvFaces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), envFaceViews[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        texIrrFaces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), irrFaceViews[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        texPreFaces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), preFaceViews[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
    auto texBrdf =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), brdfLutView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    lumen::platform::EventPump pump;
    uint32_t frameIndex { 0 };
    bool running { true };
    bool needRecreateSwapchain { false };

    lumen::ui::ImGuiLayer imguiLayer;
    imguiLayer.attach(pump);

    lumen::ui::PanelManager dockPanels;
    dockPanels.add(std::make_unique<lumen::ui::LogPanel>());
    dockPanels.add(std::make_unique<lumen::ui::GpuCapabilitiesPanel>(ctx));
    dockPanels.add(std::make_unique<lumen::ui::SceneHierarchyPanel>(
        &editorScene, &editorSelection));
    dockPanels.add(std::make_unique<lumen::ui::SceneInspectorPanel>(
        &editorScene, &editorSelection));

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
            [&](lumen::platform::EventWindowResize &) {
                needRecreateSwapchain = true;
                return false;
            });
    });

    constexpr uint64_t ACQUIRE_TIMEOUT_NS { 100'000'000 };
    /// 略长于常见 16ms 帧，减轻慢帧时轮询等待的无意义超时次数
    constexpr uint64_t FENCE_WAIT_NS { 50'000'000 };
    bool acquireFailLogged { false };
    auto prevFrameTime = std::chrono::steady_clock::now();
    lumen::ui::TextureViewRect scene_viewport_rect_for_gizmo {};
    bool scene_viewport_gizmo_rect_valid { false };
    ImGuiWindow *scene_viewport_imgui_window_for_gizmo { nullptr };
    bool prev_key_f_down { false };
    bool prev_key_w_down { false };
    bool prev_key_e_down { false };
    bool prev_key_r_down { false };
    bool prev_scene_pick_lmb_down { false };
    bool scene_pick_pending { false };
    std::uint32_t scene_pick_fb_x { 0 };
    std::uint32_t scene_pick_fb_y { 0 };

    while (running) {
        if (!pump.poll()) {
            LUMEN_APP_LOG_INFO("事件泵结束，退出主循环");
            break;
        }

        const auto frameNow = std::chrono::steady_clock::now();
        const float deltaSeconds =
            std::chrono::duration<float>(frameNow - prevFrameTime).count();
        prevFrameTime = frameNow;

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                LUMEN_APP_LOG_WARN("Swapchain 重建跳过: 帧缓冲尺寸无效 {}x{}",
                                   windowWidth, windowHeight);
            } else if (!lumen::render::recreate_swapchain_resources(
                           ctx, swapchain, framebuffers, frameSync, renderPass,
                           static_cast<uint32_t>(windowWidth),
                           static_cast<uint32_t>(windowHeight), 3,
                           VK_NULL_HANDLE)) {
                LUMEN_APP_LOG_ERROR("recreate_swapchain_resources 失败 {}x{}",
                                    windowWidth, windowHeight);
            } else {
                lumen::ui::imgui_backend_set_min_image_count(
                    swapchain.image_count());
                LUMEN_APP_LOG_INFO("Swapchain 已重建 {}x{}", windowWidth,
                                   windowHeight);
            }
            needRecreateSwapchain = false;
            frameIndex = 0;
            continue;
        }

        while (!frameSync.wait_fence(frameIndex, FENCE_WAIT_NS)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running) {
            break;
        }

        if (pendingSceneWidth >= 2U && pendingSceneHeight >= 2U &&
            (pendingSceneWidth != sceneTarget.width() ||
             pendingSceneHeight != sceneTarget.height())) {
            ctx.wait_idle();
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(sceneTexId));
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(debugSceneTexId));
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(idMapVizTexId));
            sceneTexId = static_cast<ImTextureID>(0);
            debugSceneTexId = static_cast<ImTextureID>(0);
            idMapVizTexId = static_cast<ImTextureID>(0);
            if (!sceneTarget.resize(pendingSceneWidth, pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("场景离屏目标 resize 失败");
                running = false;
                break;
            }
            if (!pickIdTarget.resize(pendingSceneWidth, pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("PickIdRenderTarget resize 失败");
                running = false;
                break;
            }
            if (!debugTileTarget.resize(pendingSceneWidth,
                                        pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("分屏调试离屏目标 resize 失败");
                running = false;
                break;
            }
            if (!idMapVizTarget.resize(pendingSceneWidth, pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("ID Map 可视化离屏目标 resize 失败");
                running = false;
                break;
            }
            lumen::render::write_descriptor_image(
                dev, idMapVizDs, 0, pickIdTarget.color_image().view(),
                pickIdNearestSampler.handle(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            sceneTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(), sceneTarget.color_view(),
                    sceneTarget.color_sample_layout()));
            debugSceneTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(), debugTileTarget.color_view(),
                    debugTileTarget.color_sample_layout()));
            idMapVizTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(), idMapVizTarget.color_view(),
                    idMapVizTarget.color_sample_layout()));
        }

        if (swapchain.extent().width == 0 || swapchain.extent().height == 0) {
            SDL_Delay(16);
            continue;
        }

        const uint32_t imageIndex =
            swapchain.acquire_next_image(frameSync.image_available(frameIndex),
                                         VK_NULL_HANDLE, ACQUIRE_TIMEOUT_NS);
        if (imageIndex == UINT32_MAX) {
            if (!acquireFailLogged) {
                LUMEN_APP_LOG_WARN(
                    "acquire_next_image 未取到图像 (超时/最小化/OUT_OF_DATE)，"
                    "跳过本帧 (连续失败仅打本条，恢复后重置)");
                acquireFailLogged = true;
            }
            continue;
        }
        acquireFailLogged = false;

        scene_viewport_gizmo_rect_valid = false;
        scene_viewport_imgui_window_for_gizmo = nullptr;

        bool sceneViewHovered { false };
        bool debugSceneViewHovered { false };

        imguiLayer.begin_frame();

        constexpr float SCENE_ORBIT_WHEEL_SCALE { 0.14F };
        const auto onOrbitViewportScroll = [&](float wheel) {
            orbit.apply_scroll_zoom(wheel, SCENE_ORBIT_WHEEL_SCALE);
            orbit.apply_to(sceneCamera);
        };

        if (ImGui::Begin("场景 Gizmo")) {
            ImGui::TextUnformatted(
                "在「Scene」视口内左键点选 SubMesh / 根网格（ID Map）；层级与检视器"
                "一致。选中后可拖 Gizmo。未在输入文字且非 Alt 时按 F 平滑对焦（鼠标在"
                " Scene 图像上或该视口已记为悬停）。");
            ImGui::TextUnformatted(
                "工具快捷键（与 Unity 一致）：W 平移 | E 旋转 | R 缩放。视口悬停时生效；"
                "按住右键飞行（WASD / Q 下 / E 上）时暂时不响应 W/E/R，避免与相机移动冲突。");
            ImGui::RadioButton(
                "平移", &scene_gizmo_operation,
                static_cast<int>(ImGuizmo::TRANSLATE));
            ImGui::SameLine();
            ImGui::RadioButton(
                "旋转", &scene_gizmo_operation,
                static_cast<int>(ImGuizmo::ROTATE));
            ImGui::SameLine();
            ImGui::RadioButton(
                "缩放", &scene_gizmo_operation,
                static_cast<int>(ImGuizmo::SCALE));
        }
        ImGui::End();

        if (ImGui::Begin("ID Map 可视化")) {
            ImGui::Checkbox("启用（每帧渲染 ID Pass + 伪彩色）", &show_id_map_viz);
            ImGui::TextUnformatted(
                "与 Scene 同分辨率；深色为 ID 0（背景），其余为编码后的实体哈希色。");
            if (show_id_map_viz &&
                idMapVizTexId != static_cast<ImTextureID>(0)) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::Image(idMapVizTexId, avail);
            }
        }
        ImGui::End();

        const auto onSceneViewportAfterImage =
            [&](const lumen::ui::TextureViewRect &rect) {
                scene_viewport_rect_for_gizmo = rect;
                scene_viewport_gizmo_rect_valid = true;
                scene_viewport_imgui_window_for_gizmo =
                    ImGui::GetCurrentWindowRead();
            };

        lumen::ui::imgui_scene_viewport_panel(
            "Scene", sceneTexId, &pendingSceneWidth, &pendingSceneHeight,
            &sceneViewHovered, onOrbitViewportScroll, onSceneViewportAfterImage);

        if (pbrDebugTileGrid &&
            debugSceneTexId != static_cast<ImTextureID>(0)) {
            auto drawTileLabels = [&](const lumen::ui::TextureViewRect &rect) {
                constexpr int DEBUG_TILE_COLS = 4;
                constexpr int DEBUG_TILE_ROWS = 4;
                ImDrawList *const drawList = ImGui::GetWindowDrawList();
                const ImU32 labelColor = IM_COL32(255, 255, 120, 255);
                const ImU32 shadowColor = IM_COL32(0, 0, 0, 180);
                static constexpr const char
                    *DEBUG_TILE_LABELS_ZH[DEBUG_TILE_COLS * DEBUG_TILE_ROWS] = {
                        "PBR 完整", "几何法线",   "法线贴图",  "反照率",
                        "金属度",   "粗糙度",     "AO",        "漫反射 IBL",
                        "镜面 IBL", "Irradiance", "Prefilter", "N·V",
                        "F0",       "BRDF LUT",   "自发光",    "Base Color",
                    };
                const ImVec2 rectMin(rect.minX, rect.minY);
                const ImVec2 rectMax(rect.maxX, rect.maxY);
                const float displayWidth = rectMax.x - rectMin.x;
                const float displayHeight = rectMax.y - rectMin.y;
                for (int tileIndex = 0;
                     tileIndex < DEBUG_TILE_COLS * DEBUG_TILE_ROWS;
                     ++tileIndex) {
                    const int col = tileIndex % DEBUG_TILE_COLS;
                    const int row = tileIndex / DEBUG_TILE_COLS;
                    const float x =
                        rectMin.x +
                        (displayWidth / static_cast<float>(DEBUG_TILE_COLS)) *
                            static_cast<float>(col);
                    const float y =
                        rectMin.y +
                        (displayHeight / static_cast<float>(DEBUG_TILE_ROWS)) *
                            static_cast<float>(row);
                    ImVec2 textPos(x + 3.0F, y + 4.0F);
                    ImVec2 shadowPos(x + 2.0F, y + 3.0F);
                    drawList->AddText(shadowPos, shadowColor,
                                      DEBUG_TILE_LABELS_ZH[tileIndex]);
                    drawList->AddText(textPos, labelColor,
                                      DEBUG_TILE_LABELS_ZH[tileIndex]);
                }
            };
            lumen::ui::imgui_scene_viewport_panel(
                "PBR 分屏调试 (4×4)", debugSceneTexId, nullptr, nullptr,
                &debugSceneViewHovered, onOrbitViewportScroll, drawTileLabels);
        }

        if (ImGui::Begin("环境光贴图")) {
            ImGui::TextWrapped("HDR 源：%s", hdrPath.c_str());
            ImGui::TextUnformatted("烘焙纹理均为 RGBA32F 线性；立方体面序与 "
                                   "Vulkan 一致（+X −X +Y −Y +Z "
                                   "−Z）。");
            char iblInfo[384];
            std::snprintf(
                iblInfo, sizeof iblInfo,
                "Environment %u×%u · mip %u  |  Irradiance %u×%u · mip %u  |  "
                "Prefilter %u×%u · mip %u  |  BRDF LUT %u×%u",
                ibl.environment.width(), ibl.environment.height(),
                ibl.environment.mip_levels(), ibl.irradiance.width(),
                ibl.irradiance.height(), ibl.irradiance.mip_levels(),
                ibl.prefilter.width(), ibl.prefilter.height(),
                ibl.prefilter.mip_levels(), ibl.brdf_lut.width(),
                ibl.brdf_lut.height());
            ImGui::TextWrapped("%s", iblInfo);
            ImGui::Separator();
            ImGui::SliderFloat("天空曝光", &skyExposure, 0.02F, 6.0F, "%.2f");
            ImGui::SliderFloat("IBL 强度", &iblStrength, 0.0F, 4.0F, "%.2f");
            ImGui::Separator();
            ImGui::TextUnformatted("Environment 立方体（mip 0）");
            imgui_draw_cubemap_face_grid(texEnvFaces, ImVec2(140.0F, 140.0F));
            ImGui::TextUnformatted("Irradiance 立方体（mip 0）");
            imgui_draw_cubemap_face_grid(texIrrFaces, ImVec2(110.0F, 110.0F));
            ImGui::TextUnformatted(
                "Prefilter 立方体（mip 0；着色器按粗糙度采样完整 mip 链）");
            imgui_draw_cubemap_face_grid(texPreFaces, ImVec2(110.0F, 110.0F));
            ImGui::TextUnformatted("BRDF 积分 LUT（RG，线性）");
            ImGui::Image(texBrdf, ImVec2(220.0F, 220.0F));
        }
        ImGui::End();

        if (ImGui::Begin("IBL 预览")) {
            ImGui::TextUnformatted(
                "右键拖拽旋转（「Scene」或「PBR 分屏调试」画面内、非 Alt）；"
                "Alt+左/中键 轨道/平移/缩放；WASD+EQ 平移；滚轮绕枢轴缩放；"
                "Scene 图像上左键点选实体（非 Alt、非 Gizmo 悬停）；未在输入文字时按 F 对焦");
            ImGui::TextUnformatted("天空曝光与 IBL 强度见「环境光贴图」。");
            float orbitR = orbit.radius();
            ImGui::SliderFloat("轨道距离（缩放）", &orbitR, 0.08F, 28.0F,
                               "%.2f");
            orbit.set_radius(orbitR);
            ImGui::SliderFloat("自发光倍率", &emissiveScale, 0.0F, 12.0F,
                               "%.1f");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "点光源（GGX 直射；分屏首格与「Scene」完整 PBR 一致）");
            ImGui::SliderInt("点光数量", &pointLightCount, 0,
                             lumen::render::PBR_FORWARD_MAX_LIGHTS);
            ImGui::SliderFloat("点光强度", &pointDirectStrength, 0.0F, 6.0F,
                               "%.2f");
        }
        ImGui::End();

        if (ImGui::Begin("PBR 着色调试")) {
            ImGui::TextUnformatted("视口操作说明见「IBL 预览」。");
            ImGui::Separator();
            ImGui::Checkbox("分屏光照调试 (4×4，「PBR 分屏调试」窗口)",
                            &pbrDebugTileGrid);
            ImGui::TextUnformatted("关闭分屏后可用单项模式：");
            ImGui::BeginDisabled(pbrDebugTileGrid);
            ImGui::RadioButton("PBR（最终）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_NONE);
            ImGui::RadioButton("法线（世界空间，着色）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_NORMAL_WS);
            ImGui::RadioButton("法线（切线空间，贴图）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_NORMAL_TS);
            ImGui::RadioButton("深度（距相机归一化）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_DEPTH);
            ImGui::RadioButton("反照率", &helmetDebugView,
                               lumen::render::PBR_DEBUG_ALBEDO);
            ImGui::RadioButton("金属度", &helmetDebugView,
                               lumen::render::PBR_DEBUG_METALLIC);
            ImGui::RadioButton("粗糙度", &helmetDebugView,
                               lumen::render::PBR_DEBUG_ROUGHNESS);
            ImGui::RadioButton("AO", &helmetDebugView,
                               lumen::render::PBR_DEBUG_AO);
            ImGui::RadioButton("直射漫反射", &helmetDebugView,
                               lumen::render::PBR_DEBUG_DIRECT_DIFFUSE);
            ImGui::RadioButton("直射镜面", &helmetDebugView,
                               lumen::render::PBR_DEBUG_DIRECT_SPECULAR);
            ImGui::RadioButton("IBL 漫反射", &helmetDebugView,
                               lumen::render::PBR_DEBUG_IBL_DIFFUSE);
            ImGui::RadioButton("IBL 镜面", &helmetDebugView,
                               lumen::render::PBR_DEBUG_IBL_SPECULAR);
            ImGui::RadioButton("自发光", &helmetDebugView,
                               lumen::render::PBR_DEBUG_EMISSIVE);
            ImGui::RadioButton("合成（无 IBL）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_FINAL_NO_IBL);
            ImGui::RadioButton("合成（无直射）", &helmetDebugView,
                               lumen::render::PBR_DEBUG_FINAL_NO_DIRECT);
            ImGui::RadioButton("热力：直射能量", &helmetDebugView,
                               lumen::render::PBR_DEBUG_HEAT_LIGHT_INTENSITY);
            ImGui::RadioButton("热力：N·L 最大", &helmetDebugView,
                               lumen::render::PBR_DEBUG_HEAT_NDOTL);
            ImGui::RadioButton("热力：光源数量", &helmetDebugView,
                               lumen::render::PBR_DEBUG_HEAT_LIGHT_COUNT);
            char dbgLine[64];
            std::snprintf(dbgLine, sizeof dbgLine, "当前模式号: %d",
                          helmetDebugView);
            ImGui::TextUnformatted(dbgLine);
            ImGui::EndDisabled();
        }
        ImGui::End();

        if (ImGui::Begin("Sponza 场景网格")) {
            ImGui::Text("glTF mesh 数: %zu（检视首个非空 mesh）",
                        sponzaAsset.model.size());
            ImGui::Text("Primitive 数: %zu", sponzaInspectorMesh->primitives.size());
            ImGui::Text("材质槽位数: %zu", pbrMaterials.size());
            ImGui::Text("已加载 GPU 纹理数: %zu", sponzaAsset.textures.size());
            ImGui::TextWrapped("多 primitive 按 `scene::Mesh` 绘制；每材质一套 "
                               "descriptor set=1。");
        }
        ImGui::End();

        if (ImGui::Begin("ECS 渲染组件总览")) {
            ImGui::TextWrapped(
                "下列实体来自场景注册表。根实体带 MeshRenderer（整网）；子实体带 "
                "SubMeshRenderer（单 primitive）。主渲染对首个检视 mesh 使用 "
                "SubMesh 路径（子实体变换可见），其余 glTF mesh 随根 "
                "`TransformComponent` 整体变换。");
            ImGui::Separator();
            entt::registry &reg = editorScene.registry();
            ImGui::TextUnformatted("MeshRendererComponent");
            ImGui::Indent();
            {
                const auto v = reg.view<lumen::scene::MeshRendererComponent>();
                if (v.begin() == v.end()) {
                    ImGui::TextDisabled("（无）");
                }
                for (const entt::entity ent : v) {
                    const char *tag = "?";
                    if (const auto *t = reg.try_get<lumen::scene::TagComponent>(ent)) {
                        tag = t->tag.c_str();
                    }
                    const auto &mr = v.get<lumen::scene::MeshRendererComponent>(ent);
                    const std::size_t n =
                        mr.mesh != nullptr ? mr.mesh->primitives.size() : 0;
                    ImGui::BulletText(
                        "%s  entity=%u  mesh=%p  primitives=%zu", tag,
                        static_cast<unsigned>(entt::to_integral(ent)),
                        static_cast<const void *>(mr.mesh), n);
                }
            }
            ImGui::Unindent();
            ImGui::Separator();
            ImGui::TextUnformatted("SubMeshRendererComponent");
            ImGui::Indent();
            {
                const auto v = reg.view<lumen::scene::SubMeshRendererComponent>();
                std::size_t count = 0;
                for (const entt::entity ent : v) {
                    ++count;
                    const char *tag = "?";
                    if (const auto *t = reg.try_get<lumen::scene::TagComponent>(ent)) {
                        tag = t->tag.c_str();
                    }
                    const auto &sm =
                        v.get<lumen::scene::SubMeshRendererComponent>(ent);
                    const std::uint32_t pc =
                        sm.mesh != nullptr
                            ? static_cast<std::uint32_t>(sm.mesh->primitives.size())
                            : 0;
                    const bool ok =
                        sm.mesh != nullptr && sm.primitiveIndex < pc &&
                        sm.mesh->primitives[sm.primitiveIndex].is_drawable();
                    ImGui::BulletText(
                        "%s  entity=%u  mesh=%p  primIndex=%u/%u  %s", tag,
                        static_cast<unsigned>(entt::to_integral(ent)),
                        static_cast<const void *>(sm.mesh), sm.primitiveIndex, pc,
                        ok ? "可绘制" : "无效或跳过");
                }
                if (count == 0) {
                    ImGui::TextDisabled("（无）");
                }
            }
            ImGui::Unindent();
        }
        ImGui::End();

        if (lumen::ui::imgui_backend_docking_enabled()) {
            dockPanels.set_default_dock_id(
                lumen::ui::imgui_backend_main_dockspace_id());
        }
        dockPanels.render_all();

        float scene_hotkey_mouse_x { 0.0F };
        float scene_hotkey_mouse_y { 0.0F };
        pump.input().mouse_position(scene_hotkey_mouse_x, scene_hotkey_mouse_y);
        const bool mouse_over_scene_image_rect =
            scene_viewport_gizmo_rect_valid &&
            lumen::ui::viewport_mouse_state(scene_viewport_rect_for_gizmo,
                                            scene_hotkey_mouse_x,
                                            scene_hotkey_mouse_y)
                .inViewport;

        const bool sceneTexViewportHovered =
            sceneViewHovered || debugSceneViewHovered ||
            mouse_over_scene_image_rect;
        const bool imguiBlocksSceneMouse =
            lumen::ui::imgui_wants_mouse() && !sceneTexViewportHovered;

        // Unity 式 Gizmo 切换：W/E/R；右键飞行（与轨道控制器 WASD+QE 一致）时不处理
        const bool scene_fly_rmb =
            sceneTexViewportHovered &&
            pump.input().is_mouse_button_down(
                lumen::platform::MouseButton::Right) &&
            !pump.input().has_alt();
        if (sceneTexViewportHovered && !scene_fly_rmb &&
            !ImGui::GetIO().WantTextInput) {
            const bool w_down =
                pump.input().is_key_down(lumen::platform::Key::W);
            const bool e_down =
                pump.input().is_key_down(lumen::platform::Key::E);
            const bool r_down =
                pump.input().is_key_down(lumen::platform::Key::R);
            if (w_down && !prev_key_w_down) {
                scene_gizmo_operation =
                    static_cast<int>(ImGuizmo::TRANSLATE);
            }
            if (e_down && !prev_key_e_down) {
                scene_gizmo_operation =
                    static_cast<int>(ImGuizmo::ROTATE);
            }
            if (r_down && !prev_key_r_down) {
                scene_gizmo_operation =
                    static_cast<int>(ImGuizmo::SCALE);
            }
            prev_key_w_down = w_down;
            prev_key_e_down = e_down;
            prev_key_r_down = r_down;
        } else {
            prev_key_w_down =
                pump.input().is_key_down(lumen::platform::Key::W);
            prev_key_e_down =
                pump.input().is_key_down(lumen::platform::Key::E);
            prev_key_r_down =
                pump.input().is_key_down(lumen::platform::Key::R);
        }

        const bool key_f_down =
            pump.input().is_key_down(lumen::platform::Key::F);
        const bool key_f_pressed = key_f_down && !prev_key_f_down;
        prev_key_f_down = key_f_down;
        // `WantCaptureKeyboard` 在 Dock 下几乎总为 true，会误挡快捷键；仅在有文本
        // 输入意图时屏蔽 F。
        if (key_f_pressed && sceneTexViewportHovered &&
            !ImGui::GetIO().WantTextInput) {
            entt::registry &frame_reg = editorScene.registry();
            const entt::entity frame_sel = editorSelection.entity;
            if (frame_reg.valid(frame_sel)) {
                if (const auto *smr = frame_reg.try_get<
                        lumen::scene::SubMeshRendererComponent>(frame_sel)) {
                    if (smr->mesh != nullptr &&
                        smr->primitiveIndex < smr->mesh->primitives.size()) {
                        const lumen::scene::Primitive &prim =
                            smr->mesh->primitives[smr->primitiveIndex];
                        if (prim.is_drawable()) {
                            glm::vec3 half_ext = prim.local_aabb_half_extent;
                            if (glm::length(half_ext) < 1e-6F) {
                                half_ext = glm::vec3(0.5F);
                            }
                            if (const auto frame_targets =
                                    lumen::scene::frame_orbit_targets_for_drawable(
                                        orbit, frame_reg, frame_sel,
                                        prim.local_pivot, half_ext)) {
                                orbit.begin_smooth_frame(frame_targets->first,
                                                         frame_targets->second);
                            }
                        }
                    }
                } else if (const auto *mr = frame_reg.try_get<
                               lumen::scene::MeshRendererComponent>(frame_sel)) {
                    if (mr->mesh != nullptr) {
                        glm::vec3 mesh_center {};
                        glm::vec3 mesh_half {};
                        if (lumen::scene::drawable_mesh_local_bounds(
                                *mr->mesh, &mesh_center, &mesh_half)) {
                            if (const auto frame_targets =
                                    lumen::scene::frame_orbit_targets_for_drawable(
                                        orbit, frame_reg, frame_sel,
                                        mesh_center, mesh_half)) {
                                orbit.begin_smooth_frame(frame_targets->first,
                                                         frame_targets->second);
                            }
                        }
                    }
                }
            }
        }

        orbit.tick_smooth_frame(deltaSeconds);
        const bool wantRelativeMouse = orbit.apply_per_frame_editor_navigation(
            pump.input(), sceneTexViewportHovered, imguiBlocksSceneMouse,
            deltaSeconds);
        window.set_relative_mouse_mode(wantRelativeMouse);
        orbit.apply_to(sceneCamera);

        if (scene_viewport_gizmo_rect_valid) {
            entt::registry &reg = editorScene.registry();
            const entt::entity sel = editorSelection.entity;
            auto *tr = reg.valid(sel)
                           ? reg.try_get<lumen::scene::TransformComponent>(sel)
                           : nullptr;
            const bool has_draw =
                tr != nullptr &&
                (reg.all_of<lumen::scene::MeshRendererComponent>(sel) ||
                 reg.all_of<lumen::scene::SubMeshRendererComponent>(sel));
            if (!has_draw) {
                lumen::ui::imguizmo_reset_interaction_state();
            } else {
                const VkExtent2D gizmo_ext = sceneTarget.extent();
                const float gizmo_aspect =
                    static_cast<float>(gizmo_ext.width) /
                    static_cast<float>((std::max)(1U, gizmo_ext.height));
                const glm::mat4 view = sceneCamera.view_matrix();
                const glm::mat4 proj =
                    sceneCamera.projection_matrix(gizmo_aspect);
                glm::vec3 pivot_mesh { 0.0F };
                if (const auto *smr =
                        reg.try_get<lumen::scene::SubMeshRendererComponent>(
                            sel)) {
                    if (smr->mesh != nullptr &&
                        smr->primitiveIndex < smr->mesh->primitives.size()) {
                        pivot_mesh =
                            smr->mesh->primitives[smr->primitiveIndex]
                                .local_pivot;
                    }
                }
                const glm::mat4 pivot_tr =
                    glm::translate(glm::mat4(1.0F), pivot_mesh);
                const glm::mat4 world =
                    lumen::scene::world_matrix(reg, sel);
                glm::mat4 gizmo_matrix = world * pivot_tr;
                const auto op = static_cast<ImGuizmo::OPERATION>(
                    scene_gizmo_operation);
                lumen::ui::imguizmo_manipulate(
                    scene_viewport_rect_for_gizmo, view, proj, &gizmo_matrix,
                    op, ImGuizmo::LOCAL, nullptr,
                    scene_viewport_imgui_window_for_gizmo);
                if (lumen::ui::imguizmo_is_using()) {
                    const glm::mat4 world_new =
                        gizmo_matrix *
                        glm::translate(glm::mat4(1.0F), -pivot_mesh);
                    glm::mat4 parent_world { 1.0F };
                    if (const auto *rel =
                            reg.try_get<lumen::scene::RelationshipComponent>(
                                sel)) {
                        if (rel->parent != lumen::core::INVALID_ID) {
                            const auto parent_ent =
                                lumen::scene::find_entity_with_id(reg,
                                                                  rel->parent);
                            if (parent_ent && reg.valid(*parent_ent)) {
                                parent_world = lumen::scene::world_matrix(
                                    reg, *parent_ent);
                            }
                        }
                    }
                    tr->set_transform(
                        glm::inverse(parent_world) * world_new);
                }
            }
        } else {
            lumen::ui::imguizmo_reset_interaction_state();
        }

        {
            const bool lmb_down = pump.input().is_mouse_button_down(
                lumen::platform::MouseButton::Left);
            const bool lmb_click = lmb_down && !prev_scene_pick_lmb_down;
            prev_scene_pick_lmb_down = lmb_down;
            if (lmb_click && scene_viewport_gizmo_rect_valid &&
                mouse_over_scene_image_rect && !pump.input().has_alt() &&
                !ImGui::GetIO().WantTextInput &&
                !lumen::ui::imguizmo_is_over() &&
                !lumen::ui::imguizmo_is_using()) {
                const lumen::ui::ViewportMouseState vms =
                    lumen::ui::viewport_mouse_state(
                        scene_viewport_rect_for_gizmo, scene_hotkey_mouse_x,
                        scene_hotkey_mouse_y);
                const VkExtent2D pe = sceneTarget.extent();
                const float rw = scene_viewport_rect_for_gizmo.width();
                const float rh = scene_viewport_rect_for_gizmo.height();
                if (vms.inViewport && rw > 0.0F && rh > 0.0F && pe.width >= 1U &&
                    pe.height >= 1U) {
                    // ImGui 矩形：localY 自上而下增大；离屏与 Vulkan 附件 (0,0) 在
                    // 左上角，行 y 向下递增，勿再翻转。
                    const float nx = vms.localX / rw;
                    const float ny = vms.localY / rh;
                    scene_pick_fb_x = (std::min)(
                        pe.width - 1U,
                        static_cast<std::uint32_t>(
                            nx * static_cast<float>(pe.width)));
                    scene_pick_fb_y = (std::min)(
                        pe.height - 1U,
                        static_cast<std::uint32_t>(
                            ny * static_cast<float>(pe.height)));
                    if (pickIdTarget.is_valid()) {
                        scene_pick_pending = true;
                    }
                }
            }
        }

        const bool record_scene_pick = scene_pick_pending;

        auto &commandBuffer = commandBuffers[frameIndex];
        if (!commandBuffer.reset()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::reset 失败 frameIndex={}",
                                frameIndex);
            continue;
        }
        if (!commandBuffer.begin()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::begin 失败 frameIndex={}",
                                frameIndex);
            continue;
        }

        std::array<VkClearValue, 2> sceneClears {};
        sceneClears[0].color = { { 0.08F, 0.08F, 0.1F, 1.0F } };
        sceneClears[1].depthStencil = { 1.0F, 0 };

        VkRenderPassBeginInfo sceneRpInfo {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        sceneRpInfo.renderPass = sceneTarget.render_pass();
        sceneRpInfo.framebuffer = sceneTarget.framebuffer();
        sceneRpInfo.renderArea.offset = { 0, 0 };
        sceneRpInfo.renderArea.extent = sceneTarget.extent();
        sceneRpInfo.clearValueCount = static_cast<uint32_t>(sceneClears.size());
        sceneRpInfo.pClearValues = sceneClears.data();

        {
            const VkExtent2D ext = sceneTarget.extent();
            const float wf = static_cast<float>(ext.width);
            const float hf = static_cast<float>(ext.height);

            const float aspect =
                wf / static_cast<float>((std::max)(1U, ext.height));
            const glm::mat4 view = sceneCamera.view_matrix();
            const glm::mat4 proj = sceneCamera.projection_matrix(aspect);
            const glm::vec3 eye = sceneCamera.eye_position();
            const glm::mat4 skyView = glm::mat4(glm::mat3(view));

            VkCommandBuffer cb = commandBuffer.handle();

            entt::registry &scene_reg = editorScene.registry();
            const glm::mat4 sponza_root_world =
                lumen::scene::world_matrix(scene_reg, sponzaRootEntity);
            bool inspector_prims_via_submesh = false;
            for (const entt::entity e :
                 scene_reg.view<lumen::scene::SubMeshRendererComponent>()) {
                const auto &smr =
                    scene_reg.get<lumen::scene::SubMeshRendererComponent>(e);
                if (smr.mesh == sponzaInspectorMesh) {
                    inspector_prims_via_submesh = true;
                    break;
                }
            }
            std::vector<lumen::scene::RenderItem> pbrRenderItems;
            if (inspector_prims_via_submesh) {
                lumen::scene::append_submesh_render_items(
                    sponzaAsset.geometry(), scene_reg, 0, pbrRenderItems);
                for (const lumen::scene::Mesh &mesh_part : sponzaAsset.model) {
                    if (&mesh_part == sponzaInspectorMesh) {
                        continue;
                    }
                    lumen::scene::append_mesh_render_items(
                        sponzaAsset.geometry(), mesh_part, sponza_root_world, 0,
                        pbrRenderItems, sponzaRootEntity);
                }
            } else {
                lumen::scene::append_model_render_items(
                    sponzaAsset.geometry(), sponzaAsset.model, sponza_root_world,
                    0, pbrRenderItems, sponzaRootEntity);
            }

            for (uint32_t mi = 0; mi < sponzaMatCount; ++mi) {
                lumen::render::PbrMaterialUbo mu {};
                lumen::render::pack_pbr_material_ubo(mu, pbrMaterials[mi],
                                                     emissiveScale);
                sponzaMaterialUbos[mi].update(mu);
            }

            lumen::render::PbrFrameUbo frameU {};
            lumen::render::pack_pbr_frame_ubo(
                frameU, view, proj, eye, skyExposure, iblStrength,
                PREFILTER_MAX_LOD, 0.0F,
                pbrDebugTileGrid ? lumen::render::PBR_DEBUG_NONE
                                 : helmetDebugView);
            helmetFrameUbos[frameIndex].update(frameU);

            lumen::render::PbrLightUbo lightU {};
            lumen::render::fill_pbr_light_ubo_default_points(
                lightU, pointLightCount, pointDirectStrength);
            helmetLightUbos[frameIndex].update(lightU);

            vkCmdBeginRenderPass(commandBuffer.handle(), &sceneRpInfo,
                                 VK_SUBPASS_CONTENTS_INLINE);

            VkViewport vp { 0.0F, 0.0F, wf, hf, 0.0F, 1.0F };
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D scissor { { 0, 0 }, ext };
            vkCmdSetScissor(cb, 0, 1, &scissor);

            SkyPush skyPush {};
            skyPush.skyMvp = proj * skyView;
            skyPush.params = glm::vec4(skyExposure, 0.0F, 0.0F, 0.0F);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              skyPipe.handle());
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    skyPl.handle(), 0, 1, &skyDs, 0, nullptr);
            vkCmdPushConstants(
                cb, skyPl.handle(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                static_cast<uint32_t>(sizeof(SkyPush)), &skyPush);
            VkDeviceSize skyOff { 0 };
            VkBuffer skyVbh = skyVbuf.handle();
            vkCmdBindVertexBuffers(cb, 0, 1, &skyVbh, &skyOff);
            vkCmdDraw(cb, 36, 1, 0, 0);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              helmetPipe.handle());
            uint32_t drawSlot = 0;
            for (const lumen::scene::RenderItem &item : pbrRenderItems) {
                if (!item.is_valid_for_draw()) {
                    continue;
                }
                const lumen::scene::Primitive *prim = item.primitive;
                const lumen::render::Material *primMaterial =
                    item.material != nullptr ? item.material : &pbrMaterials[0];
                VkDescriptorSet materialDescriptorSet =
                    vk_descriptor_set_for_pbr_material(
                        primMaterial, pbrMaterials, sponzaMaterialDs);
                lumen::render::PbrObjectUbo ou {};
                ou.model = item.model;
                const glm::mat3 n3 = glm::mat3(item.model);
                ou.normalMatrix = glm::mat4(glm::transpose(glm::inverse(n3)));
                helmetObjectUbo.update(ou, drawSlot * helmetObjStride);
                std::array<VkDescriptorSet, 4> pbrSets {
                    helmetFrameDs[frameIndex], materialDescriptorSet,
                    helmetObjectDs, helmetLightDs[frameIndex]
                };
                const uint32_t dynamicOffset =
                    static_cast<uint32_t>(drawSlot * helmetObjStride);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        helmetPl.handle(), 0,
                                        static_cast<uint32_t>(pbrSets.size()),
                                        pbrSets.data(), 1, &dynamicOffset);
                ++drawSlot;
                VkDeviceSize voff =
                    static_cast<VkDeviceSize>(prim->vertex_byte_offset);
                VkBuffer vb = item.vertexBuffer->handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &voff);
                vkCmdBindIndexBuffer(cb, item.indexBuffer->handle(), 0,
                                     item.indexBuffer->vk_index_type());
                vkCmdDrawIndexed(cb, prim->index_count, 1, prim->first_index,
                                 prim->base_vertex, 0);
            }

            vkCmdEndRenderPass(commandBuffer.handle());

            if ((scene_pick_pending || show_id_map_viz) &&
                pickIdTarget.is_valid()) {
                std::array<VkClearValue, 2> pickClears {};
                pickClears[0].color.uint32[0] = 0U;
                pickClears[0].color.uint32[1] = 0U;
                pickClears[0].color.uint32[2] = 0U;
                pickClears[0].color.uint32[3] = 0U;
                pickClears[1].depthStencil = { 1.0F, 0 };
                VkRenderPassBeginInfo pickRp {
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                };
                pickRp.renderPass = pickIdTarget.render_pass();
                pickRp.framebuffer = pickIdTarget.framebuffer();
                pickRp.renderArea.offset = { 0, 0 };
                pickRp.renderArea.extent = pickIdTarget.extent();
                pickRp.clearValueCount =
                    static_cast<uint32_t>(pickClears.size());
                pickRp.pClearValues = pickClears.data();
                vkCmdBeginRenderPass(commandBuffer.handle(), &pickRp,
                                     VK_SUBPASS_CONTENTS_INLINE);
                vkCmdSetViewport(cb, 0, 1, &vp);
                vkCmdSetScissor(cb, 0, 1, &scissor);
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pickIdPipe.handle());
                uint32_t pickDrawSlot = 0;
                for (const lumen::scene::RenderItem &item : pbrRenderItems) {
                    if (!item.is_valid_for_draw()) {
                        continue;
                    }
                    const std::uint32_t pid =
                        lumen::scene::encode_pick_entity_id(item.pick_entity);
                    if (pid != 0U) {
                        lumen::render::PbrObjectUbo ou {};
                        ou.model = item.model;
                        const glm::mat3 n3 = glm::mat3(item.model);
                        ou.normalMatrix =
                            glm::mat4(glm::transpose(glm::inverse(n3)));
                        helmetObjectUbo.update(ou, pickDrawSlot * helmetObjStride);
                        std::array<VkDescriptorSet, 2> pickSets {
                            helmetFrameDs[frameIndex], helmetObjectDs
                        };
                        const uint32_t dynOff =
                            static_cast<uint32_t>(pickDrawSlot * helmetObjStride);
                        vkCmdBindDescriptorSets(
                            cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pickIdPl.handle(), 0,
                            static_cast<uint32_t>(pickSets.size()),
                            pickSets.data(), 1, &dynOff);
                        vkCmdPushConstants(cb, pickIdPl.handle(),
                                           VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                           sizeof(std::uint32_t), &pid);
                        const lumen::scene::Primitive *prim = item.primitive;
                        VkDeviceSize voff =
                            static_cast<VkDeviceSize>(prim->vertex_byte_offset);
                        VkBuffer vb = item.vertexBuffer->handle();
                        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &voff);
                        vkCmdBindIndexBuffer(cb, item.indexBuffer->handle(), 0,
                                             item.indexBuffer->vk_index_type());
                        vkCmdDrawIndexed(cb, prim->index_count, 1,
                                         prim->first_index, prim->base_vertex,
                                         0);
                    }
                    ++pickDrawSlot;
                }
                vkCmdEndRenderPass(commandBuffer.handle());

                if (scene_pick_pending) {
                    VkBufferImageCopy copyRegion {};
                    copyRegion.bufferOffset = 0;
                    copyRegion.bufferRowLength = 0;
                    copyRegion.bufferImageHeight = 0;
                    copyRegion.imageSubresource.aspectMask =
                        VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.imageSubresource.mipLevel = 0;
                    copyRegion.imageSubresource.baseArrayLayer = 0;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset = {
                        static_cast<std::int32_t>(scene_pick_fb_x),
                        static_cast<std::int32_t>(scene_pick_fb_y), 0
                    };
                    copyRegion.imageExtent = { 1, 1, 1 };
                    vkCmdCopyImageToBuffer(
                        cb, pickIdTarget.color_image_vk(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        pickReadbackBuffer.handle(), 1, &copyRegion);

                    VkBufferMemoryBarrier bufBar {
                        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
                    };
                    bufBar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    bufBar.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    bufBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBar.buffer = pickReadbackBuffer.handle();
                    bufBar.offset = 0;
                    bufBar.size = sizeof(std::uint32_t);
                    vkCmdPipelineBarrier(
                        cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &bufBar,
                        0, nullptr);
                }

                if (show_id_map_viz) {
                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_transfer_src_to_shader_read(
                            cb, pickIdTarget.color_image_vk());

                    std::array<VkClearValue, 2> vizClears {};
                    vizClears[0].color = { { 0.0F, 0.0F, 0.0F, 1.0F } };
                    vizClears[1].depthStencil = { 1.0F, 0 };
                    VkRenderPassBeginInfo vizRp {
                        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                    };
                    vizRp.renderPass = offscreenRenderPass.handle();
                    vizRp.framebuffer = idMapVizTarget.framebuffer();
                    vizRp.renderArea.offset = { 0, 0 };
                    vizRp.renderArea.extent = idMapVizTarget.extent();
                    vizRp.clearValueCount =
                        static_cast<uint32_t>(vizClears.size());
                    vizRp.pClearValues = vizClears.data();
                    vkCmdBeginRenderPass(commandBuffer.handle(), &vizRp,
                                         VK_SUBPASS_CONTENTS_INLINE);
                    const VkExtent2D vizExt = idMapVizTarget.extent();
                    const float vw = static_cast<float>(vizExt.width);
                    const float vh = static_cast<float>(vizExt.height);
                    VkViewport vizVp { 0.0F, 0.0F, vw, vh, 0.0F, 1.0F };
                    vkCmdSetViewport(cb, 0, 1, &vizVp);
                    VkRect2D vizSc {
                        { 0, 0 },
                        vizExt
                    };
                    vkCmdSetScissor(cb, 0, 1, &vizSc);
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pickVizPipe.handle());
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pickVizPl.handle(), 0, 1,
                                            &idMapVizDs, 0, nullptr);
                    vkCmdDraw(cb, 3, 1, 0, 0);
                    vkCmdEndRenderPass(commandBuffer.handle());

                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_shader_read_to_undefined(
                            cb, pickIdTarget.color_image_vk());
                } else if (scene_pick_pending) {
                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_to_undefined(
                            cb, pickIdTarget.color_image_vk());
                }
            }
        }

        if (pbrDebugTileGrid) {
            VkRenderPassBeginInfo debugRpInfo {
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            };
            debugRpInfo.renderPass = debugTileTarget.render_pass();
            debugRpInfo.framebuffer = debugTileTarget.framebuffer();
            debugRpInfo.renderArea.offset = { 0, 0 };
            debugRpInfo.renderArea.extent = debugTileTarget.extent();
            debugRpInfo.clearValueCount =
                static_cast<uint32_t>(sceneClears.size());
            debugRpInfo.pClearValues = sceneClears.data();
            vkCmdBeginRenderPass(commandBuffer.handle(), &debugRpInfo,
                                 VK_SUBPASS_CONTENTS_INLINE);
            {
                const VkExtent2D ext = debugTileTarget.extent();
                const auto wf = static_cast<float>(ext.width);
                const float hf = static_cast<float>(ext.height);
                const glm::mat4 view = sceneCamera.view_matrix();
                const glm::vec3 eye = sceneCamera.eye_position();
                VkCommandBuffer cb = commandBuffer.handle();
                constexpr int DEBUG_TILE_COLS = 4;
                constexpr int DEBUG_TILE_ROWS = 4;
                constexpr int DEBUG_TILE_COUNT =
                    DEBUG_TILE_COLS * DEBUG_TILE_ROWS;
                const float cellWidth =
                    wf / static_cast<float>(DEBUG_TILE_COLS);
                const float cellHeight =
                    hf / static_cast<float>(DEBUG_TILE_ROWS);

                entt::registry &scene_reg_dbg = editorScene.registry();
                const glm::mat4 sponza_root_world_dbg =
                    lumen::scene::world_matrix(scene_reg_dbg, sponzaRootEntity);
                bool inspector_via_sub_dbg = false;
                for (const entt::entity e :
                     scene_reg_dbg.view<lumen::scene::SubMeshRendererComponent>()) {
                    const auto &smr =
                        scene_reg_dbg.get<lumen::scene::SubMeshRendererComponent>(
                            e);
                    if (smr.mesh == sponzaInspectorMesh) {
                        inspector_via_sub_dbg = true;
                        break;
                    }
                }
                std::vector<lumen::scene::RenderItem> pbrRenderItemsDbg;
                if (inspector_via_sub_dbg) {
                    lumen::scene::append_submesh_render_items(
                        sponzaAsset.geometry(), scene_reg_dbg, 0,
                        pbrRenderItemsDbg);
                    for (const lumen::scene::Mesh &mesh_part :
                         sponzaAsset.model) {
                        if (&mesh_part == sponzaInspectorMesh) {
                            continue;
                        }
                        lumen::scene::append_mesh_render_items(
                            sponzaAsset.geometry(), mesh_part,
                            sponza_root_world_dbg, 0, pbrRenderItemsDbg,
                            sponzaRootEntity);
                    }
                } else {
                    lumen::scene::append_model_render_items(
                        sponzaAsset.geometry(), sponzaAsset.model,
                        sponza_root_world_dbg, 0, pbrRenderItemsDbg,
                        sponzaRootEntity);
                }

                for (int tileIndex = 0; tileIndex < DEBUG_TILE_COUNT;
                     ++tileIndex) {
                    const int col = tileIndex % DEBUG_TILE_COLS;
                    const int row = tileIndex / DEBUG_TILE_COLS;
                    const float aspect =
                        cellWidth /
                        static_cast<float>((std::max)(1.0F, cellHeight));
                    glm::mat4 proj = glm::perspective(glm::radians(55.0F),
                                                      aspect, 0.05F, 120.0F);
                    proj[1][1] *= -1.0F;

                    for (uint32_t mi = 0; mi < sponzaMatCount; ++mi) {
                        lumen::render::PbrMaterialUbo mu {};
                        lumen::render::pack_pbr_material_ubo(
                            mu, pbrMaterials[mi], emissiveScale);
                        sponzaMaterialUbos[mi].update(mu);
                    }

                    lumen::render::PbrFrameUbo frameUTile {};
                    lumen::render::pack_pbr_frame_ubo(
                        frameUTile, view, proj, eye, skyExposure, iblStrength,
                        PREFILTER_MAX_LOD, 0.0F,
                        lumen::render::PBR_FORWARD_DEBUG_TILE_MODES.at(
                            static_cast<std::size_t>(tileIndex)));
                    helmetFrameUbos[frameIndex].update(frameUTile);

                    lumen::render::PbrLightUbo lightUTile {};
                    lumen::render::fill_pbr_light_ubo_default_points(
                        lightUTile, pointLightCount, pointDirectStrength);
                    helmetLightUbos[frameIndex].update(lightUTile);

                    VkViewport viewportTile {};
                    viewportTile.x = static_cast<float>(col) * cellWidth;
                    // 与「PBR 分屏调试」ImGui 角标自上而下（row 0 在上）一致
                    viewportTile.y = static_cast<float>(row) * cellHeight;
                    viewportTile.width = cellWidth;
                    viewportTile.height = cellHeight;
                    viewportTile.minDepth = 0.0F;
                    viewportTile.maxDepth = 1.0F;
                    vkCmdSetViewport(cb, 0, 1, &viewportTile);
                    const int32_t sx =
                        static_cast<int32_t>(std::lround(viewportTile.x));
                    const int32_t sy =
                        static_cast<int32_t>(std::lround(viewportTile.y));
                    const uint32_t scissorWidth = static_cast<uint32_t>((
                        std::max)(1L,
                                  std::lround(static_cast<double>(cellWidth))));
                    const uint32_t scissorHeight = static_cast<uint32_t>(
                        (std::max)(1L, std::lround(
                                           static_cast<double>(cellHeight))));
                    VkRect2D scissorTile { { sx, sy },
                                           { scissorWidth, scissorHeight } };
                    vkCmdSetScissor(cb, 0, 1, &scissorTile);

                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      helmetPipe.handle());
                    uint32_t drawSlotDbg = 0;
                    for (const lumen::scene::RenderItem &itemDbg :
                         pbrRenderItemsDbg) {
                        if (!itemDbg.is_valid_for_draw()) {
                            continue;
                        }
                        const lumen::scene::Primitive *prim = itemDbg.primitive;
                        const lumen::render::Material *primMaterialDbg =
                            itemDbg.material != nullptr ? itemDbg.material
                                                        : &pbrMaterials[0];
                        VkDescriptorSet materialDescriptorSetDbg =
                            vk_descriptor_set_for_pbr_material(
                                primMaterialDbg, pbrMaterials,
                                sponzaMaterialDs);
                        lumen::render::PbrObjectUbo objectUboDbg {};
                        objectUboDbg.model = itemDbg.model;
                        const glm::mat3 n3d = glm::mat3(itemDbg.model);
                        objectUboDbg.normalMatrix =
                            glm::mat4(glm::transpose(glm::inverse(n3d)));
                        helmetObjectUbo.update(objectUboDbg,
                                               drawSlotDbg * helmetObjStride);
                        std::array<VkDescriptorSet, 4> descriptorSetsDbg {
                            helmetFrameDs[frameIndex], materialDescriptorSetDbg,
                            helmetObjectDs, helmetLightDs[frameIndex]
                        };
                        const uint32_t dynamicOffsetDbg = static_cast<uint32_t>(
                            drawSlotDbg * helmetObjStride);
                        vkCmdBindDescriptorSets(
                            cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            helmetPl.handle(), 0,
                            static_cast<uint32_t>(descriptorSetsDbg.size()),
                            descriptorSetsDbg.data(), 1, &dynamicOffsetDbg);
                        ++drawSlotDbg;
                        VkDeviceSize voff =
                            static_cast<VkDeviceSize>(prim->vertex_byte_offset);
                        VkBuffer vbd = itemDbg.vertexBuffer->handle();
                        vkCmdBindVertexBuffers(cb, 0, 1, &vbd, &voff);
                        vkCmdBindIndexBuffer(
                            cb, itemDbg.indexBuffer->handle(), 0,
                            itemDbg.indexBuffer->vk_index_type());
                        vkCmdDrawIndexed(cb, prim->index_count, 1,
                                         prim->first_index, prim->base_vertex,
                                         0);
                    }
                }
            }
            vkCmdEndRenderPass(commandBuffer.handle());
        }

        VkClearValue swapClear {};
        swapClear.color = { { 0.07F, 0.08F, 0.11F, 1.0F } };
        VkRenderPassBeginInfo swapRpInfo {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        swapRpInfo.renderPass = renderPass.handle();
        swapRpInfo.framebuffer = framebuffers.get(imageIndex);
        swapRpInfo.renderArea.offset = { 0, 0 };
        swapRpInfo.renderArea.extent = swapchain.extent();
        swapRpInfo.clearValueCount = 1;
        swapRpInfo.pClearValues = &swapClear;
        vkCmdBeginRenderPass(commandBuffer.handle(), &swapRpInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        {
            const VkExtent2D swapchainExtent = swapchain.extent();
            VkViewport framebufferViewport {};
            framebufferViewport.x = 0.0F;
            framebufferViewport.y = 0.0F;
            framebufferViewport.width =
                static_cast<float>(swapchainExtent.width);
            framebufferViewport.height =
                static_cast<float>(swapchainExtent.height);
            framebufferViewport.minDepth = 0.0F;
            framebufferViewport.maxDepth = 1.0F;
            vkCmdSetViewport(commandBuffer.handle(), 0, 1,
                             &framebufferViewport);
            VkRect2D framebufferScissor { { 0, 0 }, swapchainExtent };
            vkCmdSetScissor(commandBuffer.handle(), 0, 1, &framebufferScissor);
        }

        imguiLayer.end_frame(commandBuffer.handle());

        vkCmdEndRenderPass(commandBuffer.handle());
        if (!commandBuffer.end()) {
            LUMEN_APP_LOG_ERROR("CommandBuffer::end 失败 frameIndex={}",
                                frameIndex);
            continue;
        }

        VkSemaphore waitSemaphore = frameSync.image_available(frameIndex);
        VkSemaphore signalSemaphore = frameSync.render_finished(imageIndex);
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkCommandBuffer submitCommandBuffer = commandBuffer.handle();
        VkSubmitInfo sub { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &waitSemaphore;
        sub.pWaitDstStageMask = &waitStage;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &submitCommandBuffer;
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &signalSemaphore;

        if (!frameSync.reset_fence(frameIndex)) {
            LUMEN_APP_LOG_ERROR("vkResetFences 失败 frameIndex={}", frameIndex);
            continue;
        }
        const VkResult submitResult =
            vkQueueSubmit(ctx.graphics_queue(), 1, &sub,
                          frameSync.in_flight_fence(frameIndex));
        if (submitResult != VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("vkQueueSubmit 失败 result={}",
                                static_cast<int>(submitResult));
            ctx.wait_idle();
            if (!frameSync.recreate_in_flight_fence_signaled(frameIndex)) {
                LUMEN_APP_LOG_ERROR(
                    "submit 失败后 fence 恢复失败 frameIndex={}", frameIndex);
                running = false;
            }
            continue;
        }

        if (record_scene_pick) {
            const VkFence pick_fence = frameSync.in_flight_fence(frameIndex);
            vkWaitForFences(dev, 1, &pick_fence, VK_TRUE, UINT64_MAX);
            void *const pickMap = pickReadbackBuffer.map();
            if (pickMap != nullptr) {
                pickReadbackBuffer.invalidate_mapped_range(
                    0, sizeof(std::uint32_t));
                const std::uint32_t enc =
                    *static_cast<const std::uint32_t *>(pickMap);
                pickReadbackBuffer.unmap();
                const entt::entity picked =
                    lumen::scene::decode_pick_entity_id(enc);
                entt::registry &pick_reg = editorScene.registry();
                editorSelection.entity =
                    pick_reg.valid(picked) ? picked : entt::null;

                if (enc == 0U) {
                    LUMEN_APP_LOG_INFO(
                        "Scene 拾取: 像素 ({}, {}) —— 背景或未命中（R32 编码 0）",
                        scene_pick_fb_x, scene_pick_fb_y);
                } else if (!pick_reg.valid(picked)) {
                    LUMEN_APP_LOG_WARN(
                        "Scene 拾取: 像素 ({}, {}) R32 编码 {} 解码为无效实体 "
                        "（entt::to_integral={}）",
                        scene_pick_fb_x, scene_pick_fb_y, enc,
                        static_cast<std::uint32_t>(entt::to_integral(picked)));
                } else {
                    std::string tag_str { "<无 TagComponent>" };
                    if (const auto *tc =
                            pick_reg.try_get<lumen::scene::TagComponent>(
                                picked)) {
                        tag_str = tc->tag;
                    }
                    std::string core_id_str { "-" };
                    if (const auto *idc =
                            pick_reg.try_get<lumen::scene::IDComponent>(
                                picked)) {
                        core_id_str = std::format(
                            "{:016x}",
                            static_cast<std::uint64_t>(idc->id));
                    }
                    std::string mesh_note;
                    if (const auto *smr =
                            pick_reg.try_get<
                                lumen::scene::SubMeshRendererComponent>(
                                picked)) {
                        mesh_note = std::format(
                            " | SubMeshRenderer primitiveIndex={} "
                            "meshPrimitiveCount={}",
                            smr->primitiveIndex,
                            smr->mesh != nullptr
                                ? smr->mesh->primitives.size()
                                : 0U);
                    } else if (const auto *mr =
                                   pick_reg.try_get<
                                       lumen::scene::MeshRendererComponent>(
                                       picked)) {
                        mesh_note = std::format(
                            " | MeshRenderer meshPrimitiveCount={}",
                            mr->mesh != nullptr
                                ? mr->mesh->primitives.size()
                                : 0U);
                    }
                    LUMEN_APP_LOG_INFO(
                        "Scene 拾取: 像素 ({}, {}) R32 编码={} "
                        "entt::to_integral={} core::ID(hex)={} 标签=\"{}\"{}",
                        scene_pick_fb_x, scene_pick_fb_y, enc,
                        static_cast<std::uint32_t>(entt::to_integral(picked)),
                        core_id_str, tag_str, mesh_note);
                }
            }
            scene_pick_pending = false;
        }

        const VkResult pr =
            swapchain.present(ctx.present_queue(), imageIndex, signalSemaphore);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
            LUMEN_APP_LOG_WARN("present OUT_OF_DATE，将重建 Swapchain");
            needRecreateSwapchain = true;
        } else if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) {
            LUMEN_APP_LOG_ERROR("present 失败 result={}", static_cast<int>(pr));
        }

        frameIndex = (frameIndex + 1) % 3;
    }

    window.set_relative_mouse_mode(false);
    vkDeviceWaitIdle(dev);
    if (sceneTexId != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(sceneTexId));
    }
    if (debugSceneTexId != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(debugSceneTexId));
    }
    if (idMapVizTexId != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(idMapVizTexId));
    }
    lumen::ui::imgui_backend_shutdown();

    for (VkImageView fv : envFaceViews) {
        destroy_image_view(dev, fv);
    }
    for (VkImageView fv : irrFaceViews) {
        destroy_image_view(dev, fv);
    }
    for (VkImageView fv : preFaceViews) {
        destroy_image_view(dev, fv);
    }

    std::vector<lumen::render::CommandBuffer> freeBuffers;
    for (auto &c : commandBuffers) {
        freeBuffers.push_back(std::move(c));
    }
    cmdPool.free(freeBuffers);

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
