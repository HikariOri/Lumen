/**
 * @file gltf/gltf_scene_mesh.cpp
 * @brief `load_gltf_scene_mesh` 实现
 */

#include "gltf/gltf_scene_mesh.hpp"

#include "gltf/gltf_loader.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/material/material.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/texture.hpp"
#include "render/vertex_layout.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>

#include <glm/glm.hpp>

#include <ghc/filesystem.hpp>

namespace lumen::scene {
namespace {

namespace fs = ghc::filesystem;

/// 与 `make_vertex_layout_pbr_forward_tangent()` 内布局一致
struct PbrInterleavedVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
    glm::vec4 tangent { 1.0F, 0.0F, 0.0F, 1.0F };
};

void recenter_and_scale_cpu_mesh(lumen::core::CpuMesh &mesh,
                                 const GltfSceneMeshLoadOptions &opts) {
    if (mesh.vertices.empty()) {
        return;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (const auto &v : mesh.vertices) {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    if (opts.recenterToOrigin) {
        const glm::vec3 center { 0.5F * (bmin + bmax) };
        for (auto &v : mesh.vertices) {
            v.position -= center;
        }
        bmin -= center;
        bmax -= center;
    }
    if (opts.uniformScaleMaxAxis > 0.F) {
        const glm::vec3 extent { bmax - bmin };
        const float max_axis =
            (std::max)(extent.x, (std::max)(extent.y, extent.z));
        const float s =
            max_axis > 1e-8F ? (opts.uniformScaleMaxAxis / max_axis) : 1.0F;
        for (auto &v : mesh.vertices) {
            v.position *= s;
        }
    }
}

struct PrimitiveLocalAabb {
    glm::vec3 center { 0.0F, 0.0F, 0.0F };
    glm::vec3 half_extent { 0.0F, 0.0F, 0.0F };
    bool valid { false };
};

/// primitive 索引范围内所引用顶点的 AABB（mesh 空间）
[[nodiscard]] PrimitiveLocalAabb primitive_index_local_aabb(
    const std::vector<PbrInterleavedVertex> &verts,
    const std::vector<std::uint32_t> &indices, std::uint32_t first_index,
    std::uint32_t index_count) {
    PrimitiveLocalAabb out {};
    if (index_count == 0U) {
        return out;
    }
    const std::size_t end =
        static_cast<std::size_t>(first_index) +
        static_cast<std::size_t>(index_count);
    if (end > indices.size()) {
        return out;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    std::uint32_t hit_count { 0U };
    for (std::uint32_t i = 0; i < index_count; ++i) {
        const std::uint32_t vi = indices[first_index + i];
        if (vi >= verts.size()) {
            continue;
        }
        const glm::vec3 &p = verts[vi].position;
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
        ++hit_count;
    }
    if (hit_count == 0U) {
        return out;
    }
    out.center = 0.5F * (bmin + bmax);
    out.half_extent = 0.5F * (bmax - bmin);
    out.valid = true;
    return out;
}

void compute_mesh_tangents(std::vector<PbrInterleavedVertex> &verts,
                           const std::vector<std::uint32_t> &indices) {
    std::vector<glm::vec3> accum_tan_u(verts.size(), glm::vec3(0.0F));
    std::vector<glm::vec3> accum_tan_v(verts.size(), glm::vec3(0.0F));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::uint32_t i0 = indices[i];
        const std::uint32_t i1 = indices[i + 1];
        const std::uint32_t i2 = indices[i + 2];
        const PbrInterleavedVertex &v0 = verts[i0];
        const PbrInterleavedVertex &v1 = verts[i1];
        const PbrInterleavedVertex &v2 = verts[i2];
        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 duv1 = v1.uv - v0.uv;
        const glm::vec2 duv2 = v2.uv - v0.uv;
        const float denom = duv1.x * duv2.y - duv2.x * duv1.y + 1e-8F;
        const float f = 1.0F / denom;
        const glm::vec3 t = f * (edge1 * duv2.y - edge2 * duv1.y);
        const glm::vec3 b = f * (edge2 * duv1.x - edge1 * duv2.x);
        accum_tan_u[i0] += t;
        accum_tan_u[i1] += t;
        accum_tan_u[i2] += t;
        accum_tan_v[i0] += b;
        accum_tan_v[i1] += b;
        accum_tan_v[i2] += b;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        const glm::vec3 &n = verts[i].normal;
        glm::vec3 t = accum_tan_u[i];
        t = glm::normalize(t - n * glm::dot(n, t));
        const float w =
            glm::dot(glm::cross(n, t), glm::normalize(accum_tan_v[i])) < 0.0F
                ? -1.0F
                : 1.0F;
        verts[i].tangent = glm::vec4(t, w);
    }
}

void fail(std::string *err, const char *msg) {
    if (err != nullptr) {
        err->append(msg);
    }
}

} // namespace

bool load_gltf_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                          lumen::render::CommandPool &cmd_pool,
                          const std::string_view gltf_path, GltfSceneMesh &out,
                          const GltfSceneMeshLoadOptions &opts,
                          std::string *error_message) {
    out = GltfSceneMesh {};

    lumen::core::CpuMesh cpu {};
    std::vector<lumen::core::PrimitiveSlice> slices {};
    std::vector<lumen::render::MaterialLoadDesc> mat_descs {};
    std::size_t gltf_mesh_count = 0;

    const std::string path_str { gltf_path };
    if (!lumen::core::load_gltf(path_str, cpu, nullptr, &slices, &mat_descs,
                                &gltf_mesh_count)) {
        fail(error_message, "load_gltf 解析失败");
        return false;
    }
    if (cpu.vertices.empty() || cpu.indices.empty()) {
        fail(error_message, "glTF 几何为空");
        return false;
    }

    if (slices.empty()) {
        slices.push_back(lumen::core::PrimitiveSlice {
            .meshIndex = 0,
            .firstIndex = 0,
            .indexCount = static_cast<std::uint32_t>(cpu.indices.size()),
            .materialIndex = 0,
        });
    }
    if (gltf_mesh_count == 0) {
        gltf_mesh_count = 1;
    }
    if (mat_descs.empty()) {
        mat_descs.emplace_back();
    }

    recenter_and_scale_cpu_mesh(cpu, opts);

    std::vector<PbrInterleavedVertex> verts;
    verts.reserve(cpu.vertices.size());
    for (const auto &cv : cpu.vertices) {
        verts.push_back(
            PbrInterleavedVertex { .position = cv.position,
                                   .normal = cv.normal,
                                   .uv = cv.uv,
                                   .tangent = { 1.0F, 0.0F, 0.0F, 1.0F } });
    }
    compute_mesh_tangents(verts, cpu.indices);

    auto vbuf = std::make_unique<lumen::render::VertexBuffer>();
    if (!vbuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, verts.data(),
            verts.size() * sizeof(PbrInterleavedVertex))) {
        fail(error_message, "顶点缓冲上传失败");
        return false;
    }
    auto ibuf = std::make_unique<lumen::render::IndexBuffer>();
    ibuf->set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!ibuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, cpu.indices.data(),
            cpu.indices.size() * sizeof(std::uint32_t))) {
        fail(error_message, "索引缓冲上传失败");
        return false;
    }

    std::unordered_map<std::string, std::size_t> tex_key_to_index {};
    std::vector<std::unique_ptr<lumen::render::Texture>> tex_storage {};

    auto acquire_texture = [&](const std::string &rel_path,
                               VkFormat fmt) -> const lumen::render::Texture * {
        if (rel_path.empty()) {
            return nullptr;
        }
        const std::string key =
            rel_path + '#' + std::to_string(static_cast<std::uint32_t>(fmt));
        const auto found = tex_key_to_index.find(key);
        if (found != tex_key_to_index.end()) {
            return tex_storage[found->second].get();
        }
        const std::string full = lumen::core::get_resource_path(rel_path);
        if (!fs::exists(fs::path(full))) {
            LUMEN_LOG_WARN("glTF 贴图不存在，跳过: {}", full);
            return nullptr;
        }
        auto tex = std::make_unique<lumen::render::Texture>();
        if (!tex->create_from_file(ctx, full.c_str(), transfer_queue, cmd_pool,
                                   {}, fmt)) {
            LUMEN_LOG_WARN("glTF 贴图上传失败: {}", full);
            return nullptr;
        }
        const std::size_t new_ix = tex_storage.size();
        tex_storage.push_back(std::move(tex));
        tex_key_to_index.emplace(key, new_ix);
        return tex_storage[new_ix].get();
    };

    out.materials.resize(mat_descs.size());
    for (std::size_t i = 0; i < mat_descs.size(); ++i) {
        const lumen::render::MaterialLoadDesc &src = mat_descs[i];
        lumen::render::Material &dst = out.materials[i];
        dst.baseColorFactor = src.base_color_factor;
        dst.metallicFactor = src.metallic_factor;
        dst.roughnessFactor = src.roughness_factor;
        dst.emissiveFactor = src.emissive_factor;
        dst.occlusionStrength = src.ao_factor;
        dst.doubleSided = src.double_sided;
        dst.alphaMode = src.alpha_mode;
        dst.baseColorTex =
            acquire_texture(src.albedo_path, VK_FORMAT_R8G8B8A8_SRGB);
        dst.normalTex =
            acquire_texture(src.normal_path, VK_FORMAT_R8G8B8A8_UNORM);
        dst.metallicRoughnessTex = acquire_texture(src.metallic_roughness_path,
                                                   VK_FORMAT_R8G8B8A8_UNORM);
        dst.occlusionTex =
            acquire_texture(src.ao_path, VK_FORMAT_R8G8B8A8_UNORM);
        dst.emissiveTex =
            acquire_texture(src.emissive_path, VK_FORMAT_R8G8B8A8_SRGB);
    }

    const lumen::render::VertexLayout layout =
        lumen::render::make_vertex_layout_pbr_forward_tangent();

    out.model.assign(gltf_mesh_count, Mesh {});
    for (const lumen::core::PrimitiveSlice &sl : slices) {
        int mesh_i = sl.meshIndex;
        if (mesh_i < 0 ||
            mesh_i >= static_cast<int>(out.model.size())) {
            mesh_i = 0;
        }
        int mi = sl.materialIndex;
        if (mi < 0 || mi >= static_cast<int>(out.materials.size())) {
            mi = 0;
        }
        const PrimitiveLocalAabb prim_aabb = primitive_index_local_aabb(
            verts, cpu.indices, sl.firstIndex, sl.indexCount);
        out.model[static_cast<std::size_t>(mesh_i)].primitives.push_back(
            Primitive { .vertex_byte_offset = 0,
                        .first_index = sl.firstIndex,
                        .index_count = sl.indexCount,
                        .base_vertex = 0,
                        .layout = layout,
                        .material = &out.materials[static_cast<std::size_t>(mi)],
                        .local_pivot = prim_aabb.valid ? prim_aabb.center
                                                       : glm::vec3(0.0F),
                        .local_aabb_half_extent =
                            prim_aabb.valid ? prim_aabb.half_extent
                                            : glm::vec3(0.0F),
                      });
    }

    out.vertexBuffer = std::move(vbuf);
    out.indexBuffer = std::move(ibuf);
    out.textures = std::move(tex_storage);
    out.statsVertexCount = verts.size();
    out.statsIndexCount = cpu.indices.size();

    std::size_t prim_total = 0;
    for (const Mesh &m : out.model) {
        prim_total += m.primitives.size();
    }
    LUMEN_LOG_DEBUG(
        "load_gltf_scene_mesh: {} 顶点, {} 索引, {} mesh, {} primitive, {} 材质",
        out.statsVertexCount, out.statsIndexCount, out.model.size(), prim_total,
        out.materials.size());
    return true;
}

} // namespace lumen::scene
