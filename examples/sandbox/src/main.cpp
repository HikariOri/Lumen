/**
 * @file main.cpp
 * @brief PBR — IBL 烘焙、HDR 天空盒、Sponza（glTF 多 primitive / 多材质）
 */

#include "asset/asset_registry.hpp"
#include "asset/geometry/mesh_asset.hpp"
#include "core/log/logger.hpp"
#include "core/path.hpp"
#include "ibl_bake.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/frame_sync.hpp"
#include "render/material/material.hpp"
#include "render/material/pbr_forward_ubo.hpp"
#include "render/material/pbr_material_bind.hpp"
#include "render/pass/pick_id_render_target.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/pbr_forward_record_render_items.hpp"
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
#include "scene/components.hpp"
#include "scene/id_lookup.hpp"
#include "scene/pick.hpp"
#include "scene/render.hpp"
#include "scene/scene.hpp"
#include "scene/scene_camera.hpp"
#include "scene/scene_mesh_asset.hpp"
#include "scene/scene_mesh_spawn.hpp"
#include "scene/scene_orbit_controller.hpp"
#include "scene/transform.hpp"
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
#include <ghc/filesystem.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "render/vulkan.hpp"

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
    // "assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf"
    // "assets/glTF-Sample-Assets/Models/FlightHelmet/glTF/FlightHelmet.gltf"
    "assets/glTF-Sample-Assets/Models/Lantern/glTF/Lantern.gltf"
};

/// 「Scene」视口 ImGuizmo 模式（须在本帧 `imgui_scene_viewport_panel`
/// 之前写入）
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

[[nodiscard]] vk::DescriptorSet vk_descriptor_set_for_pbr_material(
    const lumen::render::Material *material,
    const std::unordered_map<const lumen::render::Material *, uint32_t>
        &materialToDsIndex,
    const std::vector<vk::DescriptorSet> &materialSets) {
    if (materialSets.empty()) {
        return {};
    }
    if (material == nullptr) {
        return materialSets[0];
    }
    const auto it = materialToDsIndex.find(material);
    if (it == materialToDsIndex.end() ||
        static_cast<size_t>(it->second) >= materialSets.size()) {
        return materialSets[0];
    }
    return materialSets[static_cast<size_t>(it->second)];
}

/** @brief 立方体贴图某一面的 2D 视图（@a base_array_layer 0…5 对应 +X −X +Y −Y
 * +Z −Z） */
[[nodiscard]] vk::ImageView
create_cubemap_face_2d_view(vk::Device device, vk::Image img, vk::Format format,
                            uint32_t mipLevel, uint32_t baseArrayLayer,
                            const char *label) {
    vk::ImageViewCreateInfo createInfo {};
    createInfo.image = img;
    createInfo.viewType = vk::ImageViewType::e2D;
    createInfo.format = format;
    createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    createInfo.subresourceRange.baseMipLevel = mipLevel;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    createInfo.subresourceRange.layerCount = 1;
    vk::ImageView outView;
    const vk::Result createResult =
        device.createImageView(&createInfo, nullptr, &outView);
    if (createResult != vk::Result::eSuccess) {
        LUMEN_APP_LOG_ERROR("createImageView 失败 ({}) result={}", label,
                            static_cast<int>(createResult));
        return {};
    }
    return outView;
}

