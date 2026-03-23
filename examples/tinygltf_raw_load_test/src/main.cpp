/**
 * @file main.cpp
 * @brief 在 main 中直接用 tinygltf 解析 Sponza，合并网格；分段绘制 + 引擎对齐的 PBR/IBL（IBL 数据由本目标 `ibl_cpu` 在 CPU 生成）。
 *
 * 说明：`TINYGLTF_IMPLEMENTATION` 已在 `engine` 的 `gltf_loader.cpp` 中定义，本文件仅
 * `#include <tiny_gltf.h>` 使用 API，避免重复符号。
 */

#include "engine.hpp"

#include "core/gltf_loader.hpp"
#include "core/obj_loader.hpp"
#include "core/path.hpp"
#include "core/time.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pipeline.hpp"
#include "render/material_texture_mask.hpp"
#include "render/pbr_material_ubo.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"
#include "scene/light.hpp"
#include "scene/scene_orbit_camera.hpp"

#include "ibl_cpu.hpp"

// 与 `engine/src/core/gltf_loader.cpp` 一致，否则 TU 会期待默认的
// `tinygltf::LoadImageData` / `WriteImageData`，而引擎侧实现未编译它们（LNK2019）。
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include <ghc/filesystem.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fs = ghc::filesystem;

using Vertex = lumen::core::ObjVertex;

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };
/// 与 CMake `copy_directory assets` 及仓库内 Sponza 资源布局一致
constexpr const char *kSponzaGltfRel { "assets/model/Sponza/glTF/Sponza.gltf" };

/// 与 `gltf_pbr.vert` / `gltf_pbr.frag` 中 `SceneUBO` 一致（std140）
struct SceneUbo {
    glm::mat4 model { 1.F };
    glm::mat4 mvp { 1.F };
    glm::mat4 normal_matrix { 1.F };
    glm::vec4 camera_world { 0.F, 0.F, 0.F, 0.F };
    lumen::scene::GPULight lights[lumen::scene::kMaxLightsUbo] {};
    glm::vec4 scene_params { 0.F, 0.F, 0.F, 0.F };
    glm::mat4 sky_mvp { 1.F };
    glm::mat4 sky_orient_inv { 1.F };
    glm::vec4 env_params { 1.F, 0.F, 0.F, 1.F };
};

bool ends_with_ci(std::string_view s, std::string_view ext) {
    if (s.size() < ext.size()) {
        return false;
    }
    for (size_t i = 0; i < ext.size(); ++i) {
        const auto a = static_cast<char>(std::tolower(static_cast<unsigned char>(
            s[s.size() - ext.size() + i])));
        const auto b = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

glm::mat4 node_local_matrix(const tinygltf::Node &n) {
    if (n.matrix.size() == 16) {
        glm::dmat4 m(1.0);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                m[c][r] = n.matrix[static_cast<size_t>(c * 4 + r)];
            }
        }
        return glm::mat4(m);
    }
    glm::vec3 t(0.F);
    glm::quat q(1.F, 0.F, 0.F, 0.F);
    glm::vec3 s(1.F);
    if (n.translation.size() >= 3) {
        t = glm::vec3(static_cast<float>(n.translation[0]),
                      static_cast<float>(n.translation[1]),
                      static_cast<float>(n.translation[2]));
    }
    if (n.rotation.size() >= 4) {
        q = glm::quat(static_cast<float>(n.rotation[3]),
                      static_cast<float>(n.rotation[0]),
                      static_cast<float>(n.rotation[1]),
                      static_cast<float>(n.rotation[2]));
    }
    if (n.scale.size() >= 3) {
        s = glm::vec3(static_cast<float>(n.scale[0]),
                      static_cast<float>(n.scale[1]),
                      static_cast<float>(n.scale[2]));
    }
    const glm::mat4 T = glm::translate(glm::mat4(1.F), t);
    const glm::mat4 R = glm::mat4_cast(q);
    const glm::mat4 S = glm::scale(glm::mat4(1.F), s);
    return T * R * S;
}

std::uint32_t read_index(const tinygltf::Model &model,
                         const tinygltf::Accessor &acc, size_t i) {
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    size_t el = 4u;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        el = 2u;
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        el = 1u;
    }
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        std::uint8_t v = 0;
        std::memcpy(&v, buf.data.data() + off, sizeof(v));
        return v;
    }
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        std::uint16_t v = 0;
        std::memcpy(&v, buf.data.data() + off, sizeof(v));
        return v;
    }
    std::uint32_t v = 0;
    std::memcpy(&v, buf.data.data() + off, sizeof(v));
    return v;
}

