/**
 * @file main.cpp
 * @brief Demo3D：进入 3D 世界 - 透视、深度缓冲、OBJ 模型加载
 */

#include "engine.hpp"

#include "core/gltf_loader.hpp"
#include "core/logger.hpp"
#include "core/obj_loader.hpp"
#include "core/path.hpp"
#include "core/time.hpp"
#include "platform/event_debug.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_graph.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/material_texture_mask.hpp"
#include "render/pbr_material_ubo.hpp"
#include "render/pipeline.hpp"
#include "render/resource/cubemap_file_loader.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"
#include "scene/components.hpp"
#include "scene/light.hpp"
#include "scene/scene.hpp"
#include "scene/scene_camera_controller.hpp"
#include "scene/scene_environment.hpp"
#include "scene/scene_orbit_camera.hpp"
#include "scene/transform.hpp"
#include "ui/editor_selection.hpp"
#include "ui/environment_panel.hpp"
#include "ui/gizmo.hpp"
#include "ui/gpu_capabilities_panel.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/input_bridge.hpp"
#include "ui/light_viewport_gizmos.hpp"
#include "ui/log_panel.hpp"
#include "ui/scene_hierarchy_panel.hpp"
#include "ui/scene_inspector_panel.hpp"
#include "ui/texture_view_panel.hpp"

#include "pbr_resources.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

using Vertex = lumen::core::ObjVertex;

/// Scene UBO：与 `cube.vert` / `cube.frag` / `skybox.*` 的 `SceneUBO`
/// 一致（std140）
///
/// 注意：GLSL `mat3` 在 std140 中按 3×vec4 列存储（48 字节），与 `glm::mat3`
/// （36 字节）不一致，会导致后续 `cameraWorld`、`lights` 整体错位。此处用
/// `mat4` 存法线矩阵（仅左上 3×3 有效），与着色器对齐。
struct SceneUbo {
    glm::mat4 model;
    glm::mat4 mvp;
    glm::mat4 normalMatrix;
    glm::vec4 cameraWorld;
    lumen::scene::GPULight lights[lumen::scene::kMaxLightsUbo];
    glm::vec4 sceneParams;
    glm::mat4 skyMvp;
    glm::mat4 skyOrientInv;
    /// x=曝光, y=maxMip, z=diffuseMip, w=IBL 强度
    glm::vec4 envParams;
};

/// Push Constants：mode + modelColor
struct PushConstants {
    uint32_t mode;
    float _pad[3];
    glm::vec4 modelColor;
};

namespace {

std::string resolve_asset_path(const std::string &p) {
    namespace fs = std::filesystem;
    if (p.empty()) {
        return {};
    }
    std::error_code ec;
    const fs::path try_abs = fs::absolute(fs::path(p), ec);
    if (!ec && fs::exists(try_abs, ec)) {
        return try_abs.string();
    }
    const std::string via = lumen::core::get_resource_path(p);
    if (fs::exists(via, ec)) {
        return via;
    }
    return p;
}

bool path_ends_with_ci(std::string_view s, std::string_view ext) {
    if (s.size() < ext.size()) {
        return false;
    }
    for (size_t i = 0; i < ext.size(); ++i) {
        const char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(s[s.size() - ext.size() + i])));
        const char b = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

void write_material_descriptor_images(
    VkDevice dev, VkDescriptorSet set,
    const lumen::render::PbrPlaceholderTextures &ph,
    const lumen::render::Texture *albedo_override,
    const lumen::render::Texture *normal_override,
    const lumen::render::Texture *mr_override,
    const lumen::render::Texture *ao_override,
    const lumen::render::Texture *emissive_override) {
    auto one = [&](const lumen::render::Texture *ov,
                   const lumen::render::Texture &fallback, uint32_t binding) {
        const lumen::render::Texture &use =
            (ov && ov->is_valid()) ? *ov : fallback;
        lumen::render::write_descriptor_image(dev, set, binding, use.view(),
                                              use.sampler());
    };
    one(albedo_override, ph.albedo(), 1);
    one(normal_override, ph.normal(), 2);
    one(mr_override, ph.metallic_roughness(), 3);
    one(ao_override, ph.ao(), 4);
    one(emissive_override, ph.emissive(), 5);
}

constexpr uint32_t kMaxFramesInFlight { 2 };
constexpr const char *kGltfPath { "assets/model/adamHead/adamHead.gltf" };
constexpr const char *kObjPath { "assets/model/Mythra_1.04/Mythra_1.04.obj" };
/// ViewManipulate 命中区边长；略小可减少与 Dock 边界的溢出感
constexpr float kViewCubeSize { 96.0f };
constexpr float kViewCubeMargin { 8.0f };
/// 控件实际绘制会略超出传入矩形，右侧需额外内缩避免「出界」
constexpr float kViewCubeRightBleed { 22.0f };
/// 光源图标在世界空间中的半宽/半高（局部 xy ∈ [-1,1]）
constexpr float kLightIconHalfExtent { 0.18f };

/// 将 ViewManipulate 放在矩形右上角内侧（优先 Scene 视口屏幕矩形）
void place_view_cube_top_right(const lumen::ui::TextureViewRect &sceneRect,
                               const ImGuiViewport &mainVp, float &outX,
                               float &outY) {
    const float sz = kViewCubeSize;
    const float m = kViewCubeMargin;
    const float br = kViewCubeRightBleed;
    const auto try_rect = [&](float rx, float ry, float rw, float rh) -> bool {
        if (!std::isfinite(rw) || !std::isfinite(rh) || rw < sz + m * 2 + br ||
            rh < sz + m * 2) {
            return false;
        }
        const float minX = rx + m;
        const float minY = ry + m;
        const float maxX = rx + rw - sz - m - br;
        const float maxY = ry + rh - sz - m;
        if (maxX < minX || maxY < minY) {
            return false;
        }
        const float preferX = rx + rw - sz - m - br;
        const float preferY = ry + m;
        outX = std::clamp(preferX, minX, maxX);
        outY = std::clamp(preferY, minY, maxY);
        return true;
    };

    const float sw = sceneRect.width();
    const float sh = sceneRect.height();
    if (try_rect(sceneRect.minX, sceneRect.minY, sw, sh)) {
        return;
    }
    if (try_rect(mainVp.WorkPos.x, mainVp.WorkPos.y, mainVp.WorkSize.x,
                 mainVp.WorkSize.y)) {
        return;
    }
    outX = mainVp.WorkPos.x + m;
    outY = mainVp.WorkPos.y + m;
}

/// LearnOpenGL 天空盒顶点与索引（剔除正面，自内向外可见）
constexpr float kSkyVertices[8 * 3] {
    -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
    -1.0f, 1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
    1.0f,  1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f, 1.0f,
};
constexpr std::uint32_t kSkyIndices[36] { 1, 2, 6, 6, 5, 1, 0, 4, 7, 7, 3, 0,
                                          3, 7, 6, 6, 2, 3, 0, 1, 5, 5, 4, 0,
                                          0, 3, 2, 2, 1, 0, 4, 5, 6, 6, 7, 4 };

/// 与 Unity Scene 视图一致：Q 视图、W 移动、E 旋转、R 缩放
enum class SceneGizmoTool : std::uint8_t {
    View,
    Move,
    Rotate,
    Scale,
};

ImGuizmo::OPERATION scene_gizmo_to_operation(SceneGizmoTool t) {
    switch (t) {
    case SceneGizmoTool::Move: return ImGuizmo::TRANSLATE;
    case SceneGizmoTool::Rotate: return ImGuizmo::ROTATE;
    case SceneGizmoTool::Scale: return ImGuizmo::SCALE;
    case SceneGizmoTool::View:
    default: return ImGuizmo::TRANSLATE;
    }
}

} // namespace

/// XZ 地面网格：`kind` 0 细线、1 粗线（每 10 格）、2 世界 +X 轴、3 世界 +Z 轴
struct GroundGridVertex {
    glm::vec3 position {};
    float kind { 0.0f };
};

