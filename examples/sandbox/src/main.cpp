/**
 * @file main.cpp
 * @brief PBR — IBL 烘焙、HDR 天空盒、Sponza（glTF 多 primitive / 多材质）
 */

#include "ibl_bake.hpp"

#include "core/gltf_loader.hpp"
#include "core/gltf_material.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/frame_sync.hpp"
#include "render/material/pbr_forward_ubo.hpp"
#include "render/material/pbr_material_bind.hpp"
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
#include "scene/mesh.hpp"
#include "render/material/material.hpp"
#include "scene/scene_camera.hpp"
#include "scene/scene_orbit_controller.hpp"
#include "ui/gpu_capabilities_panel.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"
#include "ui/log_panel.hpp"
#include "ui/panel.hpp"
#include "ui/scene_viewport_panel.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
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
    // "assets/models/Sponza/glTF/Sponza.gltf"
    // "assets/models/rex_master/scene.gltf"
    // "assets/models/chisa_wuthering_waves/scene.gltf"
    // "assets/models/chisa_wuthering_waves/scene.gltf"
    // "assets/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"
    "assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf"
};

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

void center_and_scale_mesh(lumen::core::ObjMesh &mesh) {
    if (mesh.vertices.empty()) {
        return;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (const auto &v : mesh.vertices) {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    const glm::vec3 center { 0.5F * (bmin + bmax) };
    for (auto &v : mesh.vertices) {
        v.position -= center;
    }
    const glm::vec3 ext { bmax - bmin };
    const float mx = (std::max)(ext.x, (std::max)(ext.y, ext.z));
    const float s = mx > 1e-8F ? (1.8F / mx) : 1.0F;
    for (auto &v : mesh.vertices) {
        v.position *= s;
    }
}

void compute_mesh_tangents(std::vector<HelmVertex> &verts,
                           const std::vector<uint32_t> &indices) {
    std::vector<glm::vec3> tan1(verts.size(), glm::vec3(0.0F));
    std::vector<glm::vec3> tan2(verts.size(), glm::vec3(0.0F));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];
        const HelmVertex &v0 = verts[i0];
        const HelmVertex &v1 = verts[i1];
        const HelmVertex &v2 = verts[i2];
        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 duv1 = v1.uv - v0.uv;
        const glm::vec2 duv2 = v2.uv - v0.uv;
        const float denom = duv1.x * duv2.y - duv2.x * duv1.y + 1e-8F;
        const float f = 1.0F / denom;
        const glm::vec3 t = f * (edge1 * duv2.y - edge2 * duv1.y);
        const glm::vec3 b = f * (edge2 * duv1.x - edge1 * duv2.x);
        tan1[i0] += t;
        tan1[i1] += t;
        tan1[i2] += t;
        tan2[i0] += b;
        tan2[i1] += b;
        tan2[i2] += b;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const glm::vec3 &n = verts[i].normal;
        glm::vec3 t = tan1[i];
        t = glm::normalize(t - n * glm::dot(n, t));
        const float w =
            glm::dot(glm::cross(n, t), glm::normalize(tan2[i])) < 0.0F ? -1.0F
                                                                       : 1.0F;
        verts[i].tangent = glm::vec4(t, w);
    }
}

[[nodiscard]] VkDescriptorSet vk_descriptor_for_pbr_material(
    const lumen::render::Material *material,
    const std::vector<lumen::render::Material> &materials_storage,
    const std::vector<VkDescriptorSet> &material_sets) {
    if (material_sets.empty()) {
        return VK_NULL_HANDLE;
    }
    if (material == nullptr || materials_storage.empty()) {
        return material_sets[0];
    }
    const ptrdiff_t off = material - materials_storage.data();
    if (off < 0 || static_cast<size_t>(off) >= materials_storage.size()) {
        return material_sets[0];
    }
    return material_sets[static_cast<size_t>(off)];
}

/** @brief 立方体贴图某一面的 2D 视图（@a base_array_layer 0…5 对应 +X −X +Y −Y
 * +Z −Z） */