bool read_float3(const tinygltf::Model &model, int accessor_idx, size_t i,
                 glm::vec3 &out) {
    const tinygltf::Accessor &acc = model.accessors[accessor_idx];
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        acc.type != TINYGLTF_TYPE_VEC3) {
        return false;
    }
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    const size_t el = sizeof(float) * 3u;
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    const float *p = reinterpret_cast<const float *>(buf.data.data() + off);
    out = glm::vec3(p[0], p[1], p[2]);
    return true;
}

bool read_float2(const tinygltf::Model &model, int accessor_idx, size_t i,
                 glm::vec2 &out) {
    const tinygltf::Accessor &acc = model.accessors[accessor_idx];
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        acc.type != TINYGLTF_TYPE_VEC2) {
        return false;
    }
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    const size_t el = sizeof(float) * 2u;
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    const float *p = reinterpret_cast<const float *>(buf.data.data() + off);
    out = glm::vec2(p[0], p[1]);
    return true;
}

void append_primitive(const tinygltf::Model &model,
                      const tinygltf::Primitive &prim, const glm::mat4 &world,
                      lumen::core::ObjMesh &out,
                      std::vector<lumen::core::GltfSubmeshRange> *submeshes) {
    const int mode = prim.mode;
    if (mode != TINYGLTF_MODE_TRIANGLES && mode != -1) {
        return;
    }

    auto pit = prim.attributes.find("POSITION");
    if (pit == prim.attributes.end()) {
        return;
    }
    const std::uint32_t submesh_first_index =
        submeshes != nullptr ? static_cast<std::uint32_t>(out.indices.size()) : 0u;
    const int pos_acc = pit->second;
    const tinygltf::Accessor &pos_access = model.accessors[pos_acc];
    const size_t vcount = pos_access.count;

    int nrm_acc = -1;
    auto nit = prim.attributes.find("NORMAL");
    if (nit != prim.attributes.end()) {
        nrm_acc = nit->second;
    }
    int uv_acc = -1;
    auto uit = prim.attributes.find("TEXCOORD_0");
    if (uit != prim.attributes.end()) {
        uv_acc = uit->second;
    }

    const glm::mat3 n_world =
        glm::transpose(glm::inverse(glm::mat3(world)));

    const std::uint32_t vbase =
        static_cast<std::uint32_t>(out.vertices.size());
    const size_t vert_start = out.vertices.size();
    for (size_t i = 0; i < vcount; ++i) {
        glm::vec3 p {};
        if (!read_float3(model, pos_acc, i, p)) {
            while (out.vertices.size() > vert_start) {
                out.vertices.pop_back();
            }
            return;
        }
        const glm::vec3 wp = glm::vec3(world * glm::vec4(p, 1.F));

        glm::vec3 n { 0.F, 1.F, 0.F };
        if (nrm_acc >= 0) {
            glm::vec3 ln {};
            if (read_float3(model, nrm_acc, i, ln)) {
                n = glm::normalize(n_world * ln);
            }
        }

        glm::vec2 uv { 0.F };
        if (uv_acc >= 0) {
            (void)read_float2(model, uv_acc, i, uv);
        }

        out.vertices.push_back(Vertex { wp, n, uv });
    }

    if (prim.indices >= 0) {
        const tinygltf::Accessor &ia = model.accessors[prim.indices];
        for (size_t i = 0; i < ia.count; ++i) {
            const std::uint32_t idx = read_index(model, ia, i);
            out.indices.push_back(vbase + idx);
        }
    } else {
        for (size_t i = 0; i + 2 < vcount; i += 3) {
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i));
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i + 1));
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i + 2));
        }
    }

    if (submeshes != nullptr) {
        const std::uint32_t total =
            static_cast<std::uint32_t>(out.indices.size());
        if (total > submesh_first_index) {
            submeshes->push_back(lumen::core::GltfSubmeshRange {
                submesh_first_index, total - submesh_first_index, prim.material });
        }
    }
}

void append_mesh(const tinygltf::Model &model, int mesh_idx,
                 const glm::mat4 &world, lumen::core::ObjMesh &out,
                 std::vector<lumen::core::GltfSubmeshRange> *submeshes) {
    if (mesh_idx < 0 || mesh_idx >= static_cast<int>(model.meshes.size())) {
        return;
    }
    const tinygltf::Mesh &mesh = model.meshes[static_cast<size_t>(mesh_idx)];
    for (const auto &prim : mesh.primitives) {
        append_primitive(model, prim, world, out, submeshes);
    }
}