void destroy_image_view(vk::Device device, vk::ImageView imageView) {
    if (imageView) {
        device.destroyImageView(imageView);
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
                             vk::ImageView {})) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig offscreenRpConfig;
    offscreenRpConfig.useDepth = true;
    offscreenRpConfig.colorAttachment.format = swapchain.image_format();
    offscreenRpConfig.colorAttachment.finalLayout =
        vk::ImageLayout::eShaderReadOnlyOptimal;
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
        sceneCfg.colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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
            vk::ImageLayout::eShaderReadOnlyOptimal;
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
        vizCfg.colorFinalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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

    const vk::Device dev = ctx.device();
    const vk::Format iblFormat = vk::Format::eR32G32B32A32Sfloat;

    std::array<vk::ImageView, 6> envFaceViews {};
    std::array<vk::ImageView, 6> irrFaceViews {};
    std::array<vk::ImageView, 6> preFaceViews {};
    for (uint32_t face = 0; face < 6; ++face) {
        char envLabel[48];
        std::snprintf(envLabel, sizeof envLabel, "IBL environment face %u",
                      face);
        envFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.environment.image(), iblFormat, 0, face, envLabel);
        char irrLabel[48];
        std::snprintf(irrLabel, sizeof irrLabel, "IBL irradiance face %u",
                      face);
        irrFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.irradiance.image(), iblFormat, 0, face, irrLabel);
        char preLabel[48];
        std::snprintf(preLabel, sizeof preLabel, "IBL prefilter face %u", face);
        preFaceViews[face] = create_cubemap_face_2d_view(
            dev, ibl.prefilter.image(), iblFormat, 0, face, preLabel);
    }
    const vk::ImageView brdfLutView = ibl.brdf_lut.view();

    auto allSixValid = [](const std::array<vk::ImageView, 6> &a) -> bool {
        for (vk::ImageView v : a) {
            if (!v) {
                return false;
            }
        }
        return true;
    };
    const bool envViewsOk = allSixValid(envFaceViews);
    const bool irrViewsOk = allSixValid(irrFaceViews);
    const bool preViewsOk = allSixValid(preFaceViews);
    if (!envViewsOk || !irrViewsOk || !preViewsOk || !brdfLutView) {
        LUMEN_APP_LOG_ERROR(
            "ImGui 预览用 ImageView 无效: env6={} irr6={} pre6={} brdf={}",
            envViewsOk, irrViewsOk, preViewsOk, static_cast<bool>(brdfLutView));
        for (vk::ImageView fv : envFaceViews) {
            destroy_image_view(dev, fv);
        }
        for (vk::ImageView fv : irrFaceViews) {
            destroy_image_view(dev, fv);
        }
        for (vk::ImageView fv : preFaceViews) {
            destroy_image_view(dev, fv);
        }
        return -1;
    }

    namespace fs = ghc::filesystem;

    const std::string sponzaPath =
        lumen::core::get_resource_path(SPONZA_GLTF_REL);
    if (!fs::exists(fs::path(sponzaPath))) {
        LUMEN_APP_LOG_ERROR("未找到 Sponza glTF: {}（含同级纹理）", sponzaPath);
        return -1;
    }

    vk::Queue gq = ctx.graphics_queue();

    lumen::render::PbrPlaceholderTextures pbrPlaceholders;
    if (!pbrPlaceholders.create(ctx, gq, cmdPool) ||
        !pbrPlaceholders.is_complete()) {
        LUMEN_APP_LOG_ERROR("PBR 占位贴图创建失败");
        return -1;
    }

    lumen::scene::SceneMeshLoadOptions sponzaLoadOpts {};
    sponzaLoadOpts.recenterToOrigin = true;
    sponzaLoadOpts.uniformScaleMaxAxis = 1.8F;
    std::string sponzaLoadErr;
    const lumen::asset::SceneMeshAssetHandle sponza_mesh_handle =
        lumen::asset::AssetRegistry::instance().load_scene_mesh_handle(
            ctx, gq, cmdPool, sponzaPath, sponzaLoadOpts, &sponzaLoadErr);
    if (!sponza_mesh_handle.valid()) {
        LUMEN_APP_LOG_ERROR("Sponza 加载失败: {}",
                            sponzaLoadErr.empty() ? "unknown" : sponzaLoadErr);
        return -1;
    }
    const std::shared_ptr<lumen::scene::SceneMeshAsset> sponza_asset_sp =
        lumen::asset::AssetRegistry::instance().try_get_scene_mesh(
            sponza_mesh_handle);
    if (!sponza_asset_sp) {
        LUMEN_APP_LOG_ERROR("Sponza 场景网格句柄解析失败");
        return -1;
    }
    lumen::scene::SceneMeshAsset &sponzaAsset = *sponza_asset_sp;
    if (sponzaAsset.materials.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 无材质");
        return -1;
    }

    std::unordered_map<const lumen::render::Material *, uint32_t>
        sponzaMaterialToDsIndex;
    std::vector<const lumen::render::Material *> sponzaUniqueMaterials;
    for (const auto &matInst : sponzaAsset.materials) {
        if (!matInst) {
            continue;
        }
        const lumen::render::Material *const p = &matInst->material;
        if (sponzaMaterialToDsIndex.find(p) != sponzaMaterialToDsIndex.end()) {
            continue;
        }
        const uint32_t idx =
            static_cast<uint32_t>(sponzaUniqueMaterials.size());
        sponzaMaterialToDsIndex[p] = idx;
        sponzaUniqueMaterials.push_back(p);
    }
    if (sponzaUniqueMaterials.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 无有效材质实例");
        return -1;
    }
    const lumen::render::Material *const sponzaDefaultMaterial =
        sponzaUniqueMaterials[0];
    std::size_t sponza_prim_definition_count = 0;
    for (const lumen::asset::geometry::Mesh &m : sponzaAsset.model) {
        sponza_prim_definition_count += m.primitives.size();
    }
    if (sponza_prim_definition_count == 0) {
        LUMEN_APP_LOG_ERROR("Sponza 无 mesh/primitive");
        return -1;
    }

    lumen::scene::Scene editorScene;
    lumen::ui::EditorSelection editorSelection {};
    lumen::scene::SceneMeshSpawnOptions sponza_spawn_opts {};
    sponza_spawn_opts.owning_scene = sponza_asset_sp;
    sponza_spawn_opts.scene_mesh_handle = sponza_mesh_handle;
    const lumen::scene::SceneMeshSpawnResult sponza_spawn =
        lumen::scene::spawn_scene_mesh_hierarchy(
            editorScene, sponzaAsset, "SponzaSub", sponza_spawn_opts);
    if (sponza_spawn.node_entities.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza glTF 场景节点为空");
        return -1;
    }
    {
        const lumen::asset::geometry::MeshBuffer gb = sponzaAsset.geometry();
        for (const entt::entity ent :
             editorScene.registry()
                 .view<lumen::scene::SubMeshInstanceRefRendererComponent>()) {
            const auto &sr =
                editorScene.registry()
                    .get<lumen::scene::SubMeshInstanceRefRendererComponent>(
                        ent);
            const auto rp = sr.submeshRef.resolve();
            if (!rp.has_value() ||
                rp->meshBuffer.vertexBuffer != gb.vertexBuffer ||
                rp->meshBuffer.indexBuffer != gb.indexBuffer) {
                LUMEN_APP_LOG_ERROR("SubMeshInstanceRef 与 geometry() "
                                    "不一致（注册表/弱引用异常）");
                return -1;
            }
            break;
        }
    }
    {
        std::string meshFmtErr;
        const std::string triObj =
            lumen::core::get_resource_path("assets/meshes/triangle.obj");
        if (const auto sp =
                lumen::asset::AssetRegistry::instance().load_scene_mesh(
                    ctx, gq, cmdPool, triObj, {}, &meshFmtErr)) {
            LUMEN_APP_LOG_INFO("Mesh OBJ 烟测: {} 顶点", sp->statsVertexCount);
        } else {
            LUMEN_APP_LOG_WARN("Mesh OBJ 烟测跳过: {}",
                               meshFmtErr.empty() ? "?" : meshFmtErr);
        }
        meshFmtErr.clear();
        const std::string triLm =
            lumen::core::get_resource_path("assets/meshes/triangle.lumenmesh");
        if (const auto sp2 =
                lumen::asset::AssetRegistry::instance().load_scene_mesh(
                    ctx, gq, cmdPool, triLm, {}, &meshFmtErr)) {
            LUMEN_APP_LOG_INFO("Mesh .lumenmesh 烟测: {} 顶点",
                               sp2->statsVertexCount);
        } else {
            LUMEN_APP_LOG_WARN("Mesh .lumenmesh 烟测跳过: {}",
                               meshFmtErr.empty() ? "?" : meshFmtErr);
        }
    }
    editorSelection.entity = sponza_spawn.node_entities.front();

    const uint32_t sponzaDrawSlots =
        (std::max)(sponza_spawn.drawable_primitive_instances, 1u);
    const std::size_t helmetObjStride =
        lumen::render::pbr_object_ubo_dynamic_stride(static_cast<std::size_t>(
            ctx.physical_device_properties()
                .limits.minUniformBufferOffsetAlignment));
    const VkDeviceSize helmetObjectUboBytes =
        static_cast<VkDeviceSize>(helmetObjStride) *
        static_cast<VkDeviceSize>((std::max)(sponzaDrawSlots, 1u));

    LUMEN_APP_LOG_INFO(
        "Sponza: 顶点={} 索引={} 三角≈{} mesh={} 可绘制 primitive={} "
        "材质槽={} 去重后PBR材质种类={}",
        sponzaAsset.statsVertexCount, sponzaAsset.statsIndexCount,
        sponzaAsset.statsIndexCount / 3U, sponzaAsset.model.size(),
        sponzaDrawSlots, sponzaAsset.materials.size(),
        sponzaUniqueMaterials.size());

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
            sceneSampler.handle(),
            static_cast<VkImageView>(sceneTarget.color_view()),
            static_cast<VkImageLayout>(sceneTarget.color_sample_layout())));
    auto debugSceneTexId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(),
            static_cast<VkImageView>(debugTileTarget.color_view()),
            static_cast<VkImageLayout>(debugTileTarget.color_sample_layout())));

    auto idMapVizTexId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(),
            static_cast<VkImageView>(idMapVizTarget.color_view()),
            static_cast<VkImageLayout>(idMapVizTarget.color_sample_layout())));

    lumen::render::Sampler pickIdNearestSampler;
    {
        lumen::render::SamplerConfig sc {};
        sc.magFilter = vk::Filter::eNearest;
        sc.minFilter = vk::Filter::eNearest;
        sc.mipmapMode = vk::SamplerMipmapMode::eNearest;
        sc.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sc.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        sc.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
    };
    if (!skyDsl.create(ctx, skyBinds)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorSetLayout 失败");
        return -1;
    }

    lumen::render::DescriptorPool skyDpool;
    if (!skyDpool.create(ctx,
                         { { .type = vk::DescriptorType::eCombinedImageSampler,
                             .count = 1 } },
                         1)) {
        LUMEN_APP_LOG_ERROR("天空盒 DescriptorPool 失败");
        return -1;
    }

    vk::DescriptorSet skyDs {};
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
        { smSkyVs.handle(), vk::ShaderStageFlagBits::eVertex, "main" });
    skyCfg.shaderStages.push_back(
        { smSkyFs.handle(), vk::ShaderStageFlagBits::eFragment, "main" });
    skyCfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(float) * 3,
          .inputRate = vk::VertexInputRate::eVertex });
    skyCfg.vertexAttributes.push_back({ .location = 0,
                                        .binding = 0,
                                        .format = vk::Format::eR32G32B32Sfloat,
                                        .offset = 0 });
    skyCfg.depthTest = true;
    skyCfg.depthWrite = false;
    skyCfg.depthCompareOp = vk::CompareOp::eLessOrEqual;
    skyCfg.cullMode = vk::CullModeFlagBits::eFront;
    skyCfg.frontFace = vk::FrontFace::eClockwise;

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

    const auto sponzaUniqueMatCount =
        static_cast<uint32_t>(sponzaUniqueMaterials.size());
    const uint32_t PBR_UBO_STATIC = 3u + sponzaUniqueMatCount + 3u;
    const uint32_t PBR_SET_COUNT = 3u + sponzaUniqueMatCount + 1u + 3u;
    const uint32_t PBR_COMBINED_FOR_MATERIALS = sponzaUniqueMatCount * 5u;
    const uint32_t PBR_COMBINED_TOTAL = 9u + PBR_COMBINED_FOR_MATERIALS;

    lumen::render::DescriptorPool pbrDpool;
    if (!pbrDpool.create(
            ctx,
            { { .type = vk::DescriptorType::eUniformBuffer,
                .count = PBR_UBO_STATIC },
              { .type = vk::DescriptorType::eUniformBufferDynamic, .count = 1 },
              { .type = vk::DescriptorType::eCombinedImageSampler,
                .count = PBR_COMBINED_TOTAL } },
            PBR_SET_COUNT)) {
        LUMEN_APP_LOG_ERROR("PBR DescriptorPool 失败 (材质数={})",
                            sponzaUniqueMatCount);
        return -1;
    }

    std::vector<lumen::render::UniformBuffer> sponzaMaterialUbos(
        sponzaUniqueMatCount);
    for (uint32_t mi = 0; mi < sponzaUniqueMatCount; ++mi) {
        if (!sponzaMaterialUbos[mi].create_persistent(
                ctx, sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 失败 mi={}", mi);
            return -1;
        }
    }

    std::vector<vk::DescriptorSet> sponzaMaterialDs(sponzaUniqueMatCount);
    for (uint32_t mi = 0; mi < sponzaUniqueMatCount; ++mi) {
        if (!pbrDpool.allocate(dev, helmetMaterialDsl.handle(),
                               sponzaMaterialDs[mi])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败 mi={}", mi);
            return -1;
        }
        lumen::render::PbrMaterialUbo mu {};
        lumen::render::pack_pbr_material_ubo(mu, *sponzaUniqueMaterials[mi],
                                             3.0F);
        sponzaMaterialUbos[mi].update(mu);
        lumen::render::write_pbr_material_descriptor_set(
            dev, sponzaMaterialDs[mi], sponzaMaterialUbos[mi].handle(),
            sizeof(lumen::render::PbrMaterialUbo), *sponzaUniqueMaterials[mi],
            pbrPlaceholders);
    }

    std::array<vk::DescriptorSet, 3> helmetFrameDs {};
    for (uint32_t i = 0; i < helmetFrameDs.size(); ++i) {
        if (!pbrDpool.allocate(dev, helmetFrameDsl.handle(),
                               helmetFrameDs[i])) {
            LUMEN_APP_LOG_ERROR("PBR Frame DescriptorSet 分配失败");
            return -1;
        }
    }

    std::array<lumen::render::UniformBuffer, 3> helmetFrameUbos {};
    for (auto &helmetFrameUbo : helmetFrameUbos) {
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

    vk::DescriptorSet helmetObjectDs {};
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

    std::array<vk::DescriptorSet, 3> helmetLightDs {};
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
        { smHelmetVs.handle(), vk::ShaderStageFlagBits::eVertex, "main" });
    helmetCfg.shaderStages.push_back(
        { smHelmetFs.handle(), vk::ShaderStageFlagBits::eFragment, "main" });
    helmetCfg.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(HelmVertex),
          .inputRate = vk::VertexInputRate::eVertex });
    helmetCfg.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = vk::Format::eR32G32B32Sfloat,
          .offset = offsetof(HelmVertex, position) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 1,
          .binding = 0,
          .format = vk::Format::eR32G32B32Sfloat,
          .offset = offsetof(HelmVertex, normal) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 2,
          .binding = 0,
          .format = vk::Format::eR32G32Sfloat,
          .offset = offsetof(HelmVertex, uv) });
    helmetCfg.vertexAttributes.push_back(
        { .location = 3,
          .binding = 0,
          .format = vk::Format::eR32G32B32A32Sfloat,
          .offset = offsetof(HelmVertex, tangent) });
    // 关闭剔除，避免绕序/双面导致整模不可见；确认后可改回 BACK + CCW/CW
    helmetCfg.cullMode = vk::CullModeFlagBits::eNone;
    helmetCfg.frontFace = vk::FrontFace::eCounterClockwise;

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
        if (!pickIdPl.create(
                ctx, { helmetFrameDsl.handle(), helmetObjectDsl.handle() },
                { pcRange })) {
            LUMEN_APP_LOG_ERROR("Pick ID PipelineLayout 失败");
            return -1;
        }
    }
    lumen::render::GraphicsPipelineConfig pickIdCfg {};
    pickIdCfg.shaderStages.push_back(
        { smPickIdVs.handle(), vk::ShaderStageFlagBits::eVertex, "main" });
    pickIdCfg.shaderStages.push_back(
        { smPickIdFs.handle(), vk::ShaderStageFlagBits::eFragment, "main" });
    pickIdCfg.vertexBindings = helmetCfg.vertexBindings;
    pickIdCfg.vertexAttributes = helmetCfg.vertexAttributes;
    pickIdCfg.cullMode = vk::CullModeFlagBits::eNone;
    pickIdCfg.frontFace = vk::FrontFace::eCounterClockwise;
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
        bi.usage = vk::BufferUsageFlagBits::eTransferDst;
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
            ctx, { { .binding = 0,
                     .type = vk::DescriptorType::eCombinedImageSampler,
                     .count = 1,
                     .stages = vk::ShaderStageFlagBits::eFragment } })) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorSetLayout 失败");
        return -1;
    }
    lumen::render::DescriptorPool idMapVizDpool;
    if (!idMapVizDpool.create(
            ctx,
            { { .type = vk::DescriptorType::eCombinedImageSampler,
                .count = 1 } },
            1)) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorPool 失败");
        return -1;
    }
    vk::DescriptorSet idMapVizDs {};
    if (!idMapVizDpool.allocate(dev, idMapVizDsl.handle(), idMapVizDs)) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::write_descriptor_image(
        dev, idMapVizDs, 0, pickIdTarget.color_image().view(),
        pickIdNearestSampler.handle(), vk::ImageLayout::eShaderReadOnlyOptimal);

    lumen::render::PipelineLayout pickVizPl;
    if (!pickVizPl.create(ctx, { idMapVizDsl.handle() }, {})) {
        LUMEN_APP_LOG_ERROR("ID Map 可视化 PipelineLayout 失败");
        return -1;
    }
    lumen::render::GraphicsPipelineConfig pickVizCfg {};
    pickVizCfg.shaderStages.push_back(
        { smPickVizVs.handle(), vk::ShaderStageFlagBits::eVertex, "main" });
    pickVizCfg.shaderStages.push_back(
        { smPickVizFs.handle(), vk::ShaderStageFlagBits::eFragment, "main" });
    pickVizCfg.cullMode = vk::CullModeFlagBits::eNone;
    pickVizCfg.frontFace = vk::FrontFace::eCounterClockwise;
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
                uiSampler.handle(),
                static_cast<VkImageView>(envFaceViews[face]),
                static_cast<VkImageLayout>(
                    vk::ImageLayout::eShaderReadOnlyOptimal)));
        texIrrFaces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(),
                static_cast<VkImageView>(irrFaceViews[face]),
                static_cast<VkImageLayout>(
                    vk::ImageLayout::eShaderReadOnlyOptimal)));
        texPreFaces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(),
                static_cast<VkImageView>(preFaceViews[face]),
                static_cast<VkImageLayout>(
                    vk::ImageLayout::eShaderReadOnlyOptimal)));
    }
    auto texBrdf =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), static_cast<VkImageView>(brdfLutView),
            static_cast<VkImageLayout>(
                vk::ImageLayout::eShaderReadOnlyOptimal)));

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
                           vk::ImageView {})) {
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
                vk::ImageLayout::eShaderReadOnlyOptimal);
            sceneTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(),
                    static_cast<VkImageView>(sceneTarget.color_view()),
                    static_cast<VkImageLayout>(
                        sceneTarget.color_sample_layout())));
            debugSceneTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(),
                    static_cast<VkImageView>(debugTileTarget.color_view()),
                    static_cast<VkImageLayout>(
                        debugTileTarget.color_sample_layout())));
            idMapVizTexId = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    sceneSampler.handle(),
                    static_cast<VkImageView>(idMapVizTarget.color_view()),
                    static_cast<VkImageLayout>(
                        idMapVizTarget.color_sample_layout())));
        }

        if (swapchain.extent().width == 0 || swapchain.extent().height == 0) {
            SDL_Delay(16);
            continue;
        }

        const uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(frameIndex), {}, ACQUIRE_TIMEOUT_NS);
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
            ImGui::TextUnformatted("在「Scene」视口内左键点选 SubMesh / "
                                   "根网格（ID Map）；层级与检视器"
                                   "一致。选中后可拖 Gizmo。未在输入文字且非 "
                                   "Alt 时按 F 平滑对焦（鼠标在"
                                   " Scene 图像上或该视口已记为悬停）。");
            ImGui::TextUnformatted(
                "工具快捷键（与 Unity 一致）：W 平移 | E 旋转 | R "
                "缩放。视口悬停时生效；"
                "按住右键飞行（WASD / Q 下 / E 上）时暂时不响应 "
                "W/E/R，避免与相机移动冲突。");
            ImGui::RadioButton("平移", &scene_gizmo_operation,
                               static_cast<int>(ImGuizmo::TRANSLATE));
            ImGui::SameLine();
            ImGui::RadioButton("旋转", &scene_gizmo_operation,
                               static_cast<int>(ImGuizmo::ROTATE));
            ImGui::SameLine();
            ImGui::RadioButton("缩放", &scene_gizmo_operation,
                               static_cast<int>(ImGuizmo::SCALE));
        }
        ImGui::End();

        if (ImGui::Begin("ID Map 可视化")) {
            ImGui::Checkbox("启用（每帧渲染 ID Pass + 伪彩色）",
                            &show_id_map_viz);
            ImGui::TextUnformatted("与 Scene 同分辨率；深色为 ID "
                                   "0（背景），其余为编码后的实体哈希色。");
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
            &sceneViewHovered, onOrbitViewportScroll,
            onSceneViewportAfterImage);

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
                "Scene 图像上左键点选实体（非 Alt、非 Gizmo "
                "悬停）；未在输入文字时按 F 对焦");
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
            ImGui::Text("场景网格 mesh 数: %zu", sponzaAsset.model.size());
            ImGui::Text("Primitive 定义数（各 mesh 合计）: %zu",
                        sponza_prim_definition_count);
            ImGui::Text("场景节点数: %zu", sponzaAsset.scene_nodes.size());
            ImGui::Text("可绘制 primitive 实例数: %u", sponzaDrawSlots);
            ImGui::Text("材质槽位数: %zu", sponzaAsset.materials.size());
            ImGui::Text("去重后 PBR 材质种类: %zu",
                        sponzaUniqueMaterials.size());
            ImGui::TextWrapped(
                "几何在 mesh 局部空间；Node 变换在 ECS。每实例 SubMesh 绘制。");
        }
        ImGui::End();

        if (ImGui::Begin("ECS 渲染组件总览")) {
            ImGui::TextWrapped(
                "场景网格由 `spawn_scene_mesh_hierarchy` 生成：节点实体仅 "
                "`TransformComponent`；含 mesh 的节点下挂 SubMesh 子实体（每 "
                "primitive 一条），渲染走 `append_submesh_render_items`。");
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
                    if (const auto *t =
                            reg.try_get<lumen::scene::TagComponent>(ent)) {
                        tag = t->tag.c_str();
                    }
                    const auto &mr =
                        v.get<lumen::scene::MeshRendererComponent>(ent);
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
                const auto v =
                    reg.view<lumen::scene::SubMeshRendererComponent>();
                std::size_t count = 0;
                for (const entt::entity ent : v) {
                    ++count;
                    const char *tag = "?";
                    if (const auto *t =
                            reg.try_get<lumen::scene::TagComponent>(ent)) {
                        tag = t->tag.c_str();
                    }
                    const auto &sm =
                        v.get<lumen::scene::SubMeshRendererComponent>(ent);
                    const std::uint32_t pc =
                        sm.mesh != nullptr ? static_cast<std::uint32_t>(
                                                 sm.mesh->primitives.size())
                                           : 0;
                    const bool ok =
                        sm.mesh != nullptr && sm.primitiveIndex < pc &&
                        sm.mesh->primitives[sm.primitiveIndex].is_drawable();
                    ImGui::BulletText(
                        "%s  entity=%u  mesh=%p  primIndex=%u/%u  %s", tag,
                        static_cast<unsigned>(entt::to_integral(ent)),
                        static_cast<const void *>(sm.mesh), sm.primitiveIndex,
                        pc, ok ? "可绘制" : "无效或跳过");
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

        const bool sceneTexViewportHovered = sceneViewHovered ||
                                             debugSceneViewHovered ||
                                             mouse_over_scene_image_rect;
        const bool imguiBlocksSceneMouse =
            lumen::ui::imgui_wants_mouse() && !sceneTexViewportHovered;

        // Unity 式 Gizmo 切换：W/E/R；右键飞行（与轨道控制器 WASD+QE
        // 一致）时不处理
        const bool scene_fly_rmb = sceneTexViewportHovered &&
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
                scene_gizmo_operation = static_cast<int>(ImGuizmo::TRANSLATE);
            }
            if (e_down && !prev_key_e_down) {
                scene_gizmo_operation = static_cast<int>(ImGuizmo::ROTATE);
            }
            if (r_down && !prev_key_r_down) {
                scene_gizmo_operation = static_cast<int>(ImGuizmo::SCALE);
            }
            prev_key_w_down = w_down;
            prev_key_e_down = e_down;
            prev_key_r_down = r_down;
        } else {
            prev_key_w_down = pump.input().is_key_down(lumen::platform::Key::W);
            prev_key_e_down = pump.input().is_key_down(lumen::platform::Key::E);
            prev_key_r_down = pump.input().is_key_down(lumen::platform::Key::R);
        }

        const bool key_f_down =
            pump.input().is_key_down(lumen::platform::Key::F);
        const bool key_f_pressed = key_f_down && !prev_key_f_down;
        prev_key_f_down = key_f_down;
        // `WantCaptureKeyboard` 在 Dock 下几乎总为
        // true，会误挡快捷键；仅在有文本 输入意图时屏蔽 F。
        if (key_f_pressed && sceneTexViewportHovered &&
            !ImGui::GetIO().WantTextInput) {
            entt::registry &frame_reg = editorScene.registry();
            const entt::entity frame_sel = editorSelection.entity;
            if (frame_reg.valid(frame_sel)) {
                if (const auto *smr =
                        frame_reg
                            .try_get<lumen::scene::SubMeshRendererComponent>(
                                frame_sel)) {
                    if (smr->mesh != nullptr &&
                        smr->primitiveIndex < smr->mesh->primitives.size()) {
                        const lumen::asset::geometry::Primitive &prim =
                            smr->mesh->primitives[smr->primitiveIndex];
                        if (prim.is_drawable()) {
                            glm::vec3 half_ext = prim.localAabbHalfExtent;
                            if (glm::length(half_ext) < 1e-6F) {
                                half_ext = glm::vec3(0.5F);
                            }
                            if (const auto frame_targets = lumen::scene::
                                    frame_orbit_targets_for_drawable(
                                        orbit, frame_reg, frame_sel,
                                        prim.localPivot, half_ext)) {
                                orbit.begin_smooth_frame(frame_targets->first,
                                                         frame_targets->second);
                            }
                        }
                    }
                } else if (const auto *smir =
                               frame_reg.try_get<
                                   lumen::scene::
                                       SubMeshInstanceRefRendererComponent>(
                                   frame_sel)) {
                    const std::optional<
                        lumen::asset::SubMeshInstanceRef::ResolvedPrim>
                        rp = smir->submeshRef.resolve();
                    if (rp.has_value() && rp->primitive->is_drawable()) {
                        glm::vec3 half_ext = rp->primitive->localAabbHalfExtent;
                        if (glm::length(half_ext) < 1e-6F) {
                            half_ext = glm::vec3(0.5F);
                        }
                        if (const auto frame_targets =
                                lumen::scene::frame_orbit_targets_for_drawable(
                                    orbit, frame_reg, frame_sel,
                                    rp->primitive->localPivot, half_ext)) {
                            orbit.begin_smooth_frame(frame_targets->first,
                                                     frame_targets->second);
                        }
                    }
                } else if (const auto *mr =
                               frame_reg.try_get<
                                   lumen::scene::MeshRendererComponent>(
                                   frame_sel)) {
                    if (mr->mesh != nullptr) {
                        glm::vec3 mesh_center {};
                        glm::vec3 mesh_half {};
                        if (lumen::asset::geometry::drawable_mesh_local_bounds(
                                *mr->mesh, &mesh_center, &mesh_half)) {
                            if (const auto frame_targets = lumen::scene::
                                    frame_orbit_targets_for_drawable(
                                        orbit, frame_reg, frame_sel,
                                        mesh_center, mesh_half)) {
                                orbit.begin_smooth_frame(frame_targets->first,
                                                         frame_targets->second);
                            }
                        }
                    }
                } else if (const auto *mir =
                               frame_reg.try_get<
                                   lumen::scene::
                                       MeshInstanceRefRendererComponent>(
                                   frame_sel)) {
                    const std::optional<lumen::asset::MeshInstanceRef::Resolved>
                        rr = mir->meshRef.resolve();
                    if (rr.has_value() && rr->mesh != nullptr) {
                        glm::vec3 mesh_center {};
                        glm::vec3 mesh_half {};
                        if (lumen::asset::geometry::drawable_mesh_local_bounds(
                                *rr->mesh, &mesh_center, &mesh_half)) {
                            if (const auto frame_targets = lumen::scene::
                                    frame_orbit_targets_for_drawable(
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
                 reg.all_of<lumen::scene::SubMeshRendererComponent>(sel) ||
                 reg.all_of<lumen::scene::MeshInstanceRefRendererComponent>(
                     sel) ||
                 reg.all_of<lumen::scene::SubMeshInstanceRefRendererComponent>(
                     sel));
            if (!has_draw) {
                lumen::ui::imguizmo_reset_interaction_state();
            } else {
                const vk::Extent2D gizmo_ext = sceneTarget.extent();
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
                        pivot_mesh = smr->mesh->primitives[smr->primitiveIndex]
                                         .localPivot;
                    }
                } else if (const auto *smir =
                               reg.try_get<
                                   lumen::scene::
                                       SubMeshInstanceRefRendererComponent>(
                                   sel)) {
                    const std::optional<
                        lumen::asset::SubMeshInstanceRef::ResolvedPrim>
                        rp = smir->submeshRef.resolve();
                    if (rp.has_value()) {
                        pivot_mesh = rp->primitive->localPivot;
                    }
                }
                const glm::mat4 pivot_tr =
                    glm::translate(glm::mat4(1.0F), pivot_mesh);
                const glm::mat4 world = lumen::scene::world_matrix(reg, sel);
                glm::mat4 gizmo_matrix = world * pivot_tr;
                const auto op =
                    static_cast<ImGuizmo::OPERATION>(scene_gizmo_operation);
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
                    tr->set_transform(glm::inverse(parent_world) * world_new);
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
                const vk::Extent2D pe = sceneTarget.extent();
                const float rw = scene_viewport_rect_for_gizmo.width();
                const float rh = scene_viewport_rect_for_gizmo.height();
                if (vms.inViewport && rw > 0.0F && rh > 0.0F &&
                    pe.width >= 1U && pe.height >= 1U) {
                    // ImGui 矩形：localY 自上而下增大；离屏与 Vulkan 附件 (0,0)
                    // 在 左上角，行 y 向下递增，勿再翻转。
                    const float nx = vms.localX / rw;
                    const float ny = vms.localY / rh;
                    scene_pick_fb_x =
                        (std::min)(pe.width - 1U,
                                   static_cast<std::uint32_t>(
                                       nx * static_cast<float>(pe.width)));
                    scene_pick_fb_y =
                        (std::min)(pe.height - 1U,
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
        commandBuffer.reset({});

        vk::CommandBufferBeginInfo frameBegin {};
        frameBegin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        if (commandBuffer.begin(&frameBegin) != vk::Result::eSuccess) {
            LUMEN_APP_LOG_ERROR("vk::CommandBuffer::begin 失败 frameIndex={}",
                                frameIndex);
            continue;
        }

        std::array<vk::ClearValue, 2> sceneClears {};
        sceneClears[0].color.float32[0] = 0.08F;
        sceneClears[0].color.float32[1] = 0.08F;
        sceneClears[0].color.float32[2] = 0.1F;
        sceneClears[0].color.float32[3] = 1.0F;
        sceneClears[1].depthStencil.depth = 1.0F;
        sceneClears[1].depthStencil.stencil = 0;

        vk::RenderPassBeginInfo sceneRpInfo {};
        sceneRpInfo.renderPass = sceneTarget.render_pass();
        sceneRpInfo.framebuffer = sceneTarget.framebuffer();
        sceneRpInfo.renderArea.offset = vk::Offset2D { 0, 0 };
        sceneRpInfo.renderArea.extent = sceneTarget.extent();
        sceneRpInfo.clearValueCount = static_cast<uint32_t>(sceneClears.size());
        sceneRpInfo.pClearValues = sceneClears.data();

        {
            const vk::Extent2D ext = sceneTarget.extent();
            const float wf = static_cast<float>(ext.width);
            const float hf = static_cast<float>(ext.height);

            const float aspect =
                wf / static_cast<float>((std::max)(1U, ext.height));
            const glm::mat4 view = sceneCamera.view_matrix();
            const glm::mat4 proj = sceneCamera.projection_matrix(aspect);
            const glm::vec3 eye = sceneCamera.eye_position();
            const glm::mat4 skyView = glm::mat4(glm::mat3(view));

            entt::registry &scene_reg = editorScene.registry();
            std::vector<lumen::scene::RenderItem> pbrRenderItems;
            lumen::scene::collect_render_items(scene_reg, pbrRenderItems);
            lumen::scene::sort_render_items_for_minimal_state_change(
                pbrRenderItems);

            for (uint32_t mi = 0; mi < sponzaUniqueMatCount; ++mi) {
                lumen::render::PbrMaterialUbo mu {};
                lumen::render::pack_pbr_material_ubo(
                    mu, *sponzaUniqueMaterials[mi], emissiveScale);
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

            commandBuffer.beginRenderPass(sceneRpInfo,
                                          vk::SubpassContents::eInline);

            const vk::Viewport vp { 0.0F, 0.0F, wf, hf, 0.0F, 1.0F };
            commandBuffer.setViewport(0, { vp });
            const vk::Rect2D scissor { vk::Offset2D { 0, 0 }, ext };
            commandBuffer.setScissor(0, { scissor });

            SkyPush skyPush {};
            skyPush.skyMvp = proj * skyView;
            skyPush.params = glm::vec4(skyExposure, 0.0F, 0.0F, 0.0F);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                       skyPipe.handle());
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             skyPl.handle(), 0, { skyDs }, {});
            commandBuffer.pushConstants(skyPl.handle(),
                                        vk::ShaderStageFlagBits::eVertex |
                                            vk::ShaderStageFlagBits::eFragment,
                                        0, sizeof(SkyPush), &skyPush);
            const vk::DeviceSize skyOff { 0 };
            const vk::Buffer skyVbh = skyVbuf.handle();
            commandBuffer.bindVertexBuffers(0, { skyVbh }, { skyOff });
            commandBuffer.draw(36, 1, 0, 0);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                       helmetPipe.handle());
            lumen::render::PbrForwardRecordContext pbrFwdCtx {};
            pbrFwdCtx.command_buffer = &commandBuffer;
            pbrFwdCtx.pipeline_layout = &helmetPl;
            pbrFwdCtx.frame_descriptor_set = helmetFrameDs[frameIndex];
            pbrFwdCtx.light_descriptor_set = helmetLightDs[frameIndex];
            pbrFwdCtx.object_descriptor_set = helmetObjectDs;
            pbrFwdCtx.default_material = sponzaDefaultMaterial;
            pbrFwdCtx.object_dynamic_stride =
                static_cast<std::uint32_t>(helmetObjStride);
            pbrFwdCtx.bind_vertex_and_index_buffers_per_item = true;
            lumen::render::record_pbr_forward_render_items(
                pbrFwdCtx, pbrRenderItems, helmetObjectUbo,
                [&](const lumen::render::Material *m) {
                    return vk_descriptor_set_for_pbr_material(
                        m, sponzaMaterialToDsIndex, sponzaMaterialDs);
                });

            commandBuffer.endRenderPass();

            if ((scene_pick_pending || show_id_map_viz) &&
                pickIdTarget.is_valid()) {
                std::array<vk::ClearValue, 2> pickClears {};
                pickClears[0].color.uint32[0] = 0U;
                pickClears[0].color.uint32[1] = 0U;
                pickClears[0].color.uint32[2] = 0U;
                pickClears[0].color.uint32[3] = 0U;
                pickClears[1].depthStencil.depth = 1.0F;
                pickClears[1].depthStencil.stencil = 0;
                vk::RenderPassBeginInfo pickRp {};
                pickRp.renderPass = pickIdTarget.render_pass();
                pickRp.framebuffer = pickIdTarget.framebuffer();
                pickRp.renderArea.offset = vk::Offset2D { 0, 0 };
                pickRp.renderArea.extent = pickIdTarget.extent();
                pickRp.clearValueCount =
                    static_cast<uint32_t>(pickClears.size());
                pickRp.pClearValues = pickClears.data();
                commandBuffer.beginRenderPass(pickRp,
                                              vk::SubpassContents::eInline);
                commandBuffer.setViewport(0, { vp });
                commandBuffer.setScissor(0, { scissor });
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                           pickIdPipe.handle());
                lumen::render::PickIdRecordContext pickCtx {};
                pickCtx.command_buffer = &commandBuffer;
                pickCtx.pipeline_layout = &pickIdPl;
                pickCtx.frame_descriptor_set = helmetFrameDs[frameIndex];
                pickCtx.object_descriptor_set = helmetObjectDs;
                pickCtx.object_dynamic_stride =
                    static_cast<std::uint32_t>(helmetObjStride);
                pickCtx.pick_id_push_stages =
                    vk::ShaderStageFlagBits::eFragment;
                pickCtx.pick_id_push_constant_offset = 0;
                pickCtx.bind_vertex_and_index_buffers_per_item = true;
                lumen::render::record_pick_id_render_items(
                    pickCtx, pbrRenderItems, helmetObjectUbo);
                commandBuffer.endRenderPass();

                if (scene_pick_pending) {
                    vk::BufferImageCopy copyRegion {};
                    copyRegion.bufferOffset = 0;
                    copyRegion.bufferRowLength = 0;
                    copyRegion.bufferImageHeight = 0;
                    copyRegion.imageSubresource.aspectMask =
                        vk::ImageAspectFlagBits::eColor;
                    copyRegion.imageSubresource.mipLevel = 0;
                    copyRegion.imageSubresource.baseArrayLayer = 0;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset = vk::Offset3D {
                        static_cast<std::int32_t>(scene_pick_fb_x),
                        static_cast<std::int32_t>(scene_pick_fb_y), 0
                    };
                    copyRegion.imageExtent = vk::Extent3D { 1, 1, 1 };
                    commandBuffer.copyImageToBuffer(
                        static_cast<vk::Image>(pickIdTarget.color_image_vk()),
                        vk::ImageLayout::eTransferSrcOptimal,
                        pickReadbackBuffer.handle(), copyRegion);

                    vk::BufferMemoryBarrier bufBar {};
                    bufBar.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                    bufBar.dstAccessMask = vk::AccessFlagBits::eHostRead;
                    bufBar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bufBar.buffer = pickReadbackBuffer.handle();
                    bufBar.offset = 0;
                    bufBar.size = sizeof(std::uint32_t);
                    const std::array<vk::BufferMemoryBarrier, 1> bufBars {
                        bufBar
                    };
                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eHost,
                        vk::DependencyFlags {}, {}, bufBars, {});
                }

                if (show_id_map_viz) {
                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_transfer_src_to_shader_read(
                            static_cast<VkCommandBuffer>(commandBuffer),
                            pickIdTarget.color_image_vk());

                    std::array<vk::ClearValue, 2> vizClears {};
                    vizClears[0].color.float32[0] = 0.0F;
                    vizClears[0].color.float32[1] = 0.0F;
                    vizClears[0].color.float32[2] = 0.0F;
                    vizClears[0].color.float32[3] = 1.0F;
                    vizClears[1].depthStencil.depth = 1.0F;
                    vizClears[1].depthStencil.stencil = 0;
                    vk::RenderPassBeginInfo vizRp {};
                    vizRp.renderPass = offscreenRenderPass.handle();
                    vizRp.framebuffer = idMapVizTarget.framebuffer();
                    vizRp.renderArea.offset = vk::Offset2D { 0, 0 };
                    vizRp.renderArea.extent = idMapVizTarget.extent();
                    vizRp.clearValueCount =
                        static_cast<uint32_t>(vizClears.size());
                    vizRp.pClearValues = vizClears.data();
                    commandBuffer.beginRenderPass(vizRp,
                                                  vk::SubpassContents::eInline);
                    const vk::Extent2D vizExt = idMapVizTarget.extent();
                    const float vw = static_cast<float>(vizExt.width);
                    const float vh = static_cast<float>(vizExt.height);
                    const vk::Viewport vizVp { 0.0F, 0.0F, vw, vh, 0.0F, 1.0F };
                    commandBuffer.setViewport(0, { vizVp });
                    const vk::Rect2D vizSc { vk::Offset2D { 0, 0 }, vizExt };
                    commandBuffer.setScissor(0, { vizSc });
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                               pickVizPipe.handle());
                    commandBuffer.bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics, pickVizPl.handle(), 0,
                        { idMapVizDs }, {});
                    commandBuffer.draw(3, 1, 0, 0);
                    commandBuffer.endRenderPass();

                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_shader_read_to_undefined(
                            static_cast<VkCommandBuffer>(commandBuffer),
                            pickIdTarget.color_image_vk());
                } else if (scene_pick_pending) {
                    lumen::render::PickIdRenderTarget::
                        record_color_barrier_to_undefined(
                            static_cast<VkCommandBuffer>(commandBuffer),
                            pickIdTarget.color_image_vk());
                }
            }
        }

        if (pbrDebugTileGrid) {
            vk::RenderPassBeginInfo debugRpInfo {};
            debugRpInfo.renderPass = debugTileTarget.render_pass();
            debugRpInfo.framebuffer = debugTileTarget.framebuffer();
            debugRpInfo.renderArea.offset = vk::Offset2D { 0, 0 };
            debugRpInfo.renderArea.extent = debugTileTarget.extent();
            debugRpInfo.clearValueCount =
                static_cast<uint32_t>(sceneClears.size());
            debugRpInfo.pClearValues = sceneClears.data();
            commandBuffer.beginRenderPass(debugRpInfo,
                                          vk::SubpassContents::eInline);
            {
                const vk::Extent2D ext = debugTileTarget.extent();
                const auto wf = static_cast<float>(ext.width);
                const float hf = static_cast<float>(ext.height);
                const glm::mat4 view = sceneCamera.view_matrix();
                const glm::vec3 eye = sceneCamera.eye_position();
                constexpr int DEBUG_TILE_COLS = 4;
                constexpr int DEBUG_TILE_ROWS = 4;
                constexpr int DEBUG_TILE_COUNT =
                    DEBUG_TILE_COLS * DEBUG_TILE_ROWS;
                const float cellWidth =
                    wf / static_cast<float>(DEBUG_TILE_COLS);
                const float cellHeight =
                    hf / static_cast<float>(DEBUG_TILE_ROWS);

                entt::registry &scene_reg_dbg = editorScene.registry();
                std::vector<lumen::scene::RenderItem> pbrRenderItemsDbg;
                lumen::scene::collect_render_items(scene_reg_dbg,
                                                   pbrRenderItemsDbg);
                lumen::scene::sort_render_items_for_minimal_state_change(
                    pbrRenderItemsDbg);

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

                    for (uint32_t mi = 0; mi < sponzaUniqueMatCount; ++mi) {
                        lumen::render::PbrMaterialUbo mu {};
                        lumen::render::pack_pbr_material_ubo(
                            mu, *sponzaUniqueMaterials[mi], emissiveScale);
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

                    vk::Viewport viewportTile {};
                    viewportTile.x = static_cast<float>(col) * cellWidth;
                    // 与「PBR 分屏调试」ImGui 角标自上而下（row 0 在上）一致
                    viewportTile.y = static_cast<float>(row) * cellHeight;
                    viewportTile.width = cellWidth;
                    viewportTile.height = cellHeight;
                    viewportTile.minDepth = 0.0F;
                    viewportTile.maxDepth = 1.0F;
                    commandBuffer.setViewport(0, { viewportTile });
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
                    const vk::Rect2D scissorTile {
                        vk::Offset2D { sx, sy },
                        vk::Extent2D { scissorWidth, scissorHeight }
                    };
                    commandBuffer.setScissor(0, { scissorTile });

                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                               helmetPipe.handle());
                    lumen::render::PbrForwardRecordContext pbrDbgCtx {};
                    pbrDbgCtx.command_buffer = &commandBuffer;
                    pbrDbgCtx.pipeline_layout = &helmetPl;
                    pbrDbgCtx.frame_descriptor_set = helmetFrameDs[frameIndex];
                    pbrDbgCtx.light_descriptor_set = helmetLightDs[frameIndex];
                    pbrDbgCtx.object_descriptor_set = helmetObjectDs;
                    pbrDbgCtx.default_material = sponzaDefaultMaterial;
                    pbrDbgCtx.object_dynamic_stride =
                        static_cast<std::uint32_t>(helmetObjStride);
                    pbrDbgCtx.bind_vertex_and_index_buffers_per_item = true;
                    lumen::render::record_pbr_forward_render_items(
                        pbrDbgCtx, pbrRenderItemsDbg, helmetObjectUbo,
                        [&](const lumen::render::Material *m) {
                            return vk_descriptor_set_for_pbr_material(
                                m, sponzaMaterialToDsIndex, sponzaMaterialDs);
                        });
                }
            }
            commandBuffer.endRenderPass();
        }

        std::array<vk::ClearValue, 1> swapClears {};
        swapClears[0].color.float32[0] = 0.07F;
        swapClears[0].color.float32[1] = 0.08F;
        swapClears[0].color.float32[2] = 0.11F;
        swapClears[0].color.float32[3] = 1.0F;
        vk::RenderPassBeginInfo swapRpInfo {};
        swapRpInfo.renderPass = renderPass.handle();
        swapRpInfo.framebuffer = framebuffers.get(imageIndex);
        swapRpInfo.renderArea.offset = vk::Offset2D { 0, 0 };
        swapRpInfo.renderArea.extent = swapchain.extent();
        swapRpInfo.clearValueCount = static_cast<uint32_t>(swapClears.size());
        swapRpInfo.pClearValues = swapClears.data();
        commandBuffer.beginRenderPass(swapRpInfo, vk::SubpassContents::eInline);

        {
            const vk::Extent2D swapchainExtent = swapchain.extent();
            const vk::Viewport framebufferViewport {
                0.0F,
                0.0F,
                static_cast<float>(swapchainExtent.width),
                static_cast<float>(swapchainExtent.height),
                0.0F,
                1.0F
            };
            commandBuffer.setViewport(0, { framebufferViewport });
            const vk::Rect2D framebufferScissor { vk::Offset2D { 0, 0 },
                                                  swapchainExtent };
            commandBuffer.setScissor(0, { framebufferScissor });
        }

        imguiLayer.end_frame(commandBuffer);

        commandBuffer.endRenderPass();
        commandBuffer.end();

        const vk::Semaphore waitSemaphore =
            frameSync.image_available(frameIndex);
        const vk::Semaphore signalSemaphore =
            frameSync.render_finished(imageIndex);
        const std::array<vk::PipelineStageFlags, 1> waitStages {
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        };

        vk::SubmitInfo sub {};
        sub.waitSemaphoreCount = 1;
        sub.pWaitSemaphores = &waitSemaphore;
        sub.pWaitDstStageMask = waitStages.data();
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &commandBuffer;
        sub.signalSemaphoreCount = 1;
        sub.pSignalSemaphores = &signalSemaphore;

        if (!frameSync.reset_fence(frameIndex)) {
            LUMEN_LOG_ERROR("FrameSync::reset_fence 失败 frameIndex={}",
                            frameIndex);
            continue;
        }
        const vk::Result submitResult = ctx.graphics_queue().submit(
            1, &sub, frameSync.in_flight_fence(frameIndex));
        if (submitResult != vk::Result::eSuccess) {
            LUMEN_APP_LOG_ERROR("Queue::submit 失败 result={}",
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
            const vk::Fence pick_fence = frameSync.in_flight_fence(frameIndex);
            static_cast<void>(
                dev.waitForFences(1, &pick_fence, vk::True, UINT64_MAX));
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
                    LUMEN_APP_LOG_INFO("Scene 拾取: 像素 ({}, {}) —— "
                                       "背景或未命中（R32 编码 0）",
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
                            "{:016x}", static_cast<std::uint64_t>(idc->id));
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
                            smr->mesh != nullptr ? smr->mesh->primitives.size()
                                                 : 0U);
                    } else if (const auto *mr =
                                   pick_reg.try_get<
                                       lumen::scene::MeshRendererComponent>(
                                       picked)) {
                        mesh_note = std::format(
                            " | MeshRenderer meshPrimitiveCount={}",
                            mr->mesh != nullptr ? mr->mesh->primitives.size()
                                                : 0U);
                    } else if (const auto *smir =
                                   pick_reg.try_get<
                                       lumen::scene::
                                           SubMeshInstanceRefRendererComponent>(
                                       picked)) {
                        const std::optional<
                            lumen::asset::SubMeshInstanceRef::ResolvedPrim>
                            rp = smir->submeshRef.resolve();
                        mesh_note =
                            rp.has_value()
                                ? std::format(
                                      " | SubMeshInstanceRef primIndex={} "
                                      "indexCount={}",
                                      smir->submeshRef.primitiveIndex,
                                      rp->primitive->indexCount)
                                : " | "
                                  "SubMeshInstanceRef（资产已卸载，无法解析）";
                    } else if (const auto *mir =
                                   pick_reg.try_get<
                                       lumen::scene::
                                           MeshInstanceRefRendererComponent>(
                                       picked)) {
                        const std::optional<
                            lumen::asset::MeshInstanceRef::Resolved>
                            rr = mir->meshRef.resolve();
                        mesh_note =
                            rr.has_value()
                                ? std::format(" | MeshInstanceRef meshIndex={} "
                                              "primitives={}",
                                              mir->meshRef.meshIndex,
                                              rr->mesh->primitives.size())
                                : " | MeshInstanceRef（资产已卸载）";
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

        const vk::Result pr =
            swapchain.present(ctx.present_queue(), imageIndex, signalSemaphore);
        if (pr == vk::Result::eErrorOutOfDateKHR) {
            LUMEN_APP_LOG_WARN("present OUT_OF_DATE，将重建 Swapchain");
            needRecreateSwapchain = true;
        } else if (pr != vk::Result::eSuccess &&
                   pr != vk::Result::eSuboptimalKHR) {
            LUMEN_APP_LOG_ERROR("present 失败 result={}", static_cast<int>(pr));
        }

        frameIndex = (frameIndex + 1) % 3;
    }

    window.set_relative_mouse_mode(false);
    dev.waitIdle();
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
    lumen::asset::AssetRegistry::instance().clear_all();

    for (vk::ImageView fv : envFaceViews) {
        destroy_image_view(dev, fv);
    }
    for (vk::ImageView fv : irrFaceViews) {
        destroy_image_view(dev, fv);
    }
    for (vk::ImageView fv : preFaceViews) {
        destroy_image_view(dev, fv);
    }

    cmdPool.free(commandBuffers);

    return 0;
}

int main(int argc, char **argv) {
    if (!core::log::Logger::init()) {
        return -1;
    }
    const int result = run_pbr(argc, argv);
    core::log::Logger::shutdown();
    return result;
}