static void fill_ground_grid_vertices(int half_cells, float step, float plane_y,
                                      std::vector<GroundGridVertex> &out) {
    out.clear();
    const float L = static_cast<float>(half_cells) * step;
    for (int iz = -half_cells; iz <= half_cells; ++iz) {
        const float z = static_cast<float>(iz) * step;
        float kind = 0.0f;
        if (iz == 0) {
            kind = 2.0f;
        } else if (iz % 10 == 0) {
            kind = 1.0f;
        }
        out.push_back({ { -L, plane_y, z }, kind });
        out.push_back({ { L, plane_y, z }, kind });
    }
    for (int ix = -half_cells; ix <= half_cells; ++ix) {
        const float x = static_cast<float>(ix) * step;
        float kind = 0.0f;
        if (ix == 0) {
            kind = 3.0f;
        } else if (ix % 10 == 0) {
            kind = 1.0f;
        }
        out.push_back({ { x, plane_y, -L }, kind });
        out.push_back({ { x, plane_y, L }, kind });
    }
}

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

    lumen::render::Surface surface(
        ctx.instance(), window.create_vulkan_surface(ctx.instance()));
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

    // 离屏渲染目标：Scene → OffscreenRenderTarget → ImGui 采样
    lumen::render::OffscreenRenderTargetConfig sceneTargetConfig;
    sceneTargetConfig.width = static_cast<uint32_t>(w);
    sceneTargetConfig.height = static_cast<uint32_t>(h);
    sceneTargetConfig.format = swapchain.image_format();
    sceneTargetConfig.useDepth = true;
    sceneTargetConfig.colorFinalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::OffscreenRenderTarget sceneTarget;
    if (!sceneTarget.create(ctx, sceneTargetConfig)) {
        LUMEN_APP_LOG_ERROR("场景离屏目标创建失败");
        return -1;
    }

    constexpr uint32_t kAuxViewportW { 320 };
    constexpr uint32_t kAuxViewportH { 240 };
    lumen::render::OffscreenRenderTargetConfig auxConfig;
    auxConfig.width = kAuxViewportW;
    auxConfig.height = kAuxViewportH;
    auxConfig.format = swapchain.image_format();
    auxConfig.useDepth = true;
    auxConfig.colorFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::OffscreenRenderTarget wireframeTarget;
    lumen::render::OffscreenRenderTarget normalTarget;
    lumen::render::OffscreenRenderTarget depthTarget;
    if (!wireframeTarget.create(ctx, auxConfig) ||
        !normalTarget.create(ctx, auxConfig) ||
        !depthTarget.create(ctx, auxConfig)) {
        LUMEN_APP_LOG_ERROR("辅助视口创建失败");
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

    std::string sky_vert_path =
        lumen::core::get_resource_path("shaders/skybox.vert.spv");
    std::string sky_frag_path =
        lumen::core::get_resource_path("shaders/skybox.frag.spv");
    lumen::render::ShaderModule sky_vert_shader;
    lumen::render::ShaderModule sky_frag_shader;
    if (!sky_vert_shader.create_from_file(ctx.device(),
                                          sky_vert_path.c_str()) ||
        !sky_frag_shader.create_from_file(ctx.device(),
                                          sky_frag_path.c_str())) {
        LUMEN_APP_LOG_ERROR("天空盒着色器加载失败");
        return -1;
    }

    std::string grid_vert_path =
        lumen::core::get_resource_path("shaders/grid.vert.spv");
    std::string grid_frag_path =
        lumen::core::get_resource_path("shaders/grid.frag.spv");
    lumen::render::ShaderModule grid_vert_shader;
    lumen::render::ShaderModule grid_frag_shader;
    if (!grid_vert_shader.create_from_file(ctx.device(),
                                           grid_vert_path.c_str()) ||
        !grid_frag_shader.create_from_file(ctx.device(),
                                           grid_frag_path.c_str())) {
        LUMEN_APP_LOG_ERROR("地面网格着色器加载失败");
        return -1;
    }

    // glTF 优先，失败则回退 OBJ
    lumen::core::ObjMesh mesh;
    lumen::scene::MaterialComponent gltf_material {};
    std::vector<lumen::core::GltfSubmeshRange> gltf_submeshes;
    std::vector<lumen::scene::MaterialComponent> gltf_materials;
    std::unordered_map<std::string, lumen::render::Texture> gltf_tex_cache;
    bool loaded_from_gltf = false;
    const std::string gltfPath = lumen::core::get_resource_path(kGltfPath);
    const std::string objPath = lumen::core::get_resource_path(kObjPath);
    if (lumen::core::load_gltf(gltfPath, mesh, gltf_material, &gltf_submeshes,
                                &gltf_materials)) {
        loaded_from_gltf = true;
    } else if (!lumen::core::load_obj(objPath, mesh)) {
        LUMEN_APP_LOG_ERROR("模型加载失败（glTF 与 OBJ 均失败） glTF: {} OBJ: {}",
                            gltfPath, objPath);
        return -1;
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        LUMEN_APP_LOG_ERROR("OBJ 模型为空: {}", objPath);
        return -1;
    }

    glm::vec3 mesh_center_local { 0.0f };
    glm::vec3 mesh_half_extents_local { 0.0f };
    {
        glm::vec3 mn = mesh.vertices[0].position;
        glm::vec3 mx = mn;
        for (const Vertex &v : mesh.vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        mesh_center_local = 0.5f * (mn + mx);
        mesh_half_extents_local = 0.5f * (mx - mn);
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
    const bool use_gltf_multimat =
        loaded_from_gltf && !gltf_submeshes.empty() && !gltf_materials.empty();
    static const lumen::scene::MaterialComponent kGltfMissingMaterial = [] {
        lumen::scene::MaterialComponent m {};
        m.base_color_factor = glm::vec4(1.0f);
        m.metallic_factor = 0.0f;
        m.roughness_factor = 0.5f;
        m.ao_factor = 1.0f;
        return m;
    }();

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        return -1;
    }

    lumen::scene::SceneEnvironment scene_env {};

    lumen::render::PbrPlaceholderTextures placeholder_textures;
    if (!placeholder_textures.create(ctx, ctx.graphics_queue(), cmdPool)) {
        LUMEN_APP_LOG_ERROR("PBR 占位纹理创建失败");
        return -1;
    }

    lumen::render::Texture mat_tex_albedo;
    lumen::render::Texture mat_tex_normal;
    lumen::render::Texture mat_tex_mr;
    lumen::render::Texture mat_tex_ao;
    lumen::render::Texture mat_tex_emissive;

    const std::string kDefaultAlbedoPath = lumen::core::get_resource_path(
        "assets/textures/ikun2026_happy_new_year.jpg");
    if (!mat_tex_albedo.create_from_file(ctx, kDefaultAlbedoPath.c_str(),
                                         ctx.graphics_queue(), cmdPool)) {
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
        mat_tex_albedo.create_from_memory(ctx, pixels.data(), pixels.size(),
                                          kTexSize, kTexSize,
                                          ctx.graphics_queue(), cmdPool);
    }

    std::array<std::vector<std::uint8_t>, 6> sky_face_pixels {};
    constexpr std::uint32_t kEnvFaceSize { 256 };
    demo3d::pbr::fill_procedural_sky_faces(kEnvFaceSize, sky_face_pixels);
    const void *sky_face_ptrs[6] = {
        sky_face_pixels[0].data(), sky_face_pixels[1].data(),
        sky_face_pixels[2].data(), sky_face_pixels[3].data(),
        sky_face_pixels[4].data(), sky_face_pixels[5].data()
    };
    lumen::render::Texture env_cubemap;
    lumen::render::SamplerConfig env_sampler_cfg {};
    env_sampler_cfg.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!env_cubemap.create_cubemap_from_rgba8_faces(
            ctx, sky_face_ptrs, kEnvFaceSize, ctx.graphics_queue(), cmdPool,
            env_sampler_cfg)) {
        LUMEN_APP_LOG_ERROR("环境立方体贴图创建失败");
        return -1;
    }

    std::vector<std::uint8_t> brdf_lut_rgba;
    demo3d::pbr::generate_brdf_lut_rgba8(brdf_lut_rgba, 256);
    lumen::render::Texture brdf_lut_tex;
    if (!brdf_lut_tex.create_from_memory(
            ctx, brdf_lut_rgba.data(), brdf_lut_rgba.size(), 256, 256,
            ctx.graphics_queue(), cmdPool, VK_FORMAT_R8G8B8A8_UNORM,
            lumen::render::SamplerConfig {}, false)) {
        LUMEN_APP_LOG_ERROR("BRDF LUT 纹理创建失败");
        return -1;
    }

    lumen::render::VertexBuffer sky_vertex_buffer;
    lumen::render::IndexBuffer sky_index_buffer;
    if (!sky_vertex_buffer.create(ctx, sizeof(kSkyVertices)) ||
        !sky_index_buffer.create(ctx, sizeof(kSkyIndices))) {
        return -1;
    }
    sky_vertex_buffer.upload(kSkyVertices, sizeof(kSkyVertices));
    sky_index_buffer.set_index_type(
        lumen::render::IndexBuffer::IndexType::Uint32);
    sky_index_buffer.upload(kSkyIndices, sizeof(kSkyIndices));

    std::vector<GroundGridVertex> ground_grid_vertices;
    constexpr int kGridHalfCells { 50 };
    constexpr float kGridStep { 1.0f };
    constexpr float kGridPlaneY { 0.0005f };
    fill_ground_grid_vertices(kGridHalfCells, kGridStep, kGridPlaneY,
                              ground_grid_vertices);
    const uint32_t grid_vertex_count =
        static_cast<uint32_t>(ground_grid_vertices.size());
    lumen::render::VertexBuffer grid_vertex_buffer;
    if (!grid_vertex_buffer.create(ctx, ground_grid_vertices.size() *
                                            sizeof(GroundGridVertex))) {
        return -1;
    }
    grid_vertex_buffer.upload(ground_grid_vertices.data(),
                              ground_grid_vertices.size() *
                                  sizeof(GroundGridVertex));

    // Descriptor：Set0 场景 UBO + env 立方体 + BRDF LUT；Set1 材质 UBO +
    // 五张贴图
    lumen::render::DescriptorSetLayout sceneDescLayout;
    sceneDescLayout.create(
        ctx, { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
               { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT },
               { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT } });

    lumen::render::DescriptorSetLayout materialDescLayout;
    materialDescLayout.create(ctx,
                              { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                  VK_SHADER_STAGE_FRAGMENT_BIT },
                                { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  1, VK_SHADER_STAGE_FRAGMENT_BIT },
                                { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  1, VK_SHADER_STAGE_FRAGMENT_BIT },
                                { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  1, VK_SHADER_STAGE_FRAGMENT_BIT },
                                { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  1, VK_SHADER_STAGE_FRAGMENT_BIT },
                                { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  1, VK_SHADER_STAGE_FRAGMENT_BIT } });

    lumen::render::DescriptorPool descPool;
    descPool.create(
        ctx,
        { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight * 2 },
          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            kMaxFramesInFlight * 7 } },
        kMaxFramesInFlight * 2);

    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight>
        materialUniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSetsScene {};
    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSetsMaterial {};

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        uniformBuffers[i].create(ctx, sizeof(SceneUbo));
        materialUniformBuffers[i].create(ctx,
                                         sizeof(lumen::render::PbrMaterialUbo));
        descPool.allocate(ctx.device(), sceneDescLayout.handle(),
                          descriptorSetsScene[i]);
        descPool.allocate(ctx.device(), materialDescLayout.handle(),
                          descriptorSetsMaterial[i]);
        lumen::render::write_descriptor_buffer(
            ctx.device(), descriptorSetsScene[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBuffers[i].handle(), 0,
            sizeof(SceneUbo));
        lumen::render::write_descriptor_buffer(
            ctx.device(), descriptorSetsMaterial[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            materialUniformBuffers[i].handle(), 0,
            sizeof(lumen::render::PbrMaterialUbo));
        lumen::render::write_descriptor_image(
            ctx.device(), descriptorSetsScene[i], 1, env_cubemap.view(),
            env_cubemap.sampler());
        lumen::render::write_descriptor_image(
            ctx.device(), descriptorSetsScene[i], 2, brdf_lut_tex.view(),
            brdf_lut_tex.sampler());
        write_material_descriptor_images(
            ctx.device(), descriptorSetsMaterial[i], placeholder_textures,
            &mat_tex_albedo,
            mat_tex_normal.is_valid() ? &mat_tex_normal : nullptr,
            mat_tex_mr.is_valid() ? &mat_tex_mr : nullptr,
            mat_tex_ao.is_valid() ? &mat_tex_ao : nullptr,
            mat_tex_emissive.is_valid() ? &mat_tex_emissive : nullptr);
    }

    auto reload_environment_gpu = [&]() {
        ctx.wait_idle();
        env_cubemap = lumen::render::Texture {};
        if (scene_env.cubemap_directory.empty()) {
            std::array<std::vector<std::uint8_t>, 6> sky_face_pixels {};
            constexpr std::uint32_t kEnvFaceSize { 256 };
            demo3d::pbr::fill_procedural_sky_faces(kEnvFaceSize,
                                                   sky_face_pixels);
            const void *sky_face_ptrs[6] = {
                sky_face_pixels[0].data(), sky_face_pixels[1].data(),
                sky_face_pixels[2].data(), sky_face_pixels[3].data(),
                sky_face_pixels[4].data(), sky_face_pixels[5].data()
            };
            if (!env_cubemap.create_cubemap_from_rgba8_faces(
                    ctx, sky_face_ptrs, kEnvFaceSize, ctx.graphics_queue(),
                    cmdPool, env_sampler_cfg)) {
                LUMEN_APP_LOG_ERROR("程序化天空立方体重建失败");
                return;
            }
        } else {
            namespace fs = std::filesystem;
            const std::string resolved =
                resolve_asset_path(scene_env.cubemap_directory);
            const fs::path env_path = fs::path(resolved);
            std::error_code fsec;
            std::string err;
            bool env_loaded = false;
            if (fs::is_directory(env_path, fsec)) {
                env_loaded = lumen::render::load_cubemap_from_face_files(
                    ctx, resolved, ctx.graphics_queue(), cmdPool,
                    env_sampler_cfg, env_cubemap, &err);
            } else if (fs::is_regular_file(env_path, fsec)) {
                std::string ext = env_path.extension().string();
                for (char &c : ext) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".hdr") {
                    env_loaded = lumen::render::
                        load_cubemap_from_hdr_equirectangular_file(
                            ctx, resolved, ctx.graphics_queue(), cmdPool,
                            env_sampler_cfg, env_cubemap, 0, &err);
                } else {
                    err = "环境路径须为六面图目录或单张等距柱状 .hdr 文件";
                }
            } else {
                err = "环境路径不存在或无效: " + resolved;
            }
            if (!env_loaded) {
                LUMEN_APP_LOG_WARN("环境加载失败，回退程序化: {}", err);
                scene_env.cubemap_directory.clear();
                std::array<std::vector<std::uint8_t>, 6> sky_face_pixels2 {};
                constexpr std::uint32_t kEnvFaceSize2 { 256 };
                demo3d::pbr::fill_procedural_sky_faces(kEnvFaceSize2,
                                                       sky_face_pixels2);
                const void *ptrs2[6] = {
                    sky_face_pixels2[0].data(), sky_face_pixels2[1].data(),
                    sky_face_pixels2[2].data(), sky_face_pixels2[3].data(),
                    sky_face_pixels2[4].data(), sky_face_pixels2[5].data()
                };
                if (!env_cubemap.create_cubemap_from_rgba8_faces(
                        ctx, ptrs2, kEnvFaceSize2, ctx.graphics_queue(),
                        cmdPool, env_sampler_cfg)) {
                    LUMEN_APP_LOG_ERROR("程序化天空立方体重建失败");
                    return;
                }
            }
        }
        for (uint32_t j = 0; j < kMaxFramesInFlight; ++j) {
            lumen::render::write_descriptor_image(
                ctx.device(), descriptorSetsScene[j], 1, env_cubemap.view(),
                env_cubemap.sampler());
        }
    };

    lumen::render::PipelineLayout pipelineLayout;
    VkPushConstantRange pushRange {};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);
    pipelineLayout.create(
        ctx, { sceneDescLayout.handle(), materialDescLayout.handle() },
        { pushRange });

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
    pipeConfig.frontFace =
        VK_FRONT_FACE_COUNTER_CLOCKWISE; // Blender OBJ 外表面为 CCW

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(),
                         sceneTarget.render_pass(), 0, pipeConfig)) {
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipe_config_nocull = pipeConfig;
    pipe_config_nocull.cullMode = VK_CULL_MODE_NONE;
    lumen::render::GraphicsPipeline pipeline_nocull;
    if (!pipeline_nocull.create(ctx, pipelineLayout.handle(),
                                sceneTarget.render_pass(), 0,
                                pipe_config_nocull)) {
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipe_config_blend = pipeConfig;
    pipe_config_blend.cullMode = VK_CULL_MODE_NONE;
    pipe_config_blend.alphaBlend = true;
    pipe_config_blend.depthWrite = false;
    lumen::render::GraphicsPipeline pipeline_blend;
    if (!pipeline_blend.create(ctx, pipelineLayout.handle(),
                               sceneTarget.render_pass(), 0,
                               pipe_config_blend)) {
        return -1;
    }

    lumen::render::GraphicsPipelineConfig sky_pipe_config {};
    sky_pipe_config.stages.push_back(
        { sky_vert_shader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    sky_pipe_config.stages.push_back(
        { sky_frag_shader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    sky_pipe_config.vertexBindings.push_back(
        { 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX });
    sky_pipe_config.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 });
    sky_pipe_config.depthTest = true;
    sky_pipe_config.depthWrite = false;
    sky_pipe_config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sky_pipe_config.cullMode = VK_CULL_MODE_FRONT_BIT;
    sky_pipe_config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline sky_pipeline;
    if (!sky_pipeline.create(ctx, pipelineLayout.handle(),
                             sceneTarget.render_pass(), 0, sky_pipe_config)) {
        LUMEN_APP_LOG_ERROR("天空盒管线创建失败");
        return -1;
    }

    lumen::render::PipelineLayout grid_pipeline_layout;
    VkPushConstantRange grid_push {};
    grid_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    grid_push.offset = 0;
    grid_push.size = sizeof(glm::mat4);
    grid_pipeline_layout.create(ctx, {}, { grid_push });

    lumen::render::GraphicsPipelineConfig grid_pipe_config {};
    grid_pipe_config.stages.push_back(
        { grid_vert_shader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    grid_pipe_config.stages.push_back(
        { grid_frag_shader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    grid_pipe_config.vertexBindings.push_back(
        { 0, sizeof(GroundGridVertex), VK_VERTEX_INPUT_RATE_VERTEX });
    grid_pipe_config.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,
          offsetof(GroundGridVertex, position) });
    grid_pipe_config.vertexAttributes.push_back(
        { 1, 0, VK_FORMAT_R32_SFLOAT, offsetof(GroundGridVertex, kind) });
    grid_pipe_config.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    grid_pipe_config.depthTest = true;
    grid_pipe_config.depthWrite = true;
    grid_pipe_config.depthCompareOp = VK_COMPARE_OP_LESS;
    grid_pipe_config.cullMode = VK_CULL_MODE_NONE;
    grid_pipe_config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline grid_pipeline;
    if (!grid_pipeline.create(ctx, grid_pipeline_layout.handle(),
                              sceneTarget.render_pass(), 0, grid_pipe_config)) {
        LUMEN_APP_LOG_ERROR("地面网格管线创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig wireframeConfig = pipeConfig;
    wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;
    wireframeConfig.cullMode = VK_CULL_MODE_NONE;
    lumen::render::GraphicsPipeline wireframePipeline;
    if (!wireframePipeline.create(ctx, pipelineLayout.handle(),
                                  sceneTarget.render_pass(), 0,
                                  wireframeConfig)) {
        return -1;
    }

    lumen::ui::LightViewportGizmos scene_light_gizmos;
    {
        std::string spv_icon_v =
            lumen::core::get_resource_path("shaders/light_icon.vert.spv");
        std::string spv_icon_f =
            lumen::core::get_resource_path("shaders/light_icon.frag.spv");
        std::string spv_dbg_v =
            lumen::core::get_resource_path("shaders/light_debug.vert.spv");
        std::string spv_dbg_f =
            lumen::core::get_resource_path("shaders/light_debug.frag.spv");
        std::string png_dir =
            lumen::core::get_resource_path("assets/icons/SkyLight.png");
        std::string png_point =
            lumen::core::get_resource_path("assets/icons/PointLight.png");
        std::string png_spot =
            lumen::core::get_resource_path("assets/icons/SpotLight.png");
        lumen::ui::LightViewportGizmosCreateInfo lg {};
        lg.ctx = &ctx;
        lg.scene_render_pass = sceneTarget.render_pass();
        lg.subpass_index = 0;
        lg.cmd_pool = &cmdPool;
        lg.graphics_queue = ctx.graphics_queue();
        lg.max_frames_in_flight = kMaxFramesInFlight;
        lg.icon_half_extent = kLightIconHalfExtent;
        lg.spirv_light_icon_vert = spv_icon_v.c_str();
        lg.spirv_light_icon_frag = spv_icon_f.c_str();
        lg.spirv_light_debug_vert = spv_dbg_v.c_str();
        lg.spirv_light_debug_frag = spv_dbg_f.c_str();
        lg.png_directional_icon = png_dir.c_str();
        lg.png_point_icon = png_point.c_str();
        lg.png_spot_icon = png_spot.c_str();
        if (!scene_light_gizmos.create(lg)) {
            LUMEN_APP_LOG_WARN(
                "LightViewportGizmos 创建失败，视口内光源可视化将不可用");
        }
    }

    auto cmdBuffers = cmdPool.allocate(kMaxFramesInFlight);
    lumen::render::FrameSync frameSync;
    frameSync.create(ctx.device(), swapchain.image_count(), kMaxFramesInFlight);

    lumen::platform::EventPump pump;
    uint32_t renderMode { 0 };
    lumen::scene::SceneOrbitCamera scene_cam;
    lumen::scene::SceneCameraController scene_cam_ctrl;
    glm::mat4 scene_view { 1.0f };
    glm::mat4 scene_proj { 1.0f };
    SceneGizmoTool scene_gizmo_tool { SceneGizmoTool::Rotate };
    bool gizmo_world_mode { false };
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };
    uint32_t currentFrame { 0 };
    bool running { true };
    float dt { 0.016f };
    uint32_t nextSceneW { 0 };
    uint32_t nextSceneH { 0 };
    lumen::ui::TextureViewRect
        sceneRect {}; // Scene Image 屏幕坐标，供射线拾取等使用
    uint32_t nextWireframeW { 0 };
    uint32_t nextWireframeH { 0 };
    uint32_t nextNormalW { 0 };
    uint32_t nextNormalH { 0 };
    uint32_t nextDepthW { 0 };
    uint32_t nextDepthH { 0 };
    glm::vec4 clearColor { 0.1f, 0.12f, 0.18f, 1.0f };
    glm::vec4 modelColor { 1.0f, 1.0f, 1.0f, 1.0f };
    bool show_viewport_debug { false };
    bool show_light_debug_gizmo { true };

    // ImGui 后端
    lumen::ui::ImGuiBackendInitInfo imguiInfo;
    imguiInfo.ctx = &ctx;
    imguiInfo.swapchain = &swapchain;
    imguiInfo.renderPass = renderPass.handle();
    imguiInfo.window = window.sdl_window();
    // 思源黑体：简体 Bold 为主，日文 Bold 合并（中文合并先于日文）
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

    lumen::scene::Scene ecs_scene;
    lumen::ui::EditorSelection editor_selection;
    const ::entt::entity ecs_model_entity = ecs_scene.create_entity("Model");
    ecs_scene.registry().emplace<lumen::scene::DrawableTag>(ecs_model_entity);
    {
        auto &matc =
            ecs_scene.registry().emplace<lumen::scene::MaterialComponent>(
                ecs_model_entity);
        if (loaded_from_gltf) {
            matc = gltf_material;
        } else {
            matc.albedo_path = "assets/textures/ikun2026_happy_new_year.jpg";
            matc.metallic_factor = 0.0f;
            matc.roughness_factor = 0.42f;
            matc.ao_factor = 1.0f;
        }
    }
    {
        auto &reg = ecs_scene.registry();
        auto add_dir = [&](const char *name, glm::vec3 dir, float intensity) {
            const ::entt::entity le = ecs_scene.create_entity(name);
            auto &L = reg.emplace<lumen::scene::LightComponent>(le);
            L.type = lumen::scene::LightType::Directional;
            L.local_direction = dir;
            L.intensity = intensity;
            L.color = glm::vec3(1.0f);
        };
        add_dir("Dir Key", { 0.0f, 0.5f, -1.0f }, 1.2f);
        add_dir("Dir Fill", { -0.6f, 0.5f, -0.6f }, 0.7f);
        add_dir("Dir Rim", { 0.5f, 0.3f, -0.8f }, 0.6f);
        add_dir("Dir Bounce", { 0.0f, -0.5f, -0.9f }, 0.5f);
        {
            const ::entt::entity pe = ecs_scene.create_entity("Point Warm");
            auto &L = reg.emplace<lumen::scene::LightComponent>(pe);
            L.type = lumen::scene::LightType::Point;
            L.color = glm::vec3(1.0f, 0.92f, 0.82f);
            L.intensity = 2.2f;
            L.range = 14.0f;
            auto &tr = reg.get<lumen::scene::TransformComponent>(pe);
            tr.matrix =
                glm::translate(glm::mat4(1.0f), glm::vec3(2.2f, 2.8f, 1.5f));
        }
        {
            const ::entt::entity se = ecs_scene.create_entity("Spot Demo");
            auto &L = reg.emplace<lumen::scene::LightComponent>(se);
            L.type = lumen::scene::LightType::Spot;
            L.color = glm::vec3(0.75f, 0.9f, 1.0f);
            L.intensity = 3.0f;
            L.range = 18.0f;
            L.local_direction = glm::vec3(0.0f, -0.35f, -1.0f);
            auto &tr = reg.get<lumen::scene::TransformComponent>(se);
            tr.matrix =
                glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 3.2f, 2.0f));
        }
    }
    editor_selection.entity = ecs_model_entity;

    auto reload_material_textures_gpu = [&]() {
        ctx.wait_idle();
        mat_tex_albedo = lumen::render::Texture {};
        mat_tex_normal = lumen::render::Texture {};
        mat_tex_mr = lumen::render::Texture {};
        mat_tex_ao = lumen::render::Texture {};
        mat_tex_emissive = lumen::render::Texture {};
        gltf_tex_cache.clear();
        auto &reg = ecs_scene.registry();
        const ::entt::entity draw = ecs_scene.primary_drawable();
        if (draw == ::entt::null || !reg.valid(draw) ||
            !reg.all_of<lumen::scene::MaterialComponent>(draw)) {
            if (!mat_tex_albedo.create_from_file(
                    ctx, kDefaultAlbedoPath.c_str(), ctx.graphics_queue(),
                    cmdPool)) {
                constexpr uint32_t kTexSize = 64;
                std::vector<uint8_t> px(kTexSize * kTexSize * 4, 200);
                mat_tex_albedo.create_from_memory(
                    ctx, px.data(), px.size(), kTexSize, kTexSize,
                    ctx.graphics_queue(), cmdPool);
            }
        } else {
            const auto &mc = reg.get<lumen::scene::MaterialComponent>(draw);
            auto try_tex = [&](lumen::render::Texture &t,
                               const std::string &p) -> bool {
                if (p.empty()) {
                    return false;
                }
                const std::string rp = resolve_asset_path(p);
                if (path_ends_with_ci(rp, ".ktx") ||
                    path_ends_with_ci(rp, ".ktx2")) {
                    return t.create_from_ktx_file(ctx, rp.c_str(),
                                                  ctx.graphics_queue(), cmdPool);
                }
                return t.create_from_file(ctx, rp.c_str(), ctx.graphics_queue(),
                                          cmdPool);
            };
            if (!gltf_materials.empty()) {
                auto ensure_cached = [&](const std::string &rel) {
                    if (rel.empty()) {
                        return;
                    }
                    if (gltf_tex_cache.find(rel) != gltf_tex_cache.end()) {
                        return;
                    }
                    const std::string rp = resolve_asset_path(rel);
                    lumen::render::Texture tex;
                    bool ok = false;
                    if (path_ends_with_ci(rp, ".ktx") ||
                        path_ends_with_ci(rp, ".ktx2")) {
                        ok = tex.create_from_ktx_file(ctx, rp.c_str(),
                                                      ctx.graphics_queue(),
                                                      cmdPool);
                    } else {
                        ok = tex.create_from_file(ctx, rp.c_str(),
                                                  ctx.graphics_queue(), cmdPool);
                    }
                    if (ok) {
                        gltf_tex_cache.emplace(rel, std::move(tex));
                    }
                };
                for (const auto &m : gltf_materials) {
                    ensure_cached(m.albedo_path);
                    ensure_cached(m.normal_path);
                    ensure_cached(m.metallic_roughness_path);
                    ensure_cached(m.ao_path);
                    ensure_cached(m.emissive_path);
                }
            }
            if (!mc.albedo_path.empty()) {
                const std::string alb = resolve_asset_path(mc.albedo_path);
                bool albedo_ok = false;
                if (path_ends_with_ci(alb, ".ktx") ||
                    path_ends_with_ci(alb, ".ktx2")) {
                    albedo_ok = mat_tex_albedo.create_from_ktx_file(
                        ctx, alb.c_str(), ctx.graphics_queue(), cmdPool);
                } else {
                    albedo_ok = mat_tex_albedo.create_from_file(
                        ctx, alb.c_str(), ctx.graphics_queue(), cmdPool);
                }
                if (!albedo_ok) {
                    LUMEN_APP_LOG_WARN("反照率加载失败: {}", alb);
                    mat_tex_albedo = lumen::render::Texture {};
                }
            }
            (void)try_tex(mat_tex_normal, mc.normal_path);
            (void)try_tex(mat_tex_mr, mc.metallic_roughness_path);
            (void)try_tex(mat_tex_ao, mc.ao_path);
            (void)try_tex(mat_tex_emissive, mc.emissive_path);
        }
        for (uint32_t j = 0; j < kMaxFramesInFlight; ++j) {
            write_material_descriptor_images(
                ctx.device(), descriptorSetsMaterial[j], placeholder_textures,
                mat_tex_albedo.is_valid() ? &mat_tex_albedo : nullptr,
                mat_tex_normal.is_valid() ? &mat_tex_normal : nullptr,
                mat_tex_mr.is_valid() ? &mat_tex_mr : nullptr,
                mat_tex_ao.is_valid() ? &mat_tex_ao : nullptr,
                mat_tex_emissive.is_valid() ? &mat_tex_emissive : nullptr);
        }
    };
    reload_material_textures_gpu();

    lumen::scene::frame_orbit_on_drawable(scene_cam, ecs_scene.registry(),
                                          ecs_model_entity, mesh_center_local,
                                          mesh_half_extents_local);

    lumen::ui::PanelManager ui_panels;
    ui_panels.add(std::make_unique<lumen::ui::LogPanel>());
    ui_panels.add(std::make_unique<lumen::ui::GpuCapabilitiesPanel>(ctx));
    ui_panels.add(std::make_unique<lumen::ui::SceneHierarchyPanel>(
        &ecs_scene, &editor_selection));
    ui_panels.add(std::make_unique<lumen::ui::SceneInspectorPanel>(
        &ecs_scene, &editor_selection));
    ui_panels.add(std::make_unique<lumen::ui::EnvironmentPanel>(
        &scene_env, [&]() { reload_environment_gpu(); },
        [&]() { reload_environment_gpu(); }));

    auto sceneTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), sceneTarget.color_view(),
            sceneTarget.color_sample_layout()));
    auto wireframeTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), wireframeTarget.color_view(),
            wireframeTarget.color_sample_layout()));
    auto normalTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), normalTarget.color_view(),
            normalTarget.color_sample_layout()));
    auto depthTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), depthTarget.color_view(),
            depthTarget.color_sample_layout()));

    // RenderGraph：声明 reads/writes，自动排序与同步
    lumen::render::RGImage rgColor =
        lumen::render::RGImage::from_texture(sceneTarget.color_image(), false);
    lumen::render::RGImage rgDepth =
        lumen::render::RGImage::from_texture(sceneTarget.depth_image(), true);
    lumen::render::RGImage rgWireframeColor =
        lumen::render::RGImage::from_texture(wireframeTarget.color_image(),
                                             false);
    lumen::render::RGImage rgWireframeDepth =
        lumen::render::RGImage::from_texture(wireframeTarget.depth_image(),
                                             true);
    lumen::render::RGImage rgNormalColor =
        lumen::render::RGImage::from_texture(normalTarget.color_image(), false);
    lumen::render::RGImage rgNormalDepth =
        lumen::render::RGImage::from_texture(normalTarget.depth_image(), true);
    lumen::render::RGImage rgDepthColor =
        lumen::render::RGImage::from_texture(depthTarget.color_image(), false);
    lumen::render::RGImage rgDepthDepth =
        lumen::render::RGImage::from_texture(depthTarget.depth_image(), true);
    lumen::render::RGImage rgSwapchain =
        lumen::render::RGImage::from_swapchain(swapchain);

    lumen::render::RenderGraph renderGraph(&ctx);
    renderGraph.add_pass(lumen::render::RGPass {
        .name = "Scene",
        .reads = {},
        .writes = { &rgColor, &rgDepth },
        .execute =
            [&](VkCommandBuffer cmd, uint32_t /*swapchainImageIndex*/) {
                const VkExtent2D sceneExtent = sceneTarget.extent();
                VkRenderPassBeginInfo sceneRpBegin {
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                };
                sceneRpBegin.renderPass = sceneTarget.render_pass();
                sceneRpBegin.framebuffer = sceneTarget.framebuffer();
                sceneRpBegin.renderArea = { { 0, 0 }, sceneExtent };
                VkClearValue sceneClearValues[2];
                sceneClearValues[0].color = { { clearColor.r, clearColor.g,
                                                clearColor.b, clearColor.a } };
                sceneClearValues[1].depthStencil = { 1.0f, 0 };
                sceneRpBegin.clearValueCount = 2;
                sceneRpBegin.pClearValues = sceneClearValues;
                vkCmdBeginRenderPass(cmd, &sceneRpBegin,
                                     VK_SUBPASS_CONTENTS_INLINE);

                VkViewport sceneViewport {};
                sceneViewport.width = static_cast<float>(sceneExtent.width);
                sceneViewport.height = static_cast<float>(sceneExtent.height);
                sceneViewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &sceneViewport);
                VkRect2D sceneScissor { { 0, 0 }, sceneExtent };
                vkCmdSetScissor(cmd, 0, 1, &sceneScissor);

                VkPipeline mesh_fill_pipeline = pipeline.handle();
                {
                    const ::entt::registry &reg = ecs_scene.registry();
                    const ::entt::entity dr = ecs_scene.primary_drawable();
                    if (dr != ::entt::null && reg.valid(dr) &&
                        reg.all_of<lumen::scene::MaterialComponent>(dr)) {
                        const auto &mc =
                            reg.get<lumen::scene::MaterialComponent>(dr);
                        if (mc.alpha_mode ==
                            lumen::scene::MaterialAlphaMode::Blend) {
                            mesh_fill_pipeline = pipeline_blend.handle();
                        } else if (mc.double_sided) {
                            mesh_fill_pipeline = pipeline_nocull.handle();
                        }
                    }
                }
                VkPipeline activePipeline =
                    (renderMode == 1u) ? wireframePipeline.handle()
                                       : mesh_fill_pipeline;
                PushConstants pushData {};
                pushData.mode = renderMode;
                pushData.modelColor = modelColor;
                if (renderMode == 0u || renderMode == 1u) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      sky_pipeline.handle());
                    VkDescriptorSet sky_ds = descriptorSetsScene[currentFrame];
                    vkCmdBindDescriptorSets(
                        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipelineLayout.handle(), 0, 1, &sky_ds, 0, nullptr);
                    VkBuffer sky_vb = sky_vertex_buffer.handle();
                    VkDeviceSize sky_off { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, &sky_vb, &sky_off);
                    vkCmdBindIndexBuffer(cmd, sky_index_buffer.handle(), 0,
                                         sky_index_buffer.vk_index_type());
                    vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
                }
                {
                    const glm::mat4 grid_vp = scene_proj * scene_view;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      grid_pipeline.handle());
                    vkCmdPushConstants(cmd, grid_pipeline_layout.handle(),
                                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                                       sizeof(glm::mat4), &grid_vp);
                    VkBuffer grid_vb = grid_vertex_buffer.handle();
                    VkDeviceSize grid_off { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, &grid_vb, &grid_off);
                    vkCmdDraw(cmd, grid_vertex_count, 1, 0, 0);
                }
                vkCmdPushConstants(cmd, pipelineLayout.handle(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pushData);
                VkBuffer vb = vertexBuffer.handle();
                VkDeviceSize vbOffset { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0,
                                     indexBuffer.vk_index_type());

                auto tex_from_cache =
                    [&](const std::string &rel)
                    -> const lumen::render::Texture * {
                        if (rel.empty()) {
                            return nullptr;
                        }
                        auto it = gltf_tex_cache.find(rel);
                        if (it == gltf_tex_cache.end() ||
                            !it->second.is_valid()) {
                            return nullptr;
                        }
                        return &it->second;
                    };

                if (use_gltf_multimat) {
                    for (const auto &sm : gltf_submeshes) {
                        const lumen::scene::MaterialComponent &sm_mc =
                            (sm.material_index >= 0 &&
                             sm.material_index <
                                 static_cast<int>(gltf_materials.size()))
                                ? gltf_materials[static_cast<size_t>(
                                      sm.material_index)]
                                : kGltfMissingMaterial;

                        VkPipeline sub_pipe = activePipeline;
                        if (renderMode != 1u) {
                            sub_pipe = pipeline.handle();
                            if (sm_mc.alpha_mode ==
                                lumen::scene::MaterialAlphaMode::Blend) {
                                sub_pipe = pipeline_blend.handle();
                            } else if (sm_mc.double_sided) {
                                sub_pipe = pipeline_nocull.handle();
                            }
                        }
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          sub_pipe);

                        lumen::render::PbrMaterialUbo sub_mat_ubo {};
                        sub_mat_ubo.base_color_factor = sm_mc.base_color_factor;
                        sub_mat_ubo.mr_ao_factors = glm::vec4(
                            sm_mc.metallic_factor, sm_mc.roughness_factor,
                            sm_mc.ao_factor, 0.0f);
                        sub_mat_ubo.emissive_factor =
                            glm::vec4(sm_mc.emissive_factor.x,
                                      sm_mc.emissive_factor.y,
                                      sm_mc.emissive_factor.z, 0.0f);
                        const std::uint32_t sub_tex_mask =
                            lumen::render::material_texture_mask_from_component(
                                sm_mc);
                        sub_mat_ubo.shader_params = glm::vec4(
                            static_cast<float>(
                                static_cast<unsigned>(sm_mc.alpha_mode)),
                            sm_mc.alpha_cutoff,
                            lumen::render::uint_bits_to_float(sub_tex_mask),
                            0.0f);
                        materialUniformBuffers[currentFrame].update(sub_mat_ubo);

                        write_material_descriptor_images(
                            ctx.device(),
                            descriptorSetsMaterial[currentFrame],
                            placeholder_textures,
                            tex_from_cache(sm_mc.albedo_path),
                            tex_from_cache(sm_mc.normal_path),
                            tex_from_cache(sm_mc.metallic_roughness_path),
                            tex_from_cache(sm_mc.ao_path),
                            tex_from_cache(sm_mc.emissive_path));

                        VkDescriptorSet mesh_sets_multi[2] = {
                            descriptorSetsScene[currentFrame],
                            descriptorSetsMaterial[currentFrame]};
                        vkCmdBindDescriptorSets(
                            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout.handle(), 0, 2, mesh_sets_multi,
                            0, nullptr);

                        vkCmdDrawIndexed(cmd, sm.index_count, 1,
                                         sm.first_index, 0, 0);
                    }
                } else {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      activePipeline);
                    VkDescriptorSet mesh_sets[2] = {
                        descriptorSetsScene[currentFrame],
                        descriptorSetsMaterial[currentFrame]};
                    vkCmdBindDescriptorSets(
                        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout.handle(),
                        0, 2, mesh_sets, 0, nullptr);
                    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
                }

                if (scene_light_gizmos.icons_ready() ||
                    scene_light_gizmos.debug_ready()) {
                    scene_light_gizmos.record(cmd, currentFrame, scene_view,
                                              scene_proj, ecs_scene.registry());
                }

                vkCmdEndRenderPass(cmd);
            },
    });

    auto addAuxPass = [&](const char *name, lumen::render::RGImage *outColor,
                          lumen::render::RGImage *outDepth,
                          lumen::render::OffscreenRenderTarget &target,
                          VkPipeline pipelineHandle, uint32_t mode) {
        renderGraph.add_pass(lumen::render::RGPass {
            .name = name,
            .reads = {},
            .writes = { outColor, outDepth },
            .execute =
                [&, pipelineHandle, mode](VkCommandBuffer cmd,
                                          uint32_t /*swapchainImageIndex*/) {
                    const VkExtent2D ext = target.extent();
                    if (ext.width == 0 || ext.height == 0)
                        return;
                    VkRenderPassBeginInfo rpBegin {
                        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                    };
                    rpBegin.renderPass = target.render_pass();
                    rpBegin.framebuffer = target.framebuffer();
                    rpBegin.renderArea = { { 0, 0 }, ext };
                    VkClearValue clearVals[2];
                    clearVals[0].color = { { clearColor.r, clearColor.g,
                                             clearColor.b, clearColor.a } };
                    clearVals[1].depthStencil = { 1.0f, 0 };
                    rpBegin.clearValueCount = 2;
                    rpBegin.pClearValues = clearVals;
                    vkCmdBeginRenderPass(cmd, &rpBegin,
                                         VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp {};
                    vp.width = static_cast<float>(ext.width);
                    vp.height = static_cast<float>(ext.height);
                    vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor { { 0, 0 }, ext };
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipelineHandle);
                    PushConstants pc {};
                    pc.mode = mode;
                    pc.modelColor = modelColor;
                    vkCmdPushConstants(cmd, pipelineLayout.handle(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(PushConstants), &pc);
                    VkBuffer vb = vertexBuffer.handle();
                    VkDeviceSize vbOff { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOff);
                    vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0,
                                         indexBuffer.vk_index_type());

                    auto tex_from_cache_aux =
                        [&](const std::string &rel)
                        -> const lumen::render::Texture * {
                            if (rel.empty()) {
                                return nullptr;
                            }
                            auto it = gltf_tex_cache.find(rel);
                            if (it == gltf_tex_cache.end() ||
                                !it->second.is_valid()) {
                                return nullptr;
                            }
                            return &it->second;
                        };

                    if (use_gltf_multimat) {
                        for (const auto &sm : gltf_submeshes) {
                            const lumen::scene::MaterialComponent &sm_mc =
                                (sm.material_index >= 0 &&
                                 sm.material_index <
                                     static_cast<int>(gltf_materials.size()))
                                    ? gltf_materials[static_cast<size_t>(
                                          sm.material_index)]
                                    : kGltfMissingMaterial;

                            lumen::render::PbrMaterialUbo sub_mat_ubo {};
                            sub_mat_ubo.base_color_factor =
                                sm_mc.base_color_factor;
                            sub_mat_ubo.mr_ao_factors = glm::vec4(
                                sm_mc.metallic_factor, sm_mc.roughness_factor,
                                sm_mc.ao_factor, 0.0f);
                            sub_mat_ubo.emissive_factor = glm::vec4(
                                sm_mc.emissive_factor.x,
                                sm_mc.emissive_factor.y,
                                sm_mc.emissive_factor.z, 0.0f);
                            const std::uint32_t sub_tex_mask =
                                lumen::render::material_texture_mask_from_component(
                                    sm_mc);
                            sub_mat_ubo.shader_params = glm::vec4(
                                static_cast<float>(static_cast<unsigned>(
                                    sm_mc.alpha_mode)),
                                sm_mc.alpha_cutoff,
                                lumen::render::uint_bits_to_float(sub_tex_mask),
                                0.0f);
                            materialUniformBuffers[currentFrame].update(
                                sub_mat_ubo);

                            write_material_descriptor_images(
                                ctx.device(),
                                descriptorSetsMaterial[currentFrame],
                                placeholder_textures,
                                tex_from_cache_aux(sm_mc.albedo_path),
                                tex_from_cache_aux(sm_mc.normal_path),
                                tex_from_cache_aux(
                                    sm_mc.metallic_roughness_path),
                                tex_from_cache_aux(sm_mc.ao_path),
                                tex_from_cache_aux(sm_mc.emissive_path));

                            VkDescriptorSet aux_sets_loop[2] = {
                                descriptorSetsScene[currentFrame],
                                descriptorSetsMaterial[currentFrame]};
                            vkCmdBindDescriptorSets(
                                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.handle(), 0, 2,
                                aux_sets_loop, 0, nullptr);

                            vkCmdDrawIndexed(cmd, sm.index_count, 1,
                                             sm.first_index, 0, 0);
                        }
                    } else {
                        VkDescriptorSet aux_sets[2] = {
                            descriptorSetsScene[currentFrame],
                            descriptorSetsMaterial[currentFrame]};
                        vkCmdBindDescriptorSets(
                            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout.handle(), 0, 2, aux_sets, 0,
                            nullptr);
                        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
                    }
                    vkCmdEndRenderPass(cmd);
                },
        });
    };

    addAuxPass("Wireframe", &rgWireframeColor, &rgWireframeDepth,
               wireframeTarget, wireframePipeline.handle(), 1);
    addAuxPass("Normal", &rgNormalColor, &rgNormalDepth, normalTarget,
               pipeline.handle(), 2);
    addAuxPass("Depth", &rgDepthColor, &rgDepthDepth, depthTarget,
               pipeline.handle(), 3);

    renderGraph.add_pass(lumen::render::RGPass {
        .name = "UI",
        .reads = { &rgColor },
        .writes = { &rgSwapchain },
        .execute =
            [&](VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
                VkRenderPassBeginInfo rpBegin {
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                };
                rpBegin.renderPass = renderPass.handle();
                rpBegin.framebuffer = framebuffers.get(swapchainImageIndex);
                rpBegin.renderArea = { { 0, 0 }, swapchain.extent() };
                VkClearValue clearValues[2];
                clearValues[0].color = { { clearColor.r, clearColor.g,
                                           clearColor.b, clearColor.a } };
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

                ImGuiID dockspaceId =
                    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                ui_panels.set_default_dock_id(dockspaceId);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Scene", sceneTextureId, &nextSceneW, &nextSceneH,
                    &sceneRect, ImVec2(0, 0), ImVec2(1, 1),
                    [&](const lumen::ui::TextureViewRect &r) {
                        if (scene_gizmo_tool == SceneGizmoTool::View) {
                            return;
                        }
                        ::entt::registry &reg = ecs_scene.registry();
                        const ::entt::entity ge = editor_selection.entity;
                        if (!reg.valid(ge) ||
                            !reg.all_of<lumen::scene::TransformComponent>(ge)) {
                            return;
                        }
                        glm::mat4 world = lumen::scene::world_matrix(reg, ge);
                        lumen::ui::imguizmo_manipulate(
                            r, scene_view, scene_proj, &world,
                            scene_gizmo_to_operation(scene_gizmo_tool),
                            gizmo_world_mode ? ImGuizmo::WORLD
                                             : ImGuizmo::LOCAL);
                        if (lumen::ui::imguizmo_is_using()) {
                            auto &local =
                                reg.get<lumen::scene::TransformComponent>(ge);
                            if (const auto *p =
                                    reg.try_get<lumen::scene::ParentComponent>(
                                        ge);
                                p && p->parent != ::entt::null &&
                                reg.valid(p->parent)) {
                                const glm::mat4 pw =
                                    lumen::scene::world_matrix(reg, p->parent);
                                local.matrix = glm::inverse(pw) * world;
                            } else {
                                local.matrix = world;
                            }
                        }
                    });
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Wireframe", wireframeTextureId, &nextWireframeW,
                    &nextWireframeH);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel("Normal", normalTextureId,
                                                    &nextNormalW, &nextNormalH);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel("Depth", depthTextureId,
                                                    &nextDepthW, &nextDepthH);

                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(280, 0),
                                         ImGuiCond_FirstUseEver);
                ImGui::Begin("Demo3D");
                ImGui::Text("Render Mode");
                int mode = static_cast<int>(renderMode);
                ImGui::RadioButton("Lit", &mode, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Wireframe", &mode, 1);
                ImGui::RadioButton("Normal", &mode, 2);
                ImGui::SameLine();
                ImGui::RadioButton("Depth", &mode, 3);
                renderMode = static_cast<uint32_t>(mode);
                ImGui::ColorEdit4("Clear Color", glm::value_ptr(clearColor));
                ImGui::ColorEdit4("Model Color", glm::value_ptr(modelColor));
                ImGui::Separator();
                ImGui::TextDisabled("PBR / IBL：见 Inspector（Material）与 "
                                    "Environment 面板。");
                ImGui::Separator();
                {
                    float r = scene_cam.radius();
                    const auto lim = scene_cam.limits();
                    if (ImGui::SliderFloat("Camera Distance", &r,
                                           lim.min_radius, lim.max_radius,
                                           "%.1f")) {
                        scene_cam.set_radius(r);
                    }
                }
                ImGui::TextDisabled(
                    "Scene：[F] 枢轴对准选中网格中心 | Alt+左键 环绕 | "
                    "Alt+中键 平移 | Alt+右键/滚轮 缩放 | "
                    "右键+移动 环视，右键+WASD 平移、E/Q 升降");
                ImGui::Text("Gizmo (Unity: Q/W/E/R)");
                if (ImGui::RadioButton("View (Q)", scene_gizmo_tool ==
                                                       SceneGizmoTool::View)) {
                    scene_gizmo_tool = SceneGizmoTool::View;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Move (W)", scene_gizmo_tool ==
                                                       SceneGizmoTool::Move)) {
                    scene_gizmo_tool = SceneGizmoTool::Move;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate (E)",
                                       scene_gizmo_tool ==
                                           SceneGizmoTool::Rotate)) {
                    scene_gizmo_tool = SceneGizmoTool::Rotate;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale (R)",
                                       scene_gizmo_tool ==
                                           SceneGizmoTool::Scale)) {
                    scene_gizmo_tool = SceneGizmoTool::Scale;
                }
                ImGui::Checkbox("World mode", &gizmo_world_mode);
                ImGui::Separator();
                ImGui::TextDisabled(
                    "Transform: use Inspector or Scene Gizmo on selection.");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FPS: %.1f",
                                   1.0f / (dt > 0.0f ? dt : 0.016f));
                ImGui::Checkbox("Light range / direction (selected light only)",
                                &show_light_debug_gizmo);
                ImGui::TextDisabled(
                    "在 Hierarchy 选中带 Light 的实体后显示范围与方向线；"
                    "其它光源仍显示图标。");
                ImGui::Checkbox("Show viewport debug", &show_viewport_debug);
                if (show_viewport_debug) {
                    const auto sceneMouseState =
                        lumen::ui::viewport_mouse_state(sceneRect,
                                                        pump.input().mouse_x(),
                                                        pump.input().mouse_y());
                    lumen::ui::imgui_viewport_mouse_debug(
                        sceneRect, sceneMouseState, "Scene");
                }
                ImGui::TextDisabled(
                    "Scene top-right: orientation cube (ViewManipulate), "
                    "inset to stay inside the viewport.");
                ImGui::End();

                {
                    const ImGuiViewport *mainVp = ImGui::GetMainViewport();
                    if (mainVp) {
                        float cubeX { 0 };
                        float cubeY { 0 };
                        place_view_cube_top_right(sceneRect, *mainVp, cubeX,
                                                  cubeY);
                        lumen::ui::imguizmo_view_manipulate(
                            &scene_view, scene_cam.radius(), cubeX, cubeY,
                            kViewCubeSize, kViewCubeSize,
                            IM_COL32(28, 28, 32, 200));
                        scene_cam.sync_orbit_from_view(scene_view);
                    }
                }

                ui_panels.render_all();

                lumen::ui::imgui_backend_render(cmd);
                vkCmdEndRenderPass(cmd);
            },
    });

    lumen::ui::imgui_setup_event_pump(pump);
    lumen::platform::add_input_debug_handler(
        pump); // 调试：输出鼠标键盘事件到 logs/engine.log

    LUMEN_APP_LOG_INFO(
        "Demo3D 启动 Scene 相机（类 "
        "Unity）：轨道枢轴默认在模型包围盒中心；[F] "
        "对准选中 Drawable 中心。Alt+左键环绕、Alt+中键平移、Alt+右键"
        "或滚轮缩放；右键+移动环视，右键+WASD 平移枢轴、E/Q "
        "升降。模型：Hierarchy + "
        "Inspector / Gizmo（Q/W/E/R） [0]–[3] 渲染模式 [ESC] 退出");

    constexpr float kZoomSpeed { 0.25f };

    pump.on_quit([&] {
        running = false;
        SDL_SetWindowRelativeMouseMode(window.sdl_window(), false);
    });
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
        } else if (e.key == lumen::platform::Key::F) {
            ::entt::registry &reg = ecs_scene.registry();
            ::entt::entity fe = editor_selection.entity;
            if (fe == ::entt::null || !reg.valid(fe) ||
                !reg.all_of<lumen::scene::DrawableTag>(fe)) {
                fe = ecs_scene.primary_drawable();
            }
            lumen::scene::frame_orbit_on_drawable(
                scene_cam, reg, fe, mesh_center_local, mesh_half_extents_local);
        }
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
    });
    pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel &e) {
        const auto sceneHover = lumen::ui::viewport_mouse_state(
            sceneRect, pump.input().mouse_x(), pump.input().mouse_y());
        if (lumen::ui::imgui_wants_mouse() && !sceneHover.inViewport)
            return;
        scene_cam.apply_scroll_zoom(e.deltaY, kZoomSpeed);
    });

    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;

    while (running) {
        if (!pump.poll())
            break;

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            if (fbWidth > 0 && fbHeight > 0) {
                lumen::render::Image newDepth;
                newDepth.create_depth_attachment(
                    ctx, static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight));
                // 必须先 destroy framebuffers 再替换 depthImage，否则旧 depth
                // view 仍被 framebuffer 引用时被销毁会触发
                // VUID-vkDestroyImageView-01026
                lumen::render::recreate_swapchain_resources(
                    ctx, swapchain, framebuffers, frameSync,
                    renderPass.handle(), static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), kMaxFramesInFlight,
                    newDepth.view());
                depthImage = std::move(newDepth);

                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(sceneTextureId));

                sceneTarget.resize(ctx, static_cast<uint32_t>(fbWidth),
                                   static_cast<uint32_t>(fbHeight));
                sceneTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), sceneTarget.color_view(),
                        sceneTarget.color_sample_layout()));

                rgColor = lumen::render::RGImage::from_texture(
                    sceneTarget.color_image(), false);
                rgDepth = lumen::render::RGImage::from_texture(
                    sceneTarget.depth_image(), true);
                rgSwapchain = lumen::render::RGImage::from_swapchain(swapchain);
                nextSceneW = static_cast<uint32_t>(fbWidth);
                nextSceneH = static_cast<uint32_t>(fbHeight);
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

        dt = static_cast<float>(frame_dt.tick_seconds());

        lumen::ui::imgui_backend_new_frame();

        const auto &inp = pump.input();
        const auto sceneNavMouse = lumen::ui::viewport_mouse_state(
            sceneRect, inp.mouse_x(), inp.mouse_y());
        if (scene_gizmo_tool == SceneGizmoTool::View) {
            lumen::ui::imguizmo_reset_interaction_state();
        }
        const bool block_scene_nav_for_gizmo =
            (scene_gizmo_tool != SceneGizmoTool::View) &&
            (lumen::ui::imguizmo_is_using() || lumen::ui::imguizmo_is_over());
        const bool cam_nav_ok =
            sceneNavMouse.inViewport && !block_scene_nav_for_gizmo;
        const bool scene_fly =
            cam_nav_ok &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Right) &&
            !inp.has_alt();

        static bool scene_relative_mouse { false };
        if (scene_fly != scene_relative_mouse) {
            SDL_SetWindowRelativeMouseMode(window.sdl_window(), scene_fly);
            scene_relative_mouse = scene_fly;
        }

        // Q/W/E/R 须在 NewFrame 之后用 ImGui 按键查询；右键飞行时 Q/E
        // 用于升降， 与 Unity 一致不切换工具。
        {
            const ImGuiIO &io = ImGui::GetIO();
            if (!io.WantTextInput && !scene_fly) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                    scene_gizmo_tool = SceneGizmoTool::View;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Move;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Rotate;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Scale;
                }
            }
        }

        // 根据上一帧 UI 计算的显示尺寸调整离屏渲染分辨率（wait_idle
        // 避免多帧并发时资源冲突）
        const bool needSceneResize =
            nextSceneW > 0 && nextSceneH > 0 &&
            (sceneTarget.extent().width != nextSceneW ||
             sceneTarget.extent().height != nextSceneH);
        const bool needWireframeResize =
            nextWireframeW > 0 && nextWireframeH > 0 &&
            (wireframeTarget.extent().width != nextWireframeW ||
             wireframeTarget.extent().height != nextWireframeH);
        const bool needNormalResize =
            nextNormalW > 0 && nextNormalH > 0 &&
            (normalTarget.extent().width != nextNormalW ||
             normalTarget.extent().height != nextNormalH);
        const bool needDepthResize =
            nextDepthW > 0 && nextDepthH > 0 &&
            (depthTarget.extent().width != nextDepthW ||
             depthTarget.extent().height != nextDepthH);
        if (needSceneResize || needWireframeResize || needNormalResize ||
            needDepthResize) {
            ctx.wait_idle();
            if (needSceneResize) {
                sceneTarget.resize(ctx, nextSceneW, nextSceneH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(sceneTextureId));
                sceneTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), sceneTarget.color_view(),
                        sceneTarget.color_sample_layout()));
                rgColor = lumen::render::RGImage::from_texture(
                    sceneTarget.color_image(), false);
                rgDepth = lumen::render::RGImage::from_texture(
                    sceneTarget.depth_image(), true);
            }
            if (needWireframeResize) {
                wireframeTarget.resize(ctx, nextWireframeW, nextWireframeH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(wireframeTextureId));
                wireframeTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), wireframeTarget.color_view(),
                        wireframeTarget.color_sample_layout()));
                rgWireframeColor = lumen::render::RGImage::from_texture(
                    wireframeTarget.color_image(), false);
                rgWireframeDepth = lumen::render::RGImage::from_texture(
                    wireframeTarget.depth_image(), true);
            }
            if (needNormalResize) {
                normalTarget.resize(ctx, nextNormalW, nextNormalH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(normalTextureId));
                normalTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), normalTarget.color_view(),
                        normalTarget.color_sample_layout()));
                rgNormalColor = lumen::render::RGImage::from_texture(
                    normalTarget.color_image(), false);
                rgNormalDepth = lumen::render::RGImage::from_texture(
                    normalTarget.depth_image(), true);
            }
            if (needDepthResize) {
                depthTarget.resize(ctx, nextDepthW, nextDepthH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(depthTextureId));
                depthTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), depthTarget.color_view(),
                        depthTarget.color_sample_layout()));
                rgDepthColor = lumen::render::RGImage::from_texture(
                    depthTarget.color_image(), false);
                rgDepthDepth = lumen::render::RGImage::from_texture(
                    depthTarget.depth_image(), true);
            }
        }

        const bool imgui_blocks_mouse =
            lumen::ui::imgui_wants_mouse() && !sceneNavMouse.inViewport;
        const float mdx = inp.mouse_delta_x();
        const float mdy = inp.mouse_delta_y();

        if (cam_nav_ok && inp.has_alt() &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Left)) {
            scene_cam_ctrl.apply_alt_orbit(scene_cam, mdx, mdy);
        }

        if (cam_nav_ok && inp.has_alt() &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Middle)) {
            scene_cam_ctrl.apply_alt_pan(scene_cam, mdx, mdy);
        }

        if (cam_nav_ok && inp.has_alt() &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Right)) {
            scene_cam_ctrl.apply_alt_zoom_drag(scene_cam, mdy);
        }

        if (!imgui_blocks_mouse && scene_fly) {
            scene_cam_ctrl.apply_rmb_look(scene_cam, mdx, mdy);
            lumen::scene::SceneCameraFlyInput fly {};
            fly.move_forward = inp.is_key_down(lumen::platform::Key::W);
            fly.move_back = inp.is_key_down(lumen::platform::Key::S);
            fly.move_left = inp.is_key_down(lumen::platform::Key::A);
            fly.move_right = inp.is_key_down(lumen::platform::Key::D);
            fly.move_up = inp.is_key_down(lumen::platform::Key::E);
            fly.move_down = inp.is_key_down(lumen::platform::Key::Q);
            fly.fast_modifier = inp.has_shift();
            fly.delta_seconds = dt;
            scene_cam_ctrl.apply_fly_pan(scene_cam, fly);
        }

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX)
            continue;

        // MVP 矩阵（与 Scene 视口 Gizmo 共用 view / proj）
        const glm::vec3 cameraPos = scene_cam.eye_position();
        scene_view = scene_cam.view_matrix();
        const VkExtent2D sceneExtentForProj = sceneTarget.extent();
        const float scene_aspect =
            static_cast<float>(sceneExtentForProj.width) /
            static_cast<float>(sceneExtentForProj.height);
        scene_proj = scene_cam.projection_matrix(scene_aspect);
        glm::mat4 model_matrix { 1.0f };
        const ::entt::entity drawable = ecs_scene.primary_drawable();
        if (drawable != ::entt::null && ecs_scene.registry().valid(drawable)) {
            model_matrix =
                lumen::scene::world_matrix(ecs_scene.registry(), drawable);
        }
        if (editor_selection.material_texture_reload_requested) {
            editor_selection.material_texture_reload_requested = false;
            reload_material_textures_gpu();
        }

        SceneUbo ubo {};
        ubo.model = model_matrix;
        ubo.mvp = scene_proj * scene_view * model_matrix;
        const glm::mat3 nm =
            glm::mat3(glm::transpose(glm::inverse(model_matrix)));
        ubo.normalMatrix = glm::mat4(nm);
        ubo.cameraWorld = glm::vec4(cameraPos, 0.0f);
        std::uint32_t light_count = 0;
        lumen::scene::pack_lights_for_ubo(ecs_scene.registry(), ubo.lights,
                                          light_count);
        ubo.sceneParams =
            glm::vec4(static_cast<float>(light_count), 0.0f, 0.0f, 0.0f);
        const glm::mat4 view_rot = glm::mat4(glm::mat3(scene_view));
        ubo.skyMvp = scene_proj * view_rot;
        ubo.skyOrientInv = glm::inverse(view_rot);
        const float max_mip = static_cast<float>(
            env_cubemap.mip_levels() > 0 ? env_cubemap.mip_levels() - 1 : 0);
        const float diff_mip = std::max(0.0f, max_mip - 3.0f);
        ubo.envParams = glm::vec4(scene_env.exposure, max_mip, diff_mip,
                                  scene_env.ibl_strength);
        uniformBuffers[currentFrame].update(ubo);

        if (!use_gltf_multimat) {
            lumen::render::PbrMaterialUbo matUbo {};
            if (drawable != ::entt::null &&
                ecs_scene.registry().all_of<lumen::scene::MaterialComponent>(
                    drawable)) {
                const auto &mc =
                    ecs_scene.registry().get<lumen::scene::MaterialComponent>(
                        drawable);
                matUbo.base_color_factor = mc.base_color_factor;
                matUbo.mr_ao_factors = glm::vec4(
                    mc.metallic_factor, mc.roughness_factor, mc.ao_factor,
                    0.0f);
                matUbo.emissive_factor =
                    glm::vec4(mc.emissive_factor.x, mc.emissive_factor.y,
                              mc.emissive_factor.z, 0.0f);
                const std::uint32_t tex_mask =
                    lumen::render::material_texture_mask_from_component(mc);
                matUbo.shader_params =
                    glm::vec4(static_cast<float>(static_cast<unsigned>(
                                  mc.alpha_mode)),
                              mc.alpha_cutoff,
                              lumen::render::uint_bits_to_float(tex_mask),
                              0.0f);
            } else {
                matUbo.base_color_factor = glm::vec4(1.0f);
                matUbo.mr_ao_factors = glm::vec4(0.0f, 0.42f, 1.0f, 0.0f);
                matUbo.emissive_factor = glm::vec4(0.0f);
                matUbo.shader_params =
                    glm::vec4(0.0f, 0.5f,
                              lumen::render::uint_bits_to_float(
                                  lumen::render::kMatTexBitAlbedo),
                              0.0f);
            }
            materialUniformBuffers[currentFrame].update(matUbo);
        }

        if (scene_light_gizmos.icons_ready() ||
            scene_light_gizmos.debug_ready()) {
            scene_light_gizmos.prepare_frame(
                ecs_scene.registry(), editor_selection.entity, true,
                show_light_debug_gizmo, currentFrame);
        }

        VkCommandBuffer cmd = cmdBuffers[currentFrame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            continue;
        }

        renderGraph.execute(cmd, imageIndex);

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