void traverse_nodes_to_mesh(const tinygltf::Model &model, int node_idx,
                            const glm::mat4 &parent, lumen::core::ObjMesh &out,
                            std::vector<lumen::core::GltfSubmeshRange> *submeshes) {
    if (node_idx < 0 || node_idx >= static_cast<int>(model.nodes.size())) {
        return;
    }
    const tinygltf::Node &node = model.nodes[static_cast<size_t>(node_idx)];
    const glm::mat4 world = parent * node_local_matrix(node);
    if (node.mesh >= 0) {
        append_mesh(model, node.mesh, world, out, submeshes);
    }
    for (int c : node.children) {
        traverse_nodes_to_mesh(model, c, world, out, submeshes);
    }
}

glm::vec4 gltf_base_color_factor(const tinygltf::Material &m) {
    const auto &p = m.pbrMetallicRoughness.baseColorFactor;
    if (p.size() >= 4) {
        return glm::vec4(static_cast<float>(p[0]), static_cast<float>(p[1]),
                         static_cast<float>(p[2]), static_cast<float>(p[3]));
    }
    return glm::vec4(1.F, 1.F, 1.F, 1.F);
}

/// glTF：`baseColor`/`emissive` 为 sRGB；`metallicRoughness`、`normal`、`occlusion` 为线性数据
bool load_gltf_image_to_texture(const tinygltf::Model &model,
                                const fs::path &gltf_dir, int image_idx,
                                const lumen::render::Context &ctx,
                                VkQueue transfer_queue,
                                lumen::render::CommandPool &cmd_pool,
                                lumen::render::Texture &out_tex,
                                VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB) {
    if (image_idx < 0 ||
        image_idx >= static_cast<int>(model.images.size())) {
        return false;
    }
    const tinygltf::Image &im = model.images[static_cast<size_t>(image_idx)];

    if (!im.image.empty() && im.width > 0 && im.height > 0) {
        return out_tex.create_from_memory(
            ctx, im.image.data(), im.image.size(),
            static_cast<std::uint32_t>(im.width),
            static_cast<std::uint32_t>(im.height), transfer_queue, cmd_pool,
            image_format, {}, true);
    }
    if (!im.uri.empty()) {
        const fs::path abs = (gltf_dir / fs::path(im.uri)).lexically_normal();
        std::error_code ec;
        if (fs::exists(abs, ec)) {
            return out_tex.create_from_file(ctx, abs.string().c_str(),
                                            transfer_queue, cmd_pool, {},
                                            image_format);
        }
    }
    return false;
}

bool load_gltf_texture_index(const tinygltf::Model &model, const fs::path &gltf_dir,
                             int tex_index, const lumen::render::Context &ctx,
                             VkQueue transfer_queue,
                             lumen::render::CommandPool &cmd_pool,
                             lumen::render::Texture &out_tex,
                             VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB) {
    if (tex_index < 0 ||
        tex_index >= static_cast<int>(model.textures.size())) {
        return false;
    }
    const int src = model.textures[static_cast<size_t>(tex_index)].source;
    return load_gltf_image_to_texture(model, gltf_dir, src, ctx, transfer_queue,
                                      cmd_pool, out_tex, image_format);
}

lumen::render::PbrMaterialUbo pbr_ubo_from_gltf_material(
    const tinygltf::Material &m, std::uint32_t tex_mask) {
    lumen::render::PbrMaterialUbo u {};
    const auto &pbr = m.pbrMetallicRoughness;
    u.base_color_factor = gltf_base_color_factor(m);
    u.mr_ao_factors =
        glm::vec4(static_cast<float>(pbr.metallicFactor),
                  static_cast<float>(pbr.roughnessFactor),
                  static_cast<float>(m.occlusionTexture.strength), 0.F);
    if (m.emissiveFactor.size() >= 3) {
        u.emissive_factor =
            glm::vec4(static_cast<float>(m.emissiveFactor[0]),
                      static_cast<float>(m.emissiveFactor[1]),
                      static_cast<float>(m.emissiveFactor[2]), 0.F);
    }
    if (m.alphaMode == "MASK") {
        u.shader_params.x = 1.F;
    } else if (m.alphaMode == "BLEND") {
        u.shader_params.x = 2.F;
    }
    u.shader_params.y = static_cast<float>(m.alphaCutoff);
    u.shader_params.z = lumen::render::uint_bits_to_float(tex_mask);
    return u;
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
                   const lumen::render::Texture &fallback, std::uint32_t binding) {
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

} // namespace

