/**
 * @file main.cpp
 * @brief PBR — IBL 烘焙、HDR 天空盒、DamagedHelmet（OBJ + 贴图）与 ImGui
 * 预览
 */

#include "ibl_bake.hpp"

#include "core/logger.hpp"
#include "core/obj_loader.hpp"
#include "core/path.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/frame_sync.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/material/pbr_forward_ubo.hpp"
#include "render/material/pbr_material_bind.hpp"
#include "render/resource/image.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "render/material/material.hpp"
#include "render/shader.hpp"
#include "render/surface.hpp"
#include "render/swapchain.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr const char *kHdrRelPath { "assets/environment_maps/meadow_2_2k.hdr" };

constexpr const char *kHelmetObjRel {
    "assets/models/DamagedHelmet_obj/DamagedHelmet.obj"
};
constexpr const char *kHelmetBaseColor {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_baseColorTexture.jpg"
};
constexpr const char *kHelmetEmissive {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_emissiveTexture.jpg"
};
constexpr const char *kHelmetMetallic {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_metallicTexture.jpg"
};
constexpr const char *kHelmetNormal {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_normalTexture.jpg"
};
constexpr const char *kHelmetOcclusion {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_occlusionTexture.jpg"
};
constexpr const char *kHelmetRoughness {
    "assets/models/DamagedHelmet_obj/DamagedHelmet_roughnessTexture.jpg"
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

    lumen::render::OffscreenRenderTarget scene_target;
    {
        lumen::render::OffscreenRenderTargetConfig scene_cfg;
        scene_cfg.width =
            static_cast<uint32_t>((std::max)(2, window_width * 3 / 4));
        scene_cfg.height =
            static_cast<uint32_t>((std::max)(2, window_height * 3 / 4));
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

    const std::size_t helmet_obj_stride =
        lumen::render::pbr_object_ubo_dynamic_stride(
            static_cast<std::size_t>(
                ctx.physical_device_properties()
                    .limits.minUniformBufferOffsetAlignment));
    const VkDeviceSize helmet_object_ubo_bytes =
        static_cast<VkDeviceSize>(helmet_obj_stride);

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

    const std::string obj_path = lumen::core::get_resource_path(kHelmetObjRel);
    if (!std::filesystem::exists(obj_path)) {
        LUMEN_APP_LOG_ERROR(
            "未找到头盔模型: {} (需 DamagedHelmet.obj 与同目录 JPG)", obj_path);
        return -1;
    }

    lumen::core::ObjMesh helmet_obj {};
    if (!lumen::core::load_obj(obj_path, helmet_obj)) {
        LUMEN_APP_LOG_ERROR("OBJ 解析失败: {}", obj_path);
        return -1;
    }
    if (helmet_obj.vertices.empty()) {
        LUMEN_APP_LOG_ERROR("OBJ 无顶点数据: {}", obj_path);
        return -1;
    }
    if (helmet_obj.indices.empty()) {
        LUMEN_APP_LOG_ERROR("OBJ 无索引数据: {}", obj_path);
        return -1;
    }

    LUMEN_APP_LOG_INFO(
        "OBJ 模型已加载: 顶点数={}, 索引数={}, 面片(三角)数={}, 路径={}",
        helmet_obj.vertices.size(), helmet_obj.indices.size(),
        helmet_obj.indices.size() / 3U, obj_path);

    center_and_scale_mesh(helmet_obj);
    // OBJ `vt` 的 v：0=纹理底边、1=顶边，与 Vulkan 归一化坐标（v=0 为底）一致。
    // create_from_file 已 stbi_set_flip_vertically_on_load(1) 对齐行序，勿再对
    // UV 做 1−v， 否则 Base Color 等与贴图预览会上下错开。

    std::vector<HelmVertex> helm_verts;
    helm_verts.reserve(helmet_obj.vertices.size());
    for (const auto &ov : helmet_obj.vertices) {
        helm_verts.push_back(
            HelmVertex { .position = ov.position,
                         .normal = ov.normal,
                         .uv = ov.uv,
                         .tangent = { 1.0F, 0.0F, 0.0F, 1.0F } });
    }
    compute_mesh_tangents(helm_verts, helmet_obj.indices);
    const uint32_t helmet_index_count =
        static_cast<uint32_t>(helmet_obj.indices.size());

    VkQueue gq = ctx.graphics_queue();
    lumen::render::Texture tex_albedo;
    lumen::render::Texture tex_normal;
    lumen::render::Texture tex_mr;
    lumen::render::Texture tex_ao;
    lumen::render::Texture tex_emissive;
    const std::string p_albedo =
        lumen::core::get_resource_path(kHelmetBaseColor);
    const std::string p_normal = lumen::core::get_resource_path(kHelmetNormal);
    const std::string p_metallic =
        lumen::core::get_resource_path(kHelmetMetallic);
    const std::string p_roughness =
        lumen::core::get_resource_path(kHelmetRoughness);
    const std::string p_ao = lumen::core::get_resource_path(kHelmetOcclusion);
    const std::string p_emissive =
        lumen::core::get_resource_path(kHelmetEmissive);

    if (!tex_albedo.create_from_file(ctx, p_albedo.c_str(), gq, cmdPool, {},
                                     VK_FORMAT_R8G8B8A8_SRGB)) {
        LUMEN_APP_LOG_ERROR("贴图加载失败 (baseColor): {}", p_albedo);
        return -1;
    }
    if (!tex_normal.create_from_file(ctx, p_normal.c_str(), gq, cmdPool, {},
                                     VK_FORMAT_R8G8B8A8_UNORM)) {
        LUMEN_APP_LOG_ERROR("贴图加载失败 (normal): {}", p_normal);
        return -1;
    }
    if (!lumen::render::create_metallic_roughness_texture_from_grayscale_files(
            tex_mr, ctx, p_metallic.c_str(), p_roughness.c_str(), gq, cmdPool)) {
        LUMEN_APP_LOG_ERROR("金属粗糙度合并失败: {} + {}", p_metallic,
                            p_roughness);
        return -1;
    }
    if (!tex_ao.create_from_file(ctx, p_ao.c_str(), gq, cmdPool, {},
                                 VK_FORMAT_R8G8B8A8_UNORM)) {
        LUMEN_APP_LOG_ERROR("贴图加载失败 (occlusion): {}", p_ao);
        return -1;
    }
    if (!tex_emissive.create_from_file(ctx, p_emissive.c_str(), gq, cmdPool, {},
                                       VK_FORMAT_R8G8B8A8_SRGB)) {
        LUMEN_APP_LOG_ERROR("贴图加载失败 (emissive): {}", p_emissive);
        return -1;
    }

    lumen::render::PbrPlaceholderTextures pbr_placeholders;
    if (!pbr_placeholders.create(ctx, gq, cmdPool) ||
        !pbr_placeholders.is_complete()) {
        LUMEN_APP_LOG_ERROR("PBR 占位贴图创建失败");
        return -1;
    }

    lumen::render::Material helmet_mat {};
    helmet_mat.emissiveFactor = glm::vec3(1.0F, 1.0F, 1.0F);
    helmet_mat.baseColorTex = &tex_albedo;
    helmet_mat.normalTex = &tex_normal;
    helmet_mat.metallicRoughnessTex = &tex_mr;
    helmet_mat.occlusionTex = &tex_ao;
    helmet_mat.emissiveTex = &tex_emissive;

    lumen::render::VertexBuffer helmet_vbuf;
    if (!helmet_vbuf.create_device_local_and_upload(
            ctx, gq, cmdPool, helm_verts.data(),
            helm_verts.size() * sizeof(HelmVertex))) {
        LUMEN_APP_LOG_ERROR("头盔顶点缓冲失败");
        return -1;
    }
    lumen::render::IndexBuffer helmet_ibuf;
    helmet_ibuf.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!helmet_ibuf.create_device_local_and_upload(
            ctx, gq, cmdPool, helmet_obj.indices.data(),
            helmet_obj.indices.size() * sizeof(uint32_t))) {
        LUMEN_APP_LOG_ERROR("头盔索引缓冲失败");
        return -1;
    }

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

    auto scene_tex_id =
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

    lumen::render::DescriptorPool helmet_dpool;
    if (!helmet_dpool.create(
            ctx,
            { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .count = 7 },
              { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .count = 1 },
              { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .count = 3U * 3U + 5U } },
            8)) {
        LUMEN_APP_LOG_ERROR("PBR DescriptorPool 失败");
        return -1;
    }

    lumen::render::UniformBuffer helmet_material_ubo;
    if (!helmet_material_ubo.create_persistent(
            ctx, sizeof(lumen::render::PbrMaterialUbo))) {
        LUMEN_APP_LOG_ERROR("材质 UniformBuffer 失败");
        return -1;
    }
    VkDescriptorSet helmet_material_ds { VK_NULL_HANDLE };
    if (!helmet_dpool.allocate(dev, helmet_material_dsl.handle(),
                               helmet_material_ds)) {
        LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败");
        return -1;
    }
    {
        lumen::render::PbrMaterialUbo mu {};
        lumen::render::pack_pbr_material_ubo(mu, helmet_mat, 3.0F);
        helmet_material_ubo.update(mu);
    }
    lumen::render::write_pbr_material_descriptor_set(
        dev, helmet_material_ds, helmet_material_ubo.handle(),
        sizeof(lumen::render::PbrMaterialUbo), helmet_mat, pbr_placeholders);

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
        "pbr 资源就绪: OBJ 顶点={} 索引={} 三角形={}, 着色器 {} | {}",
        helmet_obj.vertices.size(), helmet_obj.indices.size(),
        helmet_obj.indices.size() / 3, helmet_vs_path, helmet_fs_path);

    float skyExposure { 4.0F };
    float iblStrength { 3.0F };
    float emissiveScale { 3.0F };
    int pointlightCount { lumen::render::PBR_LEGACY_POINT_LIGHT_CAP };
    float pointDirectStrength { 1.15F };
    int helmetDebugView { 0 };
    bool pbrDebugTileGrid { false };
    struct CamState {
        float yaw { -90.0F };
        float pitch { 0.0F };
        bool rmb_down { false };
        /// 相机距原点（轨道半径），与 lookAt 目标一致
        float orbit_radius { 2.35F };
    } cam {};

    uint32_t pendingSceneWidth { scene_target.width() };
    uint32_t pendingSceneHeight { scene_target.height() };

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

    auto img_albedo =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_albedo.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto img_normal =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_normal.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto img_metallic =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_mr.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto img_roughness =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_mr.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto img_ao =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_ao.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    auto img_emissive =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            uiSampler.handle(), tex_emissive.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

    lumen::platform::EventPump pump;
    uint32_t frameIdx { 0 };
    bool running { true };
    bool need_recreate_swapchain { false };

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
        d.dispatch<lumen::platform::EventMouseButtonDown>(
            [&](lumen::platform::EventMouseButtonDown &e) {
                if (e.button == lumen::platform::MouseButton::Right) {
                    cam.rmb_down = true;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventMouseButtonUp>(
            [&](lumen::platform::EventMouseButtonUp &e) {
                if (e.button == lumen::platform::MouseButton::Right) {
                    cam.rmb_down = false;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventMouseMove>(
            [&](lumen::platform::EventMouseMove &e) {
                if (cam.rmb_down) {
                    cam.yaw += e.deltaX * 0.18F;
                    cam.pitch -= e.deltaY * 0.18F;
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

        if (pendingSceneWidth >= 2U && pendingSceneHeight >= 2U &&
            (pendingSceneWidth != scene_target.width() ||
             pendingSceneHeight != scene_target.height())) {
            ctx.wait_idle();
            lumen::ui::imgui_backend_remove_texture(
                reinterpret_cast<void *>(scene_tex_id));
            scene_tex_id = static_cast<ImTextureID>(0);
            if (!scene_target.resize(pendingSceneWidth, pendingSceneHeight)) {
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
            pendingSceneWidth =
                (std::max)(2U,
                           static_cast<uint32_t>((std::max)(1.0F, vp_avail.x)));
            pendingSceneHeight =
                (std::max)(2U,
                           static_cast<uint32_t>((std::max)(1.0F, vp_avail.y)));
            if (scene_tex_id != static_cast<ImTextureID>(0)) {
                ImGui::Image(scene_tex_id, ImVec2(static_cast<float>(scene_w),
                                                  static_cast<float>(scene_h)));
                // 指针在画面上时滚轮缩放（悬停 Image 时 WantCaptureMouse 为
                // true， 若在 SDL 事件里用 imgui_wants_mouse 过滤会永远进不来）
                if (ImGui::IsItemHovered()) {
                    constexpr float k_orbit_min { 0.45F };
                    constexpr float k_orbit_max { 28.0F };
                    constexpr float k_wheel_scale { 0.14F };
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0F) {
                        cam.orbit_radius -= wheel * k_wheel_scale;
                        cam.orbit_radius = std::clamp(cam.orbit_radius,
                                                      k_orbit_min, k_orbit_max);
                    }
                }
            }
            if (pbrDebugTileGrid &&
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
        }
        ImGui::End();

        if (ImGui::Begin("IBL 预览")) {
            ImGui::TextUnformatted(
                "右键拖拽旋转；鼠标在「3D 视口」画面上滚轮缩放（相机绕原点）");
            ImGui::SliderFloat("轨道距离（缩放）", &cam.orbit_radius, 0.45F,
                               28.0F, "%.2f");
            ImGui::SliderFloat("天空曝光", &skyExposure, 0.05F, 4.0F, "%.2f");
            ImGui::SliderFloat("IBL 强度", &iblStrength, 0.0F, 3.0F, "%.2f");
            ImGui::SliderFloat("自发光倍率", &emissiveScale, 0.0F, 12.0F,
                               "%.1f");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "点光源（GGX 直射，仅「PBR 完整 / 分屏首格」）");
            ImGui::SliderInt("点光数量", &pointlightCount, 0,
                             lumen::render::PBR_FORWARD_MAX_LIGHTS);
            ImGui::SliderFloat("点光强度", &pointDirectStrength, 0.0F, 6.0F,
                               "%.2f");
            ImGui::Separator();
            ImGui::Checkbox("分屏光照调试 (4×4，仅头盔)", &pbrDebugTileGrid);
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

        if (ImGui::Begin("DamagedHelmet 材质贴图")) {
            const ImVec2 thumb(140, 140);
            auto helmet_tex_cell = [&](const char *label, ImTextureID tex_id) {
                ImGui::BeginGroup();
                ImGui::TextUnformatted(label);
                ImGui::Image(tex_id, thumb);
                ImGui::EndGroup();
                ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
            };
            helmet_tex_cell("Base Color (sRGB)", img_albedo);
            helmet_tex_cell("Normal", img_normal);
            helmet_tex_cell("Metallic", img_metallic);
            helmet_tex_cell("Roughness", img_roughness);
            helmet_tex_cell("Ambient Occlusion", img_ao);
            ImGui::BeginGroup();
            ImGui::TextUnformatted("Emissive");
            ImGui::Image(img_emissive, thumb);
            ImGui::EndGroup();
        }
        ImGui::End();

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

            glm::vec3 forward {};
            forward.x = std::cos(glm::radians(cam.yaw)) *
                        std::cos(glm::radians(cam.pitch));
            forward.y = std::sin(glm::radians(cam.pitch));
            forward.z = std::sin(glm::radians(cam.yaw)) *
                        std::cos(glm::radians(cam.pitch));
            forward = glm::normalize(forward);
            const glm::vec3 eye = -forward * cam.orbit_radius;
            const glm::mat4 view =
                glm::lookAt(eye, glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
            const glm::mat4 sky_v = glm::mat4(glm::mat3(view));

            VkCommandBuffer cb = cmd_buf.handle();
            const glm::mat4 helmet_model { 1.0F };

            if (!pbrDebugTileGrid) {
                const float aspect =
                    wf / static_cast<float>((std::max)(1U, ext.height));
                glm::mat4 proj =
                    glm::perspective(glm::radians(55.0F), aspect, 0.05F, 50.0F);
                proj[1][1] *= -1.0F;

                lumen::render::PbrMaterialUbo mu {};
                lumen::render::pack_pbr_material_ubo(mu, helmet_mat, emissiveScale);
                helmet_material_ubo.update(mu);

                lumen::render::PbrFrameUbo frame_u {};
                lumen::render::pack_pbr_frame_ubo(
                    frame_u, view, proj, eye, skyExposure, iblStrength,
                    k_prefilter_max_lod, 0.0F, helmetDebugView);
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
                lumen::render::PbrObjectUbo ou {};
                ou.model = helmet_model;
                const glm::mat3 n3 = glm::mat3(helmet_model);
                ou.normalMatrix =
                    glm::mat4(glm::transpose(glm::inverse(n3)));
                helmet_object_ubo.update(ou, 0);
                std::array<VkDescriptorSet, 4> pbr_sets {
                    helmet_frame_ds[frameIdx], helmet_material_ds,
                    helmet_object_ds, helmet_light_ds[frameIdx]
                };
                const uint32_t dyn0 = 0;
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        helmet_pl.handle(), 0,
                                        static_cast<uint32_t>(pbr_sets.size()),
                                        pbr_sets.data(), 1, &dyn0);
                VkDeviceSize hv_off { 0 };
                VkBuffer hvb = helmet_vbuf.handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &hvb, &hv_off);
                vkCmdBindIndexBuffer(cb, helmet_ibuf.handle(), 0,
                                     helmet_ibuf.vk_index_type());
                vkCmdDrawIndexed(cb, helmet_index_count, 1, 0, 0, 0);
            } else {
                constexpr int k_dbg_tile_cols = 4;
                constexpr int k_dbg_tile_rows = 4;
                constexpr int k_dbg_tile_count =
                    k_dbg_tile_cols * k_dbg_tile_rows;
                const float cw = wf / static_cast<float>(k_dbg_tile_cols);
                const float ch = hf / static_cast<float>(k_dbg_tile_rows);
                VkBuffer hvb = helmet_vbuf.handle();
                VkDeviceSize hv_off_zero { 0 };

                for (int ti = 0; ti < k_dbg_tile_count; ++ti) {
                    const int col = ti % k_dbg_tile_cols;
                    const int row = ti / k_dbg_tile_cols;
                    const float aspect =
                        cw / static_cast<float>((std::max)(1.0F, ch));
                    glm::mat4 proj = glm::perspective(glm::radians(55.0F),
                                                      aspect, 0.05F, 50.0F);
                    proj[1][1] *= -1.0F;

                    lumen::render::PbrMaterialUbo mu_tile {};
                    lumen::render::pack_pbr_material_ubo(mu_tile, helmet_mat,
                                                         emissiveScale);
                    helmet_material_ubo.update(mu_tile);

                    lumen::render::PbrFrameUbo frame_tile {};
                    lumen::render::pack_pbr_frame_ubo(
                        frame_tile, view, proj, eye, skyExposure, iblStrength,
                        k_prefilter_max_lod, 0.0F,
                        lumen::render::PBR_FORWARD_DEBUG_TILE_MODES.at(
                            static_cast<std::size_t>(ti)));
                    helmet_frame_ubos[frameIdx].update(frame_tile);

                    lumen::render::PbrLightUbo light_tile {};
                    lumen::render::fill_pbr_light_ubo_default_points(
                        light_tile, pointlightCount, pointDirectStrength);
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
                    lumen::render::PbrObjectUbo ou_tile {};
                    ou_tile.model = helmet_model;
                    const glm::mat3 n3t = glm::mat3(helmet_model);
                    ou_tile.normalMatrix =
                        glm::mat4(glm::transpose(glm::inverse(n3t)));
                    helmet_object_ubo.update(ou_tile, 0);
                    std::array<VkDescriptorSet, 4> pbr_sets_tile {
                        helmet_frame_ds[frameIdx], helmet_material_ds,
                        helmet_object_ds, helmet_light_ds[frameIdx]
                    };
                    const uint32_t dyn_tile = 0;
                    vkCmdBindDescriptorSets(
                        cb, VK_PIPELINE_BIND_POINT_GRAPHICS, helmet_pl.handle(),
                        0, static_cast<uint32_t>(pbr_sets_tile.size()),
                        pbr_sets_tile.data(), 1, &dyn_tile);
                    vkCmdBindVertexBuffers(cb, 0, 1, &hvb, &hv_off_zero);
                    vkCmdBindIndexBuffer(cb, helmet_ibuf.handle(), 0,
                                         helmet_ibuf.vk_index_type());
                    vkCmdDrawIndexed(cb, helmet_index_count, 1, 0, 0, 0);
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

    destroy_view(dev, v_env);
    destroy_view(dev, v_irr);
    destroy_view(dev, v_pre);

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
