/**
 * @file asset/lumenmesh_scene_importer.cpp
 * @brief `.lumenmesh` v1 → `SceneMeshAsset`
 */

#include "asset/mesh_scene_importer.hpp"

#include "asset/asset_registry.hpp"
#include "asset/lumenmesh_format.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pbr_interleaved_vertex.hpp"
#include "render/resource/buffer.hpp"
#include "render/vertex_layout.hpp"
#include "scene/scene_mesh_asset.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace lumen::asset {
namespace {

[[nodiscard]] bool read_file_bytes(const std::string &path,
                                   std::vector<std::uint8_t> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return false;
    }
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    out.resize(sz);
    if (sz == 0) {
        return true;
    }
    f.read(reinterpret_cast<char *>(out.data()),
           static_cast<std::streamsize>(sz));
    return static_cast<std::size_t>(f.gcount()) == sz;
}

struct PrimLocalAabb {
    glm::vec3 center { 0.0F };
    glm::vec3 half { 0.0F };
    bool valid { false };
};

[[nodiscard]] PrimLocalAabb prim_aabb(
    const std::vector<lumen::render::PbrInterleavedVertex> &verts,
                                      const std::vector<std::uint32_t> &indices,
                                      const std::uint32_t first_index,
                                      const std::uint32_t index_count) {
    PrimLocalAabb r {};
    if (index_count == 0U) {
        return r;
    }
    const std::size_t end =
        static_cast<std::size_t>(first_index) +
        static_cast<std::size_t>(index_count);
    if (end > indices.size()) {
        return r;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    std::uint32_t hits { 0U };
    for (std::uint32_t i = 0; i < index_count; ++i) {
        const std::uint32_t vi = indices[first_index + i];
        if (vi >= verts.size()) {
            continue;
        }
        const glm::vec3 &p = verts[vi].position;
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
        ++hits;
    }
    if (hits == 0U) {
        return r;
    }
    r.center = 0.5F * (bmin + bmax);
    r.half = 0.5F * (bmax - bmin);
    r.valid = true;
    return r;
}

class LumenmeshSceneImporter final : public IMeshSceneImporter {
public:
    [[nodiscard]] bool import(lumen::render::Context &ctx, VkQueue transfer_queue,
                              lumen::render::CommandPool &cmd_pool,
                              const std::string_view path,
                              lumen::scene::SceneMeshAsset &out,
                              const lumen::scene::SceneMeshLoadOptions &opts,
                              std::string *error_message) const override;

    [[nodiscard]] bool
    supports_extension(const std::string_view ext_lower) const override {
        return ext_lower == ".lumenmesh";
    }
};

bool LumenmeshSceneImporter::import(
    lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, const std::string_view path,
    lumen::scene::SceneMeshAsset &out,
    const lumen::scene::SceneMeshLoadOptions &opts,
    std::string *error_message) const {
    (void)opts;
    out = lumen::scene::SceneMeshAsset {};

    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(std::string { path }, bytes)) {
        if (error_message != nullptr) {
            *error_message += "无法读取 .lumenmesh 文件\n";
        }
        return false;
    }
    if (bytes.size() < sizeof(lumenmesh::LumenMeshFileHeader)) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh 文件过短\n";
        }
        return false;
    }
    lumenmesh::LumenMeshFileHeader hdr {};
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    if (std::memcmp(hdr.magic, lumenmesh::MAGIC, 8) != 0) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh magic 无效\n";
        }
        return false;
    }
    if (hdr.version != lumenmesh::FILE_VERSION) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh 版本不支持\n";
        }
        return false;
    }
    constexpr std::uint32_t kExpectedStride =
        static_cast<std::uint32_t>(
            sizeof(lumen::render::PbrInterleavedVertex));
    if (hdr.vertex_stride != kExpectedStride) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh vertex_stride 与引擎不一致\n";
        }
        return false;
    }

    std::size_t off = sizeof(lumenmesh::LumenMeshFileHeader);
    const std::size_t vert_bytes =
        static_cast<std::size_t>(hdr.vertex_count) *
        static_cast<std::size_t>(hdr.vertex_stride);
    const std::size_t idx_bytes =
        static_cast<std::size_t>(hdr.index_count) * sizeof(std::uint32_t);
    const std::size_t prim_bytes =
        static_cast<std::size_t>(hdr.primitive_count) *
        sizeof(lumenmesh::LumenMeshPrimDesc);
    if (hdr.primitive_count == 0U) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh primitive_count 为 0\n";
        }
        return false;
    }
    if (off + vert_bytes + idx_bytes + prim_bytes > bytes.size()) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh 主体截断\n";
        }
        return false;
    }

    std::vector<lumen::render::PbrInterleavedVertex> verts(hdr.vertex_count);
    if (hdr.vertex_count > 0) {
        std::memcpy(verts.data(), bytes.data() + off, vert_bytes);
    }
    off += vert_bytes;

    std::vector<std::uint32_t> indices(hdr.index_count);
    if (hdr.index_count > 0) {
        std::memcpy(indices.data(), bytes.data() + off, idx_bytes);
    }
    off += idx_bytes;

    std::vector<lumenmesh::LumenMeshPrimDesc> prims(hdr.primitive_count);
    if (hdr.primitive_count > 0) {
        std::memcpy(prims.data(), bytes.data() + off, prim_bytes);
    }
    off += prim_bytes;

    for (std::uint32_t ni = 0; ni < hdr.node_count; ++ni) {
        if (off + sizeof(lumenmesh::LumenMeshNodeDesc) > bytes.size()) {
            if (error_message != nullptr) {
                *error_message += ".lumenmesh 节点表截断\n";
            }
            return false;
        }
        lumenmesh::LumenMeshNodeDesc nd {};
        std::memcpy(&nd, bytes.data() + off, sizeof(nd));
        off += sizeof(nd);
        if (off + nd.name_length > bytes.size()) {
            if (error_message != nullptr) {
                *error_message += ".lumenmesh 节点名截断\n";
            }
            return false;
        }
        std::string name;
        name.resize(nd.name_length);
        if (nd.name_length > 0) {
            std::memcpy(name.data(), bytes.data() + off, nd.name_length);
        }
        off += nd.name_length;

        lumen::scene::SceneNode sn {};
        sn.parent_index = nd.parent_index;
        sn.mesh_index = nd.mesh_index;
        sn.name = std::move(name);
        sn.local_transform = glm::make_mat4(nd.local_transform);
        out.scene_nodes.push_back(std::move(sn));
    }

    if (out.scene_nodes.empty()) {
        lumen::scene::SceneNode root {};
        root.parent_index = -1;
        root.name = "LumenMeshRoot";
        root.local_transform = glm::mat4(1.0F);
        root.mesh_index = 0;
        out.scene_nodes.push_back(root);
    }

    auto vbuf = std::make_unique<lumen::render::VertexBuffer>();
    if (!vbuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, verts.data(),
            verts.size() *
                sizeof(lumen::render::PbrInterleavedVertex))) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh 顶点缓冲上传失败\n";
        }
        return false;
    }
    auto ibuf = std::make_unique<lumen::render::IndexBuffer>();
    ibuf->set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!ibuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, indices.data(),
            indices.size() * sizeof(std::uint32_t))) {
        if (error_message != nullptr) {
            *error_message += ".lumenmesh 索引缓冲上传失败\n";
        }
        return false;
    }

    lumen::asset::AssetRegistry &assets = lumen::asset::AssetRegistry::instance();
    lumen::asset::MaterialRegistry &mat_registry = assets.materials();
    lumen::render::MaterialLoadDesc default_desc {};
    int max_mat = 0;
    for (const auto &pd : prims) {
        if (pd.material_index > max_mat) {
            max_mat = pd.material_index;
        }
    }
    if (max_mat < 0) {
        max_mat = 0;
    }
    out.materials.resize(static_cast<std::size_t>(max_mat) + 1U);
    for (std::size_t i = 0; i < out.materials.size(); ++i) {
        out.materials[i] = mat_registry.get_or_create(
            assets.textures(), ctx, transfer_queue, cmd_pool, default_desc);
    }

    const lumen::render::VertexLayout layout =
        lumen::render::make_vertex_layout_pbr_forward_tangent();

    lumen::asset::geometry::Mesh mesh {};
    for (const auto &pd : prims) {
        int mi = pd.material_index;
        if (mi < 0 ||
            mi >= static_cast<int>(out.materials.size())) {
            mi = 0;
        }
        const PrimLocalAabb ab =
            prim_aabb(verts, indices, pd.first_index, pd.index_count);
        mesh.primitives.push_back(lumen::asset::geometry::Primitive {
            .vertexByteOffset = 0,
            .firstIndex = pd.first_index,
            .indexCount = pd.index_count,
            .baseVertex = 0,
            .layout = layout,
            .material =
                &out.materials[static_cast<std::size_t>(mi)]->material,
            .localPivot = ab.valid ? ab.center : glm::vec3(0.0F),
            .localAabbHalfExtent = ab.valid ? ab.half : glm::vec3(0.0F),
        });
    }
    out.model.push_back(std::move(mesh));

    out.vertexBuffer = std::move(vbuf);
    out.indexBuffer = std::move(ibuf);
    out.statsVertexCount = verts.size();
    out.statsIndexCount = indices.size();

    LUMEN_LOG_DEBUG("load_lumenmesh: {} 顶点, {} 索引, {} primitive",
                    out.statsVertexCount, out.statsIndexCount,
                    hdr.primitive_count);
    return true;
}

} // namespace

std::unique_ptr<IMeshSceneImporter> make_lumenmesh_scene_importer() {
    return std::make_unique<LumenmeshSceneImporter>();
}

} // namespace lumen::asset