static int run_tinygltf_raw_viewer() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig win_cfg;
    win_cfg.title = "tinygltf raw — PBR (Sponza)";
    win_cfg.width = 1280;
    win_cfg.height = 720;

    if (!window.create(win_cfg)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    const std::string model_path =
        lumen::core::get_resource_path(kSponzaGltfRel);
    LUMEN_APP_LOG_INFO("模型: {}", model_path);

    // ---------- tinygltf：在 main 流程内直接加载（不经 `load_gltf`）----------
    tinygltf::Model model {};
    tinygltf::TinyGLTF loader {};
    std::string err;
    std::string warn;
    const bool is_glb = ends_with_ci(model_path, ".glb");
    const bool load_ok =
        is_glb ? loader.LoadBinaryFromFile(&model, &err, &warn, model_path)
               : loader.LoadASCIIFromFile(&model, &err, &warn, model_path);
    if (!warn.empty()) {
        LUMEN_APP_LOG_WARN("tinygltf: {}", warn);
    }
    if (!err.empty()) {
        LUMEN_APP_LOG_ERROR("tinygltf: {}", err);
    }
    if (!load_ok) {
        LUMEN_APP_LOG_ERROR("加载 glTF 失败");
        return -1;
    }

    int scene_index = model.defaultScene;
    if (scene_index < 0 || scene_index >= static_cast<int>(model.scenes.size())) {
        scene_index = model.scenes.empty() ? -1 : 0;
    }
    if (scene_index < 0) {
        LUMEN_APP_LOG_ERROR("无场景");
        return -1;
    }

    lumen::core::ObjMesh mesh {};
    std::vector<lumen::core::GltfSubmeshRange> submeshes {};
    const tinygltf::Scene &scene = model.scenes[static_cast<size_t>(scene_index)];
    const glm::mat4 root(1.F);
    for (int root_node : scene.nodes) {
        traverse_nodes_to_mesh(model, root_node, root, mesh, &submeshes);
    }

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        LUMEN_APP_LOG_ERROR("网格为空（无三角形可绘制）");
        return -1;
    }

    if (submeshes.empty()) {
        submeshes.push_back(lumen::core::GltfSubmeshRange {
            0, static_cast<std::uint32_t>(mesh.indices.size()), -1 });
    }

    LUMEN_APP_LOG_INFO("合并网格: {} 顶点, {} 索引, {} 段子网格（按材质分 draw）",
                       mesh.vertices.size(), mesh.indices.size(),
                       submeshes.size());

    glm::vec3 mesh_center { 0.F };
    glm::vec3 mesh_half_extents { 1.F };
    {
        glm::vec3 mn = mesh.vertices[0].position;
        glm::vec3 mx = mn;
        for (const Vertex &v : mesh.vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        mesh_center = 0.5f * (mn + mx);
        mesh_half_extents = 0.5f * (mx - mn);
    }

    auto extensions = window.get_vulkan_instance_extensions();
    lumen::render::ContextConfig ctx_cfg;
    ctx_cfg.instanceExtensions.assign(extensions.begin(), extensions.end());

    lumen::render::Context ctx;
    if (!ctx.init_instance(ctx_cfg)) {
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
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig rp_cfg;
    rp_cfg.useDepth = true;
    rp_cfg.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass render_pass;
    if (!render_pass.create(ctx.device(), rp_cfg)) {
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    lumen::render::Image depth_image;
    if (!depth_image.create_depth_attachment(ctx, static_cast<uint32_t>(w),
                                             static_cast<uint32_t>(h))) {
        LUMEN_APP_LOG_ERROR("深度附件创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), render_pass.handle(), swapchain,
                             depth_image.view())) {
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    std::string vert_spv =
        lumen::core::get_resource_path("shaders/gltf_pbr.vert.spv");
    std::string frag_spv =
        lumen::core::get_resource_path("shaders/gltf_pbr.frag.spv");
    lumen::render::ShaderModule vert_shader;
    lumen::render::ShaderModule frag_shader;
    if (!vert_shader.create_from_file(ctx.device(), vert_spv.c_str()) ||
        !frag_shader.create_from_file(ctx.device(), frag_spv.c_str())) {
        LUMEN_APP_LOG_ERROR("着色器加载失败 vert={} frag={}", vert_spv,
                            frag_spv);
        return -1;
    }

    const size_t v_bytes = mesh.vertices.size() * sizeof(Vertex);
    const size_t i_bytes = mesh.indices.size() * sizeof(std::uint32_t);

    lumen::render::VertexBuffer vertex_buffer;
    lumen::render::IndexBuffer index_buffer;
    if (!vertex_buffer.create(ctx, v_bytes) ||
        !index_buffer.create(ctx, i_bytes)) {
        LUMEN_APP_LOG_ERROR("几何 Buffer 创建失败");
        return -1;
    }
    index_buffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    vertex_buffer.upload(mesh.vertices.data(), v_bytes);
    index_buffer.upload(mesh.indices.data(), i_bytes);

    lumen::render::CommandPool cmd_pool;
    if (!cmd_pool.create(ctx, ctx.graphics_queue_family())) {
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    lumen::render::PbrPlaceholderTextures placeholder_textures;
    if (!placeholder_textures.create(ctx, ctx.graphics_queue(), cmd_pool)) {
        LUMEN_APP_LOG_ERROR("PBR 占位纹理创建失败");
        return -1;
    }

    constexpr std::uint32_t k_env_face_size { 256 };
    std::vector<std::array<std::vector<std::uint8_t>, 6>> radiance_mips;
    tinygltf_test::ibl::build_radiance_env_mipmap_chain_rgba8(k_env_face_size,
                                                            radiance_mips);
    std::vector<lumen::render::CubemapRgba8MipLevel> radiance_mip_desc(
        radiance_mips.size());
    for (size_t mi = 0; mi < radiance_mips.size(); ++mi) {
        radiance_mip_desc[mi].face_size =
            std::max(1u, k_env_face_size >> static_cast<std::uint32_t>(mi));
        for (int f = 0; f < 6; ++f) {
            radiance_mip_desc[mi].faces[static_cast<size_t>(f)] =
                radiance_mips[mi][static_cast<size_t>(f)].data();
        }
    }
    lumen::render::Texture env_cubemap;
    lumen::render::SamplerConfig env_sampler_cfg {};
    env_sampler_cfg.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!env_cubemap.create_cubemap_from_rgba8_mip_chain(
            ctx, radiance_mip_desc.data(), radiance_mip_desc.size(),
            ctx.graphics_queue(), cmd_pool, env_sampler_cfg,
            VK_FORMAT_R8G8B8A8_UNORM)) {
        LUMEN_APP_LOG_ERROR("辐射度环境立方体（GGX mip 链）创建失败");
        return -1;
    }

    std::array<std::vector<std::uint8_t>, 6> irradiance_faces {};
    constexpr std::uint32_t k_irradiance_face_size { 32 };
    tinygltf_test::ibl::fill_irradiance_environment_faces(k_irradiance_face_size,
                                                          irradiance_faces);
    const void *irr_face_ptrs[6] = {
        irradiance_faces[0].data(), irradiance_faces[1].data(),
        irradiance_faces[2].data(), irradiance_faces[3].data(),
        irradiance_faces[4].data(), irradiance_faces[5].data()
    };
    lumen::render::Texture irradiance_cubemap;
    if (!irradiance_cubemap.create_cubemap_from_rgba8_faces(
            ctx, irr_face_ptrs, k_irradiance_face_size, ctx.graphics_queue(),
            cmd_pool, env_sampler_cfg, VK_FORMAT_R8G8B8A8_UNORM)) {
        LUMEN_APP_LOG_ERROR("辐照度立方体贴图创建失败");
        return -1;
    }

    std::vector<std::uint8_t> brdf_lut_rgba;
    tinygltf_test::ibl::generate_brdf_lut_rgba8(brdf_lut_rgba, 256);
    lumen::render::Texture brdf_lut_tex;
    if (!brdf_lut_tex.create_from_memory(
            ctx, brdf_lut_rgba.data(), brdf_lut_rgba.size(), 256, 256,
            ctx.graphics_queue(), cmd_pool, VK_FORMAT_R8G8B8A8_UNORM,
            lumen::render::SamplerConfig {}, false)) {
        LUMEN_APP_LOG_ERROR("BRDF LUT 创建失败");
        return -1;
    }

    const fs::path gltf_dir { fs::path(model_path).parent_path() };
    const size_t nmat = model.materials.size();
    const size_t n_tex_sets = std::max(nmat + 1, size_t { 1 });

    std::vector<lumen::render::Texture> mat_albedo(nmat);
    std::vector<lumen::render::Texture> mat_normal(nmat);
    std::vector<lumen::render::Texture> mat_mr(nmat);
    std::vector<lumen::render::Texture> mat_ao(nmat);
    std::vector<lumen::render::Texture> mat_emissive(nmat);
    std::vector<lumen::render::PbrMaterialUbo> mat_ubo_cpu(n_tex_sets);

    for (size_t mi = 0; mi < nmat; ++mi) {
        const tinygltf::Material &tm = model.materials[mi];
        std::uint32_t mask = 0;
        if (load_gltf_texture_index(
                model, gltf_dir, tm.pbrMetallicRoughness.baseColorTexture.index,
                ctx, ctx.graphics_queue(), cmd_pool, mat_albedo[mi],
                VK_FORMAT_R8G8B8A8_SRGB)) {
            mask |= lumen::render::kMatTexBitAlbedo;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.normalTexture.index, ctx,
                                    ctx.graphics_queue(), cmd_pool,
                                    mat_normal[mi], VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitNormal;
        }
        if (load_gltf_texture_index(
                model, gltf_dir,
                tm.pbrMetallicRoughness.metallicRoughnessTexture.index, ctx,
                ctx.graphics_queue(), cmd_pool, mat_mr[mi],
                VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitMetallicRoughness;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.occlusionTexture.index,
                                    ctx, ctx.graphics_queue(), cmd_pool,
                                    mat_ao[mi], VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitOcclusion;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.emissiveTexture.index,
                                    ctx, ctx.graphics_queue(), cmd_pool,
                                    mat_emissive[mi], VK_FORMAT_R8G8B8A8_SRGB)) {
            mask |= lumen::render::kMatTexBitEmissive;
        }
        mat_ubo_cpu[mi] = pbr_ubo_from_gltf_material(tm, mask);
    }
    mat_ubo_cpu[n_tex_sets - 1] = lumen::render::PbrMaterialUbo {};

    std::vector<lumen::render::UniformBuffer> mat_uniform_buffers(n_tex_sets);
    for (size_t si = 0; si < n_tex_sets; ++si) {
        if (!mat_uniform_buffers[si].create(ctx,
                                              sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 创建失败");
            return -1;
        }
        mat_uniform_buffers[si].update(mat_ubo_cpu[si]);
    }

    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> scene_uniform_buffers;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!scene_uniform_buffers[i].create(ctx, sizeof(SceneUbo))) {
            LUMEN_APP_LOG_ERROR("场景 UniformBuffer 创建失败");
            return -1;
        }
    }

    lumen::render::DescriptorSetLayout scene_desc_layout;
    if (!scene_desc_layout.create(
            ctx, { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        LUMEN_APP_LOG_ERROR("场景 DescriptorSetLayout 创建失败");
        return -1;
    }

    lumen::render::DescriptorSetLayout material_desc_layout;
    if (!material_desc_layout.create(
            ctx,
            { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        LUMEN_APP_LOG_ERROR("材质 DescriptorSetLayout 创建失败");
        return -1;
    }

    const std::uint32_t n_mat_sets_u32 = static_cast<std::uint32_t>(n_tex_sets);
    lumen::render::DescriptorPool desc_pool;
    if (!desc_pool.create(
            ctx,
            { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                kMaxFramesInFlight + n_mat_sets_u32 },
              { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                kMaxFramesInFlight * 3U + n_mat_sets_u32 * 5U } },
            kMaxFramesInFlight + n_mat_sets_u32)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
    }

    std::array<VkDescriptorSet, kMaxFramesInFlight> scene_descriptor_sets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!desc_pool.allocate(ctx.device(), scene_desc_layout.handle(),
                                scene_descriptor_sets[i])) {
            LUMEN_APP_LOG_ERROR("场景 DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_buffer(
            ctx.device(), scene_descriptor_sets[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_uniform_buffers[i].handle(),
            0, sizeof(SceneUbo));
        lumen::render::write_descriptor_image(
            ctx.device(), scene_descriptor_sets[i], 1, env_cubemap.view(),
            env_cubemap.sampler());
        lumen::render::write_descriptor_image(
            ctx.device(), scene_descriptor_sets[i], 2, brdf_lut_tex.view(),
            brdf_lut_tex.sampler());
        lumen::render::write_descriptor_image(
            ctx.device(), scene_descriptor_sets[i], 3, irradiance_cubemap.view(),
            irradiance_cubemap.sampler());
    }

    std::vector<VkDescriptorSet> mat_descriptor_sets(n_tex_sets);
    for (size_t si = 0; si < n_tex_sets; ++si) {
        if (!desc_pool.allocate(ctx.device(), material_desc_layout.handle(),
                                mat_descriptor_sets[si])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_buffer(
            ctx.device(), mat_descriptor_sets[si], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            mat_uniform_buffers[si].handle(), 0,
            sizeof(lumen::render::PbrMaterialUbo));
        const lumen::render::Texture *pa =
            (si < nmat) ? &mat_albedo[si] : nullptr;
        const lumen::render::Texture *pn =
            (si < nmat) ? &mat_normal[si] : nullptr;
        const lumen::render::Texture *pmr =
            (si < nmat) ? &mat_mr[si] : nullptr;
        const lumen::render::Texture *pao =
            (si < nmat) ? &mat_ao[si] : nullptr;
        const lumen::render::Texture *pem =
            (si < nmat) ? &mat_emissive[si] : nullptr;
        write_material_descriptor_images(ctx.device(), mat_descriptor_sets[si],
                                         placeholder_textures, pa, pn, pmr, pao,
                                         pem);
    }

    lumen::render::PipelineLayout pipeline_layout;
    if (!pipeline_layout.create(
            ctx, { scene_desc_layout.handle(), material_desc_layout.handle() },
            {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipe_cfg;
    pipe_cfg.stages.push_back(
        { vert_shader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipe_cfg.stages.push_back(
        { frag_shader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipe_cfg.vertexBindings.push_back(
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
    pipe_cfg.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
    pipe_cfg.vertexAttributes.push_back(
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
    pipe_cfg.vertexAttributes.push_back(
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
    pipe_cfg.depthTest = true;
    pipe_cfg.depthWrite = true;
    pipe_cfg.cullMode = VK_CULL_MODE_BACK_BIT;
    pipe_cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipeline_layout.handle(), render_pass.handle(), 0,
                         pipe_cfg)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

    auto cmd_buffers = cmd_pool.allocate(kMaxFramesInFlight);
    if (cmd_buffers.size() != kMaxFramesInFlight) {
        LUMEN_APP_LOG_ERROR("CommandBuffer 分配失败");
        return -1;
    }

    lumen::render::FrameSync frame_sync;
    if (!frame_sync.create(ctx.device(), swapchain.image_count(),
                          kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    lumen::scene::SceneOrbitCamera cam;
    cam.set_pivot(mesh_center);
    lumen::scene::SceneOrbitCameraLimits lim = cam.limits();
    lim.max_radius = 800.F;
    lim.min_radius = 0.2F;
    cam.set_limits(lim);
    const float fit_r =
        glm::max(1.F, glm::length(mesh_half_extents) * 2.8F);
    cam.set_radius(fit_r);
    cam.set_yaw(0.9F);
    cam.set_pitch(0.45F);
    cam.set_depth_range(0.05F, 2000.F);

    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;

    lumen::platform::EventPump pump;
    uint32_t current_frame { 0 };
    bool running { true };
    int fb_width { w }, fb_height { h };
    bool need_recreate_swapchain { false };
    float mouse_wheel_dy { 0.F };

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape) {
            running = false;
        }
    });
    pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel &e) {
        mouse_wheel_dy += e.deltaY;
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fb_width = r.width;
        fb_height = r.height;
        need_recreate_swapchain = true;
    });

    constexpr uint64_t k_acquire_timeout_ns = 100'000'000;
    constexpr uint64_t k_fence_wait_timeout_ns = 16'000'000;

    LUMEN_APP_LOG_INFO(
        "WASD 平移枢轴，QE 旋转 yaw，鼠标滚轮缩放；顶点已在 CPU 乘节点世界矩阵");

    while (running) {
        if (!pump.poll()) {
            break;
        }

        if (need_recreate_swapchain) {
            window.get_framebuffer_size(&fb_width, &fb_height);
            if (fb_width > 0 && fb_height > 0) {
                lumen::render::Image new_depth;
                if (new_depth.create_depth_attachment(
                        ctx, static_cast<uint32_t>(fb_width),
                        static_cast<uint32_t>(fb_height)) &&
                    lumen::render::recreate_swapchain_resources(
                        ctx, swapchain, framebuffers, frame_sync,
                        render_pass.handle(), static_cast<uint32_t>(fb_width),
                        static_cast<uint32_t>(fb_height), kMaxFramesInFlight,
                        new_depth.view())) {
                    depth_image = std::move(new_depth);
                }
            }
            need_recreate_swapchain = false;
            continue;
        }

        while (!frame_sync.wait_fence(current_frame, k_fence_wait_timeout_ns)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running) {
            break;
        }

        const float dt = static_cast<float>(frame_dt.tick_seconds());
        const auto &inp = pump.input();
        const float move_s = 2.F * dt;
        glm::vec3 pivot = cam.pivot();
        if (inp.is_key_down(lumen::platform::Key::W)) {
            pivot.z -= move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::S)) {
            pivot.z += move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::A)) {
            pivot.x -= move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::D)) {
            pivot.x += move_s;
        }
        cam.set_pivot(pivot);
        if (inp.is_key_down(lumen::platform::Key::Q)) {
            cam.set_yaw(cam.yaw() - 1.2F * dt);
        }
        if (inp.is_key_down(lumen::platform::Key::E)) {
            cam.set_yaw(cam.yaw() + 1.2F * dt);
        }
        cam.apply_scroll_zoom(mouse_wheel_dy, 2.F);
        mouse_wheel_dy = 0.F;

        const float aspect =
            static_cast<float>(swapchain.extent().width) /
            std::max(1.F, static_cast<float>(swapchain.extent().height));
        const glm::mat4 proj = cam.projection_matrix(aspect);
        const glm::mat4 view = cam.view_matrix();
        const glm::mat4 view_proj = proj * view;

        uint32_t image_index = swapchain.acquire_next_image(
            frame_sync.image_available(current_frame), VK_NULL_HANDLE,
            k_acquire_timeout_ns);
        if (image_index == UINT32_MAX) {
            continue;
        }

        vkResetCommandBuffer(cmd_buffers[current_frame], 0);
        VkCommandBufferBeginInfo begin_info {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd_buffers[current_frame], &begin_info) !=
            VK_SUCCESS) {
            continue;
        }

        VkClearValue clear_values[2];
        clear_values[0].color = { { 0.08f, 0.09f, 0.12f, 1.0f } };
        clear_values[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp_begin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rp_begin.renderPass = render_pass.handle();
        rp_begin.framebuffer = framebuffers.get(image_index);
        rp_begin.renderArea.offset = { 0, 0 };
        rp_begin.renderArea.extent = swapchain.extent();
        rp_begin.clearValueCount = 2;
        rp_begin.pClearValues = clear_values;

        vkCmdBeginRenderPass(cmd_buffers[current_frame], &rp_begin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.x = 0.F;
        viewport.y = 0.F;
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.minDepth = 0.F;
        viewport.maxDepth = 1.F;
        vkCmdSetViewport(cmd_buffers[current_frame], 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { 0, 0 };
        scissor.extent = swapchain.extent();
        vkCmdSetScissor(cmd_buffers[current_frame], 0, 1, &scissor);

        vkCmdBindPipeline(cmd_buffers[current_frame],
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

        SceneUbo scene_ubo {};
        scene_ubo.model = glm::mat4(1.F);
        scene_ubo.mvp = view_proj;
        scene_ubo.normal_matrix = glm::mat4(1.F);
        scene_ubo.camera_world = glm::vec4(cam.eye_position(), 0.F);
        scene_ubo.lights[0].position = glm::vec4(0.F, 0.F, 0.F, 0.F);
        scene_ubo.lights[0].direction =
            glm::vec4(glm::normalize(glm::vec3(0.35F, 1.F, 0.28F)), 0.F);
        scene_ubo.lights[0].color =
            glm::vec4(1.F, 0.97F, 0.93F, 5.F);
        scene_ubo.scene_params = glm::vec4(1.F, 0.F, 0.F, 0.F);
        const glm::mat4 view_rot = glm::mat4(glm::mat3(view));
        scene_ubo.sky_mvp = proj * view_rot;
        scene_ubo.sky_orient_inv = glm::inverse(view_rot);
        const float max_mip = static_cast<float>(
            env_cubemap.mip_levels() > 0 ? env_cubemap.mip_levels() - 1 : 0);
        // x=曝光，y=辐射度最大 mip，z 保留，w=IBL 强度（环境贴图为 UNORM 线性 texel）
        scene_ubo.env_params = glm::vec4(2.5F, max_mip, 0.F, 1.9F);
        scene_uniform_buffers[current_frame].update(scene_ubo);

        VkBuffer vb = vertex_buffer.handle();
        VkDeviceSize vb_off { 0 };
        vkCmdBindVertexBuffers(cmd_buffers[current_frame], 0, 1, &vb, &vb_off);
        vkCmdBindIndexBuffer(cmd_buffers[current_frame], index_buffer.handle(),
                             0, index_buffer.vk_index_type());

        vkCmdBindDescriptorSets(cmd_buffers[current_frame],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout.handle(), 0, 1,
                                &scene_descriptor_sets[current_frame], 0,
                                nullptr);

        for (const lumen::core::GltfSubmeshRange &sub : submeshes) {
            const int mid = sub.material_index;
            size_t tex_set_i = n_tex_sets - 1;
            if (mid >= 0 && static_cast<size_t>(mid) < nmat) {
                tex_set_i = static_cast<size_t>(mid);
            }

            vkCmdBindDescriptorSets(cmd_buffers[current_frame],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout.handle(), 1, 1,
                                    &mat_descriptor_sets[tex_set_i], 0, nullptr);

            vkCmdDrawIndexed(cmd_buffers[current_frame], sub.index_count, 1,
                             sub.first_index, 0, 0);
        }

        vkCmdEndRenderPass(cmd_buffers[current_frame]);
        if (vkEndCommandBuffer(cmd_buffers[current_frame]) != VK_SUCCESS) {
            continue;
        }

        VkSubmitInfo submit_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore wait_sem = frame_sync.image_available(current_frame);
        VkSemaphore signal_sem = frame_sync.render_finished(image_index);
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &wait_sem;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffers[current_frame];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &signal_sem;

        if (vkQueueSubmit(ctx.graphics_queue(), 1, &submit_info,
                          frame_sync.in_flight_fence(current_frame)) !=
            VK_SUCCESS) {
            continue;
        }

        VkResult pr = swapchain.present(ctx.present_queue(), image_index,
                                        signal_sem);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
            need_recreate_swapchain = true;
        }

        current_frame = (current_frame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    LUMEN_APP_LOG_INFO("退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int r = run_tinygltf_raw_viewer();
    lumen::core::Logger::shutdown();
    return r;
}