[[nodiscard]] VkImageView create_cubemap_face_2d_view(VkDevice dev, VkImage img,
                                                      VkFormat fmt,
                                                      uint32_t mip_level,
                                                      uint32_t base_array_layer,
                                                      const char *label) {
    VkImageViewCreateInfo vi { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = mip_level;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.baseArrayLayer = base_array_layer;
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

void imgui_draw_cubemap_face_grid(const std::array<ImTextureID, 6> &faces,
                                  const ImVec2 &face_sz) {
    static constexpr const char *k_face_nm[6] = { "+X", "-X", "+Y",
                                                  "-Y", "+Z", "-Z" };
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int fi = row * 3 + col;
            ImGui::BeginGroup();
            ImGui::TextUnformatted(k_face_nm[fi]);
            ImGui::Image(faces[static_cast<size_t>(fi)], face_sz);
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
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Lumen — PBR DamagedHelmet + IBL";
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

    lumen::render::OffscreenRenderTarget sceneTarget;
    {
        lumen::render::OffscreenRenderTargetConfig scene_cfg;
        scene_cfg.width =
            static_cast<uint32_t>((std::max)(2, window_width * 3 / 4));
        scene_cfg.height =
            static_cast<uint32_t>((std::max)(2, window_height * 3 / 4));
        scene_cfg.format = swapchain.image_format();
        scene_cfg.useDepth = true;
        scene_cfg.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!sceneTarget.create(ctx, scene_cfg, &offscreen_render_pass)) {
            LUMEN_APP_LOG_ERROR("场景离屏渲染目标创建失败");
            return -1;
        }
    }

    lumen::render::OffscreenRenderTarget debug_tile_target;
    {
        lumen::render::OffscreenRenderTargetConfig dbg_cfg;
        dbg_cfg.width =
            static_cast<uint32_t>((std::max)(2, window_width * 3 / 4));
        dbg_cfg.height =
            static_cast<uint32_t>((std::max)(2, window_height * 3 / 4));
        dbg_cfg.format = swapchain.image_format();
        dbg_cfg.useDepth = true;
        dbg_cfg.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (!debug_tile_target.create(ctx, dbg_cfg, &offscreen_render_pass)) {
            LUMEN_APP_LOG_ERROR("分屏调试用离屏目标创建失败");
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

    std::array<VkImageView, 6> v_env_faces {};
    std::array<VkImageView, 6> v_irr_faces {};
    std::array<VkImageView, 6> v_pre_faces {};
    for (uint32_t face = 0; face < 6; ++face) {
        char env_label[48];
        std::snprintf(env_label, sizeof env_label, "IBL environment face %u",
                      face);
        v_env_faces[face] = create_cubemap_face_2d_view(
            dev, ibl.environment.image(), ibl_fmt, 0, face, env_label);
        char irr_label[48];
        std::snprintf(irr_label, sizeof irr_label, "IBL irradiance face %u",
                      face);
        v_irr_faces[face] = create_cubemap_face_2d_view(
            dev, ibl.irradiance.image(), ibl_fmt, 0, face, irr_label);
        char pre_label[48];
        std::snprintf(pre_label, sizeof pre_label, "IBL prefilter face %u",
                      face);
        v_pre_faces[face] = create_cubemap_face_2d_view(
            dev, ibl.prefilter.image(), ibl_fmt, 0, face, pre_label);
    }
    VkImageView v_brdf = ibl.brdf_lut.view();

    auto all_six_valid = [](const std::array<VkImageView, 6> &a) -> bool {
        for (VkImageView v : a) {
            if (v == VK_NULL_HANDLE) {
                return false;
            }
        }
        return true;
    };
    const bool env_views_ok = all_six_valid(v_env_faces);
    const bool irr_views_ok = all_six_valid(v_irr_faces);
    const bool pre_views_ok = all_six_valid(v_pre_faces);
    if (!env_views_ok || !irr_views_ok || !pre_views_ok ||
        v_brdf == VK_NULL_HANDLE) {
        LUMEN_APP_LOG_ERROR(
            "ImGui 预览用 ImageView 无效: env6={} irr6={} pre6={} brdf={}",
            env_views_ok, irr_views_ok, pre_views_ok, v_brdf != VK_NULL_HANDLE);
        for (VkImageView fv : v_env_faces) {
            destroy_view(dev, fv);
        }
        for (VkImageView fv : v_irr_faces) {
            destroy_view(dev, fv);
        }
        for (VkImageView fv : v_pre_faces) {
            destroy_view(dev, fv);
        }
        return -1;
    }

    const std::string sponza_path =
        lumen::core::get_resource_path(kSponzaGltfRel);
    if (!std::filesystem::exists(sponza_path)) {
        LUMEN_APP_LOG_ERROR("未找到 Sponza glTF: {}（含同级纹理）",
                            sponza_path);
        return -1;
    }

    lumen::core::ObjMesh sponza_obj {};
    std::vector<lumen::core::GltfSubmeshRange> sponza_submeshes;
    std::vector<lumen::core::GltfMaterialData> sponza_gltf_materials;
    lumen::core::GltfMaterialData sponza_fallback_mat {};
    if (!lumen::core::load_gltf(sponza_path, sponza_obj, sponza_fallback_mat,
                                &sponza_submeshes, &sponza_gltf_materials)) {
        LUMEN_APP_LOG_ERROR("glTF 加载失败: {}", sponza_path);
        return -1;
    }
    if (sponza_obj.vertices.empty() || sponza_obj.indices.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 网格为空");
        return -1;
    }
    if (sponza_submeshes.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 无 primitive 分段（需 submesh）");
        return -1;
    }

    center_and_scale_mesh(sponza_obj);

    std::vector<HelmVertex> sponza_verts;
    sponza_verts.reserve(sponza_obj.vertices.size());
    for (const auto &ov : sponza_obj.vertices) {
        sponza_verts.push_back(
            HelmVertex { .position = ov.position,
                         .normal = ov.normal,
                         .uv = ov.uv,
                         .tangent = { 1.0F, 0.0F, 0.0F, 1.0F } });
    }
    compute_mesh_tangents(sponza_verts, sponza_obj.indices);

    VkQueue gq = ctx.graphics_queue();

    lumen::render::PbrPlaceholderTextures pbr_placeholders;
    if (!pbr_placeholders.create(ctx, gq, cmdPool) ||
        !pbr_placeholders.is_complete()) {
        LUMEN_APP_LOG_ERROR("PBR 占位贴图创建失败");
        return -1;
    }

    std::unordered_map<std::string, size_t> tex_key_to_index;
    /// unique_ptr：vector 扩容时堆上 Texture 地址不变；vector<Texture> 会令
    /// Material 内指针全部悬垂并污染 descriptor 写入。
    std::vector<std::unique_ptr<lumen::render::Texture>> sponza_texture_storage;

    auto acquire_texture = [&](const std::string &rel_path,
                               VkFormat fmt) -> const lumen::render::Texture * {
        if (rel_path.empty()) {
            return nullptr;
        }
        const std::string key =
            rel_path + '#' + std::to_string(static_cast<uint32_t>(fmt));
        const auto found = tex_key_to_index.find(key);
        if (found != tex_key_to_index.end()) {
            return sponza_texture_storage[found->second].get();
        }
        const std::string full = lumen::core::get_resource_path(rel_path);
        if (!std::filesystem::exists(full)) {
            LUMEN_APP_LOG_WARN("贴图不存在，使用占位: {}", full);
            return nullptr;
        }
        auto tex = std::make_unique<lumen::render::Texture>();
        if (!tex->create_from_file(ctx, full.c_str(), gq, cmdPool, {}, fmt)) {
            LUMEN_APP_LOG_WARN("贴图上传失败: {}", full);
            return nullptr;
        }
        const size_t new_ix = sponza_texture_storage.size();
        sponza_texture_storage.push_back(std::move(tex));
        tex_key_to_index.emplace(key, new_ix);
        return sponza_texture_storage[new_ix].get();
    };

    std::vector<lumen::render::Material> pbr_materials(
        sponza_gltf_materials.size());
    for (size_t i = 0; i < sponza_gltf_materials.size(); ++i) {
        const lumen::core::GltfMaterialData &src = sponza_gltf_materials[i];
        lumen::render::Material &dst = pbr_materials[i];
        dst.baseColorFactor = src.base_color_factor;
        dst.metallicFactor = src.metallic_factor;
        dst.roughnessFactor = src.roughness_factor;
        dst.emissiveFactor = src.emissive_factor;
        dst.occlusionStrength = src.ao_factor;
        dst.doubleSided = src.double_sided;
        dst.alphaMode = static_cast<lumen::render::MaterialAlphaMode>(
            static_cast<
                std::underlying_type_t<lumen::core::GltfMaterialAlphaMode>>(
                src.alpha_mode));
        dst.baseColorTex =
            acquire_texture(src.albedo_path, VK_FORMAT_R8G8B8A8_SRGB);
        dst.normalTex =
            acquire_texture(src.normal_path, VK_FORMAT_R8G8B8A8_UNORM);
        dst.metallicRoughnessTex = acquire_texture(
            src.metallic_roughness_path, VK_FORMAT_R8G8B8A8_UNORM);
        dst.occlusionTex =
            acquire_texture(src.ao_path, VK_FORMAT_R8G8B8A8_UNORM);
        dst.emissiveTex =
            acquire_texture(src.emissive_path, VK_FORMAT_R8G8B8A8_SRGB);
    }
    if (pbr_materials.empty()) {
        LUMEN_APP_LOG_ERROR("Sponza 材质表为空");
        return -1;
    }

    lumen::render::VertexBuffer sponza_vbuf;
    if (!sponza_vbuf.create_device_local_and_upload(
            ctx, gq, cmdPool, sponza_verts.data(),
            sponza_verts.size() * sizeof(HelmVertex))) {
        LUMEN_APP_LOG_ERROR("Sponza 顶点缓冲失败");
        return -1;
    }
    lumen::render::IndexBuffer sponza_ibuf;
    sponza_ibuf.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!sponza_ibuf.create_device_local_and_upload(
            ctx, gq, cmdPool, sponza_obj.indices.data(),
            sponza_obj.indices.size() * sizeof(uint32_t))) {
        LUMEN_APP_LOG_ERROR("Sponza 索引缓冲失败");
        return -1;
    }

    lumen::scene::Mesh sponza_mesh {};
    sponza_mesh.primitives.reserve(sponza_submeshes.size());
    for (const lumen::core::GltfSubmeshRange &r : sponza_submeshes) {
        int mi = r.materialIndex;
        if (mi < 0 || mi >= static_cast<int>(pbr_materials.size())) {
            mi = 0;
        }
        sponza_mesh.primitives.push_back(lumen::scene::Primitive {
            .vertex_buffer = &sponza_vbuf,
            .index_buffer = &sponza_ibuf,
            .vertex_byte_offset = 0,
            .first_index = r.firstIndex,
            .index_count = r.indexCount,
            .material = &pbr_materials[static_cast<size_t>(mi)],
        });
    }

    uint32_t sponza_draw_slots = 0;
    for (const lumen::scene::Primitive &sp : sponza_mesh.primitives) {
        if (sp.is_drawable()) {
            ++sponza_draw_slots;
        }
    }
    const std::size_t helmet_obj_stride =
        lumen::render::pbr_object_ubo_dynamic_stride(static_cast<std::size_t>(
            ctx.physical_device_properties()
                .limits.minUniformBufferOffsetAlignment));
    const VkDeviceSize helmet_object_ubo_bytes =
        static_cast<VkDeviceSize>(helmet_obj_stride) *
        static_cast<VkDeviceSize>((std::max)(sponza_draw_slots, 1u));

    LUMEN_APP_LOG_INFO(
        "Sponza: 顶点={} 索引={} 三角≈{} primitive={} 材质={} GPU 贴图实例={}",
        sponza_obj.vertices.size(), sponza_obj.indices.size(),
        sponza_obj.indices.size() / 3U, sponza_submeshes.size(),
        pbr_materials.size(), sponza_texture_storage.size());

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

    auto sceneTexID =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            scene_sampler.handle(), sceneTarget.color_view(),
            sceneTarget.color_sample_layout()));
    auto debug_scene_tex_id =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            scene_sampler.handle(), debug_tile_target.color_view(),
            debug_tile_target.color_sample_layout()));

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

    const std::string helmet_vs_path = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_VERT_SPV_RELATIVE));
    const std::string helmet_fs_path = lumen::core::get_resource_path(
        std::string(lumen::render::PBR_FORWARD_FRAG_SPV_RELATIVE));
    lumen::render::ShaderModule sm_helmet_vs;
    lumen::render::ShaderModule sm_helmet_fs;
    if (!sm_helmet_vs.create_from_file(dev, helmet_vs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("头盔顶点着色器加载失败: {}", helmet_vs_path);
        return -1;
    }
    if (!sm_helmet_fs.create_from_file(dev, helmet_fs_path.c_str())) {
        LUMEN_APP_LOG_ERROR("头盔片元着色器加载失败: {}", helmet_fs_path);
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

    const uint32_t sponza_mat_count =
        static_cast<uint32_t>(pbr_materials.size());
    const uint32_t pbr_ubo_static = 3u + sponza_mat_count + 3u;
    const uint32_t pbr_set_count = 3u + sponza_mat_count + 1u + 3u;
    const uint32_t pbr_combined_for_materials = sponza_mat_count * 5u;
    const uint32_t pbr_combined_total = 9u + pbr_combined_for_materials;

    lumen::render::DescriptorPool pbr_dpool;
    if (!pbr_dpool.create(
            ctx,
            { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .count = pbr_ubo_static },
              { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .count = 1 },
              { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .count = pbr_combined_total } },
            pbr_set_count)) {
        LUMEN_APP_LOG_ERROR("PBR DescriptorPool 失败 (材质数={})",
                            sponza_mat_count);
        return -1;
    }

    std::vector<lumen::render::UniformBuffer> sponza_material_ubos(
        sponza_mat_count);
    for (uint32_t mi = 0; mi < sponza_mat_count; ++mi) {
        if (!sponza_material_ubos[mi].create_persistent(
                ctx, sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 失败 mi={}", mi);
            return -1;
        }
    }

    std::vector<VkDescriptorSet> sponza_material_ds(sponza_mat_count);
    for (uint32_t mi = 0; mi < sponza_mat_count; ++mi) {
        if (!pbr_dpool.allocate(dev, helmet_material_dsl.handle(),
                                sponza_material_ds[mi])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败 mi={}", mi);
            return -1;
        }
        lumen::render::PbrMaterialUbo mu {};
        lumen::render::pack_pbr_material_ubo(mu, pbr_materials[mi], 3.0F);
        sponza_material_ubos[mi].update(mu);
        lumen::render::write_pbr_material_descriptor_set(
            dev, sponza_material_ds[mi], sponza_material_ubos[mi].handle(),
            sizeof(lumen::render::PbrMaterialUbo), pbr_materials[mi],
            pbr_placeholders);
    }

    std::array<VkDescriptorSet, 3> helmet_frame_ds {};
    for (uint32_t i = 0; i < helmet_frame_ds.size(); ++i) {
        if (!pbr_dpool.allocate(dev, helmet_frame_dsl.handle(),
                                helmet_frame_ds[i])) {
            LUMEN_APP_LOG_ERROR("PBR Frame DescriptorSet 分配失败");
            return -1;
        }
    }

    std::array<lumen::render::UniformBuffer, 3> helmet_frame_ubos {};
    for (uint32_t i = 0; i < helmet_frame_ubos.size(); ++i) {
        if (!helmet_frame_ubos[i].create_persistent(
                ctx, sizeof(lumen::render::PbrFrameUbo))) {
            LUMEN_APP_LOG_ERROR("PBR Frame UniformBuffer 失败");
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
    if (!pbr_dpool.allocate(dev, helmet_object_dsl.handle(),
                            helmet_object_ds)) {
        LUMEN_APP_LOG_ERROR("PBR Object DescriptorSet 分配失败");
        return -1;
    }
    lumen::render::UniformBuffer helmet_object_ubo;
    if (!helmet_object_ubo.create_persistent(
            ctx, static_cast<size_t>(helmet_object_ubo_bytes))) {
        LUMEN_APP_LOG_ERROR("PBR Object UniformBuffer 失败");
        return -1;
    }
    lumen::render::write_pbr_object_descriptor_set_dynamic(
        dev, helmet_object_ds, helmet_object_ubo.handle(),
        sizeof(lumen::render::PbrObjectUbo));

    std::array<VkDescriptorSet, 3> helmet_light_ds {};
    std::array<lumen::render::UniformBuffer, 3> helmet_light_ubos {};
    for (uint32_t i = 0; i < helmet_light_ds.size(); ++i) {
        if (!pbr_dpool.allocate(dev, helmet_light_dsl.handle(),
                                helmet_light_ds[i])) {
            LUMEN_APP_LOG_ERROR("PBR Light DescriptorSet 分配失败");
            return -1;
        }
        if (!helmet_light_ubos[i].create_persistent(
                ctx, sizeof(lumen::render::PbrLightUbo))) {
            LUMEN_APP_LOG_ERROR("PBR Light UniformBuffer 失败");
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

    LUMEN_APP_LOG_INFO("PBR 场景资源就绪，着色器 {} | {}", helmet_vs_path,
                       helmet_fs_path);

    float skyExposure { 4.0F };
    float iblStrength { 3.0F };
    float emissiveScale { 3.0F };
    int pointlightCount { lumen::render::PBR_LEGACY_POINT_LIGHT_CAP };
    float pointDirectStrength { 1.15F };
    int helmetDebugView { 0 };
    bool pbrDebugTileGrid { false };

    lumen::scene::SceneCamera scene_camera;
    lumen::scene::SceneOrbitController orbit;
    scene_camera.set_projection_perspective(55.0F, 0.05F, 120.0F);
    orbit.set_pivot(glm::vec3(0.0F));
    orbit.set_world_up(glm::vec3(0.0F, 1.0F, 0.0F));
    orbit.set_yaw(0.0F);
    orbit.set_pitch(0.0F);
    orbit.set_radius(9.0F);
    {
        lumen::scene::SceneOrbitController::Limits lim {};
        lim.min_radius = 0.45F;
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

    std::array<ImTextureID, 6> tex_env_faces {};
    std::array<ImTextureID, 6> tex_irr_faces {};
    std::array<ImTextureID, 6> tex_pre_faces {};
    for (uint32_t face = 0; face < 6; ++face) {
        tex_env_faces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), v_env_faces[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        tex_irr_faces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), v_irr_faces[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        tex_pre_faces[face] =
            reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
                uiSampler.handle(), v_pre_faces[face],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    }
    auto tex_brdf =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), v_brdf,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    lumen::platform::EventPump pump;
    uint32_t frameIdx { 0 };
    bool running { true };
    bool need_recreate_swapchain { false };

    lumen::ui::ImGuiLayer imgui_layer;
    imgui_layer.attach(pump);

    lumen::ui::PanelManager dock_panels;
    dock_panels.add(std::make_unique<lumen::ui::LogPanel>());
    dock_panels.add(std::make_unique<lumen::ui::GpuCapabilitiesPanel>(ctx));

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
                need_recreate_swapchain = true;
                return false;
            });
    });

    constexpr uint64_t kAcquireTimeoutNs { 100'000'000 };
    /// 略长于常见 16ms 帧，减轻慢帧时轮询等待的无意义超时次数
    constexpr uint64_t kFenceWaitNs { 50'000'000 };
    bool acquire_fail_logged { false };
    auto prev_frame_time = std::chrono::steady_clock::now();

    while (running) {
        if (!pump.poll()) {
            LUMEN_APP_LOG_INFO("事件泵结束，退出主循环");
            break;
        }

        const auto frame_now = std::chrono::steady_clock::now();
        const float delta_seconds =
            std::chrono::duration<float>(frame_now - prev_frame_time).count();
        prev_frame_time = frame_now;

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

        if (pendingSceneWidth >= 2U && pendingSceneHeight >= 2U &&
            (pendingSceneWidth != sceneTarget.width() ||
             pendingSceneHeight != sceneTarget.height())) {
            ctx.wait_idle();
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(sceneTexID));
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(debug_scene_tex_id));
            sceneTexID = static_cast<ImTextureID>(0);
            debug_scene_tex_id = static_cast<ImTextureID>(0);
            if (!sceneTarget.resize(pendingSceneWidth, pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("场景离屏目标 resize 失败");
                running = false;
                break;
            }
            if (!debug_tile_target.resize(pendingSceneWidth,
                                          pendingSceneHeight)) {
                LUMEN_APP_LOG_ERROR("分屏调试离屏目标 resize 失败");
                running = false;
                break;
            }
            sceneTexID = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    scene_sampler.handle(), sceneTarget.color_view(),
                    sceneTarget.color_sample_layout()));
            debug_scene_tex_id = reinterpret_cast<ImTextureID>(
                lumen::ui::imgui_backend_add_texture(
                    scene_sampler.handle(), debug_tile_target.color_view(),
                    debug_tile_target.color_sample_layout()));
        }

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

        bool scene_view_hovered { false };
        bool debug_scene_view_hovered { false };

        imgui_layer.begin_frame();

        constexpr float k_scene_orbit_wheel_scale { 0.14F };
        const auto on_orbit_viewport_scroll = [&](float wheel) {
            orbit.apply_scroll_zoom(wheel, k_scene_orbit_wheel_scale);
        };

        lumen::ui::imgui_scene_viewport_panel(
            "Scene", sceneTexID, &pendingSceneWidth, &pendingSceneHeight,
            &scene_view_hovered, on_orbit_viewport_scroll);

        if (pbrDebugTileGrid &&
            debug_scene_tex_id != static_cast<ImTextureID>(0)) {
            auto draw_tile_labels =
                [&](const lumen::ui::TextureViewRect &rect) {
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
                    const ImVec2 p0(rect.minX, rect.minY);
                    const ImVec2 p1(rect.maxX, rect.maxY);
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
                };
            lumen::ui::imgui_scene_viewport_panel(
                "PBR 分屏调试 (4×4)", debug_scene_tex_id, nullptr, nullptr,
                &debug_scene_view_hovered, on_orbit_viewport_scroll,
                draw_tile_labels);
        }

        if (ImGui::Begin("环境光贴图")) {
            ImGui::TextWrapped("HDR 源：%s", hdr_path.c_str());
            ImGui::TextUnformatted("烘焙纹理均为 RGBA32F 线性；立方体面序与 "
                                   "Vulkan 一致（+X −X +Y −Y +Z "
                                   "−Z）。");
            char ibl_info[384];
            std::snprintf(
                ibl_info, sizeof ibl_info,
                "Environment %u×%u · mip %u  |  Irradiance %u×%u · mip %u  |  "
                "Prefilter %u×%u · mip %u  |  BRDF LUT %u×%u",
                ibl.environment.width(), ibl.environment.height(),
                ibl.environment.mip_levels(), ibl.irradiance.width(),
                ibl.irradiance.height(), ibl.irradiance.mip_levels(),
                ibl.prefilter.width(), ibl.prefilter.height(),
                ibl.prefilter.mip_levels(), ibl.brdf_lut.width(),
                ibl.brdf_lut.height());
            ImGui::TextWrapped("%s", ibl_info);
            ImGui::Separator();
            ImGui::SliderFloat("天空曝光", &skyExposure, 0.05F, 4.0F, "%.2f");
            ImGui::SliderFloat("IBL 强度", &iblStrength, 0.0F, 3.0F, "%.2f");
            ImGui::Separator();
            ImGui::TextUnformatted("Environment 立方体（mip 0）");
            imgui_draw_cubemap_face_grid(tex_env_faces, ImVec2(140.0F, 140.0F));
            ImGui::TextUnformatted("Irradiance 立方体（mip 0）");
            imgui_draw_cubemap_face_grid(tex_irr_faces, ImVec2(110.0F, 110.0F));
            ImGui::TextUnformatted(
                "Prefilter 立方体（mip 0；着色器按粗糙度采样完整 mip 链）");
            imgui_draw_cubemap_face_grid(tex_pre_faces, ImVec2(110.0F, 110.0F));
            ImGui::TextUnformatted("BRDF 积分 LUT（RG，线性）");
            ImGui::Image(tex_brdf, ImVec2(220.0F, 220.0F));
        }
        ImGui::End();

        if (ImGui::Begin("IBL 预览")) {
            ImGui::TextUnformatted(
                "右键拖拽旋转（「Scene」或「PBR 分屏调试」画面内、非 Alt）；"
                "Alt+左/中键 轨道/平移/缩放；WASD+EQ 平移；在上述画面上滚轮缩放"
                "（绕枢轴）");
            ImGui::TextUnformatted("天空曝光与 IBL 强度见「环境光贴图」。");
            float orbit_r = orbit.radius();
            ImGui::SliderFloat("轨道距离（缩放）", &orbit_r, 0.45F, 28.0F,
                               "%.2f");
            orbit.set_radius(orbit_r);
            ImGui::SliderFloat("自发光倍率", &emissiveScale, 0.0F, 12.0F,
                               "%.1f");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "点光源（GGX 直射；分屏首格与「Scene」完整 PBR 一致）");
            ImGui::SliderInt("点光数量", &pointlightCount, 0,
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
            char dbg_line[64];
            std::snprintf(dbg_line, sizeof dbg_line, "当前模式号: %d",
                          helmetDebugView);
            ImGui::TextUnformatted(dbg_line);
            ImGui::EndDisabled();
        }
        ImGui::End();

        if (ImGui::Begin("Sponza 场景网格")) {
            ImGui::Text("Primitive 数: %zu", sponza_mesh.primitives.size());
            ImGui::Text("材质槽位数: %zu", pbr_materials.size());
            ImGui::Text("已加载 GPU 纹理数: %zu",
                        sponza_texture_storage.size());
            ImGui::TextWrapped("多 primitive 按 `scene::Mesh` 绘制；每材质一套 "
                               "descriptor set=1。");
        }
        ImGui::End();

        if (lumen::ui::imgui_backend_docking_enabled()) {
            dock_panels.set_default_dock_id(
                lumen::ui::imgui_backend_main_dockspace_id());
        }
        dock_panels.render_all();

        const bool scene_tex_viewport_hovered =
            scene_view_hovered || debug_scene_view_hovered;
        const bool imgui_blocks_scene_mouse =
            lumen::ui::imgui_wants_mouse() && !scene_tex_viewport_hovered;
        const bool want_rel_mouse = orbit.apply_per_frame_editor_navigation(
            pump.input(), scene_tex_viewport_hovered, imgui_blocks_scene_mouse,
            delta_seconds);
        window.set_relative_mouse_mode(want_rel_mouse);
        orbit.apply_to(scene_camera);

        auto &cmd_buf = commandBuffers[frameIdx];
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
        scene_rp.renderPass = sceneTarget.render_pass();
        scene_rp.framebuffer = sceneTarget.framebuffer();
        scene_rp.renderArea.offset = { 0, 0 };
        scene_rp.renderArea.extent = sceneTarget.extent();
        scene_rp.clearValueCount = static_cast<uint32_t>(scene_clears.size());
        scene_rp.pClearValues = scene_clears.data();

        vkCmdBeginRenderPass(cmd_buf.handle(), &scene_rp,
                             VK_SUBPASS_CONTENTS_INLINE);

        {
            const VkExtent2D ext = sceneTarget.extent();
            const float wf = static_cast<float>(ext.width);
            const float hf = static_cast<float>(ext.height);

            const float aspect =
                wf / static_cast<float>((std::max)(1U, ext.height));
            const glm::mat4 view = scene_camera.view_matrix();
            const glm::mat4 proj = scene_camera.projection_matrix(aspect);
            const glm::vec3 eye = scene_camera.eye_position();
            const glm::mat4 sky_v = glm::mat4(glm::mat3(view));

            VkCommandBuffer cb = cmd_buf.handle();
            const glm::mat4 helmet_model { 1.0F };

            for (uint32_t mi = 0; mi < sponza_mat_count; ++mi) {
                lumen::render::PbrMaterialUbo mu {};
                lumen::render::pack_pbr_material_ubo(mu, pbr_materials[mi],
                                                     emissiveScale);
                sponza_material_ubos[mi].update(mu);
            }

            lumen::render::PbrFrameUbo frame_u {};
            lumen::render::pack_pbr_frame_ubo(
                frame_u, view, proj, eye, skyExposure, iblStrength,
                k_prefilter_max_lod, 0.0F,
                pbrDebugTileGrid ? lumen::render::PBR_DEBUG_NONE
                                 : helmetDebugView);
            helmet_frame_ubos[frameIdx].update(frame_u);

            lumen::render::PbrLightUbo light_u {};
            lumen::render::fill_pbr_light_ubo_default_points(
                light_u, pointlightCount, pointDirectStrength);
            helmet_light_ubos[frameIdx].update(light_u);

            VkViewport vp { 0.0F, 0.0F, wf, hf, 0.0F, 1.0F };
            vkCmdSetViewport(cb, 0, 1, &vp);
            VkRect2D scissor { { 0, 0 }, ext };
            vkCmdSetScissor(cb, 0, 1, &scissor);

            SkyPush sky_push {};
            sky_push.sky_mvp = proj * sky_v;
            sky_push.params = glm::vec4(skyExposure, 0.0F, 0.0F, 0.0F);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              sky_pipe.handle());
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    sky_pl.handle(), 0, 1, &sky_ds, 0, nullptr);
            vkCmdPushConstants(
                cb, sky_pl.handle(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                static_cast<uint32_t>(sizeof(SkyPush)), &sky_push);
            VkDeviceSize sky_off { 0 };
            VkBuffer sky_vbh = sky_vbuf.handle();
            vkCmdBindVertexBuffers(cb, 0, 1, &sky_vbh, &sky_off);
            vkCmdDraw(cb, 36, 1, 0, 0);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              helmet_pipe.handle());
            uint32_t draw_slot = 0;
            for (const lumen::scene::Primitive &prim : sponza_mesh.primitives) {
                if (!prim.is_drawable()) {
                    continue;
                }
                const lumen::render::Material *prim_mat =
                    prim.material != nullptr ? prim.material
                                             : &pbr_materials[0];
                VkDescriptorSet mat_ds = vk_descriptor_for_pbr_material(
                    prim_mat, pbr_materials, sponza_material_ds);
                lumen::render::PbrObjectUbo ou {};
                ou.model = helmet_model;
                const glm::mat3 n3 = glm::mat3(helmet_model);
                ou.normalMatrix = glm::mat4(glm::transpose(glm::inverse(n3)));
                helmet_object_ubo.update(ou, draw_slot * helmet_obj_stride);
                std::array<VkDescriptorSet, 4> pbr_sets {
                    helmet_frame_ds[frameIdx], mat_ds, helmet_object_ds,
                    helmet_light_ds[frameIdx]
                };
                const uint32_t dyn_off =
                    static_cast<uint32_t>(draw_slot * helmet_obj_stride);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        helmet_pl.handle(), 0,
                                        static_cast<uint32_t>(pbr_sets.size()),
                                        pbr_sets.data(), 1, &dyn_off);
                ++draw_slot;
                VkDeviceSize voff =
                    static_cast<VkDeviceSize>(prim.vertex_byte_offset);
                VkBuffer vb = prim.vertex_buffer->handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &voff);
                vkCmdBindIndexBuffer(cb, prim.index_buffer->handle(), 0,
                                     prim.index_buffer->vk_index_type());
                vkCmdDrawIndexed(cb, prim.index_count, 1, prim.first_index, 0,
                                 0);
            }
        }

        vkCmdEndRenderPass(cmd_buf.handle());

        if (pbrDebugTileGrid) {
            VkRenderPassBeginInfo dbg_rp {
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            };
            dbg_rp.renderPass = debug_tile_target.render_pass();
            dbg_rp.framebuffer = debug_tile_target.framebuffer();
            dbg_rp.renderArea.offset = { 0, 0 };
            dbg_rp.renderArea.extent = debug_tile_target.extent();
            dbg_rp.clearValueCount = static_cast<uint32_t>(scene_clears.size());
            dbg_rp.pClearValues = scene_clears.data();
            vkCmdBeginRenderPass(cmd_buf.handle(), &dbg_rp,
                                 VK_SUBPASS_CONTENTS_INLINE);
            {
                const VkExtent2D ext = debug_tile_target.extent();
                const float wf = static_cast<float>(ext.width);
                const float hf = static_cast<float>(ext.height);
                const glm::mat4 view = scene_camera.view_matrix();
                const glm::vec3 eye = scene_camera.eye_position();
                VkCommandBuffer cb = cmd_buf.handle();
                const glm::mat4 helmet_model { 1.0F };
                constexpr int k_dbg_tile_cols = 4;
                constexpr int k_dbg_tile_rows = 4;
                constexpr int k_dbg_tile_count =
                    k_dbg_tile_cols * k_dbg_tile_rows;
                const float cw = wf / static_cast<float>(k_dbg_tile_cols);
                const float ch = hf / static_cast<float>(k_dbg_tile_rows);

                for (int ti = 0; ti < k_dbg_tile_count; ++ti) {
                    const int col = ti % k_dbg_tile_cols;
                    const int row = ti / k_dbg_tile_cols;
                    const float aspect =
                        cw / static_cast<float>((std::max)(1.0F, ch));
                    glm::mat4 proj = glm::perspective(glm::radians(55.0F),
                                                      aspect, 0.05F, 120.0F);
                    proj[1][1] *= -1.0F;

                    for (uint32_t mi = 0; mi < sponza_mat_count; ++mi) {
                        lumen::render::PbrMaterialUbo mu {};
                        lumen::render::pack_pbr_material_ubo(
                            mu, pbr_materials[mi], emissiveScale);
                        sponza_material_ubos[mi].update(mu);
                    }

                    lumen::render::PbrFrameUbo frame_u_tile {};
                    lumen::render::pack_pbr_frame_ubo(
                        frame_u_tile, view, proj, eye, skyExposure, iblStrength,
                        k_prefilter_max_lod, 0.0F,
                        lumen::render::PBR_FORWARD_DEBUG_TILE_MODES.at(
                            static_cast<std::size_t>(ti)));
                    helmet_frame_ubos[frameIdx].update(frame_u_tile);

                    lumen::render::PbrLightUbo light_u_tile {};
                    lumen::render::fill_pbr_light_ubo_default_points(
                        light_u_tile, pointlightCount, pointDirectStrength);
                    helmet_light_ubos[frameIdx].update(light_u_tile);

                    VkViewport vp_tile {};
                    vp_tile.x = static_cast<float>(col) * cw;
                    // 与「PBR 分屏调试」ImGui 角标自上而下（row 0 在上）一致
                    vp_tile.y = static_cast<float>(row) * ch;
                    vp_tile.width = cw;
                    vp_tile.height = ch;
                    vp_tile.minDepth = 0.0F;
                    vp_tile.maxDepth = 1.0F;
                    vkCmdSetViewport(cb, 0, 1, &vp_tile);
                    const int32_t sx =
                        static_cast<int32_t>(std::lround(vp_tile.x));
                    const int32_t sy =
                        static_cast<int32_t>(std::lround(vp_tile.y));
                    const uint32_t sc_w = static_cast<uint32_t>(
                        (std::max)(1L, std::lround(static_cast<double>(cw))));
                    const uint32_t sc_h = static_cast<uint32_t>(
                        (std::max)(1L, std::lround(static_cast<double>(ch))));
                    VkRect2D scissor_tile { { sx, sy }, { sc_w, sc_h } };
                    vkCmdSetScissor(cb, 0, 1, &scissor_tile);

                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      helmet_pipe.handle());
                    uint32_t draw_slot_dbg = 0;
                    for (const lumen::scene::Primitive &prim :
                         sponza_mesh.primitives) {
                        if (!prim.is_drawable()) {
                            continue;
                        }
                        const lumen::render::Material *prim_mat =
                            prim.material != nullptr ? prim.material
                                                     : &pbr_materials[0];
                        VkDescriptorSet mat_ds_dbg =
                            vk_descriptor_for_pbr_material(
                                prim_mat, pbr_materials, sponza_material_ds);
                        lumen::render::PbrObjectUbo ou_dbg {};
                        ou_dbg.model = helmet_model;
                        const glm::mat3 n3d = glm::mat3(helmet_model);
                        ou_dbg.normalMatrix =
                            glm::mat4(glm::transpose(glm::inverse(n3d)));
                        helmet_object_ubo.update(ou_dbg, draw_slot_dbg *
                                                             helmet_obj_stride);
                        std::array<VkDescriptorSet, 4> sets_dbg {
                            helmet_frame_ds[frameIdx], mat_ds_dbg,
                            helmet_object_ds, helmet_light_ds[frameIdx]
                        };
                        const uint32_t dyn_dbg = static_cast<uint32_t>(
                            draw_slot_dbg * helmet_obj_stride);
                        vkCmdBindDescriptorSets(
                            cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            helmet_pl.handle(), 0,
                            static_cast<uint32_t>(sets_dbg.size()),
                            sets_dbg.data(), 1, &dyn_dbg);
                        ++draw_slot_dbg;
                        VkDeviceSize voff =
                            static_cast<VkDeviceSize>(prim.vertex_byte_offset);
                        VkBuffer vbd = prim.vertex_buffer->handle();
                        vkCmdBindVertexBuffers(cb, 0, 1, &vbd, &voff);
                        vkCmdBindIndexBuffer(
                            cb, prim.index_buffer->handle(), 0,
                            prim.index_buffer->vk_index_type());
                        vkCmdDrawIndexed(cb, prim.index_count, 1,
                                         prim.first_index, 0, 0);
                    }
                }
            }
            vkCmdEndRenderPass(cmd_buf.handle());
        }

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
            ctx.wait_idle();
            if (!frameSync.recreate_in_flight_fence_signaled(frameIdx)) {
                LUMEN_APP_LOG_ERROR("submit 失败后 fence 恢复失败 frameIdx={}",
                                    frameIdx);
                running = false;
            }
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

    window.set_relative_mouse_mode(false);
    vkDeviceWaitIdle(dev);
    if (sceneTexID != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(sceneTexID));
    }
    if (debug_scene_tex_id != static_cast<ImTextureID>(0)) {
        lumen::ui::imgui_backend_remove_texture(
            reinterpret_cast<void *>(debug_scene_tex_id));
    }
    lumen::ui::imgui_backend_shutdown();

    for (VkImageView fv : v_env_faces) {
        destroy_view(dev, fv);
    }
    for (VkImageView fv : v_irr_faces) {
        destroy_view(dev, fv);
    }
    for (VkImageView fv : v_pre_faces) {
        destroy_view(dev, fv);
    }

    std::vector<lumen::render::CommandBuffer> free_bufs;
    for (auto &c : commandBuffers) {
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
