/**
 * @file asset/obj_mesh_scene_importer.cpp
 * @brief Wavefront OBJ → `SceneMeshAsset`（单 mesh、三角化、PBR 交错顶点）
 */

#include "asset/mesh_scene_importer.hpp"

#include "asset/asset_registry.hpp"
#include "core/log/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pbr_interleaved_vertex.hpp"
#include "render/resource/buffer.hpp"
#include "render/vertex_layout.hpp"
#include "scene/scene_mesh_asset.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace lumen::asset {
namespace {

[[nodiscard]] int fix_index(const int idx, const std::size_t count) {
    if (idx < 0) {
        return static_cast<int>(count) + idx;
    }
    return idx - 1;
}

[[nodiscard]] std::string trim_line(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

[[nodiscard]] bool parse_int_sv(const std::string_view sv, int &out) {
    if (sv.empty()) {
        return false;
    }
    try {
        out = std::stoi(std::string { sv });
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool parse_face_corner(const std::string &tok, int &vi, int &vti,
                                     int &vni) {
    vi = vti = vni = 0;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : tok) {
        if (c == '/') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    if (parts.empty() || parts[0].empty()) {
        return false;
    }
    if (!parse_int_sv(parts[0], vi)) {
        return false;
    }
    if (parts.size() >= 2 && !parts[1].empty()) {
        (void)parse_int_sv(parts[1], vti);
    }
    if (parts.size() >= 3 && !parts[2].empty()) {
        (void)parse_int_sv(parts[2], vni);
    }
    return true;
}

class ObjMeshSceneImporter final : public IMeshSceneImporter {
public:
    [[nodiscard]] bool import(lumen::render::Context &ctx, VkQueue transfer_queue,
                              lumen::render::CommandPool &cmd_pool,
                              const std::string_view path,
                              lumen::scene::SceneMeshAsset &out,
                              const lumen::scene::SceneMeshLoadOptions &opts,
                              std::string *error_message) const override;

    [[nodiscard]] bool
    supports_extension(const std::string_view ext_lower) const override {
        return ext_lower == ".obj";
    }
};

bool ObjMeshSceneImporter::import(lumen::render::Context &ctx,
                                  VkQueue transfer_queue,
                                  lumen::render::CommandPool &cmd_pool,
                                  const std::string_view path,
                                  lumen::scene::SceneMeshAsset &out,
                                  const lumen::scene::SceneMeshLoadOptions &opts,
                                  std::string *error_message) const {
    (void)opts;
    out = lumen::scene::SceneMeshAsset {};

    std::ifstream in(std::string { path });
    if (!in) {
        if (error_message != nullptr) {
            *error_message += "无法打开 OBJ 文件\n";
        }
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    std::vector<std::uint32_t> indices;
    std::vector<lumen::render::PbrInterleavedVertex> interleaved;

    std::string line;
    while (std::getline(in, line)) {
        line = trim_line(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        if (kw == "v") {
            glm::vec3 p {};
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (kw == "vn") {
            glm::vec3 n {};
            ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (kw == "vt") {
            glm::vec2 t {};
            ls >> t.x >> t.y;
            texcoords.push_back(t);
        } else if (kw == "f") {
            std::vector<std::string> corners;
            std::string tok;
            while (ls >> tok) {
                corners.push_back(tok);
            }
            if (corners.size() < 3) {
                continue;
            }
            const auto emit_tri = [&](const std::string &a, const std::string &b,
                                      const std::string &c) {
                int avi = 0;
                int avt = 0;
                int avn = 0;
                int bvi = 0;
                int bvt = 0;
                int bvn = 0;
                int cvi = 0;
                int cvt = 0;
                int cvn = 0;
                if (!parse_face_corner(a, avi, avt, avn) ||
                    !parse_face_corner(b, bvi, bvt, bvn) ||
                    !parse_face_corner(c, cvi, cvt, cvn)) {
                    return;
                }
                auto push_v = [&](int vi, int vti, int vni) {
                    const int pi = fix_index(vi, positions.size());
                    if (pi < 0 ||
                        static_cast<std::size_t>(pi) >= positions.size()) {
                        return;
                    }
                    lumen::render::PbrInterleavedVertex v {};
                    v.position = positions[static_cast<std::size_t>(pi)];
                    if (vni != 0) {
                        const int ni = fix_index(vni, normals.size());
                        if (ni >= 0 &&
                            static_cast<std::size_t>(ni) < normals.size()) {
                            v.normal = normals[static_cast<std::size_t>(ni)];
                        }
                    }
                    if (vti != 0) {
                        const int ti = fix_index(vti, texcoords.size());
                        if (ti >= 0 &&
                            static_cast<std::size_t>(ti) < texcoords.size()) {
                            v.uv = texcoords[static_cast<std::size_t>(ti)];
                        }
                    }
                    interleaved.push_back(v);
                    indices.push_back(
                        static_cast<std::uint32_t>(interleaved.size() - 1));
                };
                push_v(avi, avt, avn);
                push_v(bvi, bvt, bvn);
                push_v(cvi, cvt, cvn);
            };
            emit_tri(corners[0], corners[1], corners[2]);
            for (std::size_t k = 3; k < corners.size(); ++k) {
                emit_tri(corners[0], corners[k - 1], corners[k]);
            }
        }
    }

    if (interleaved.empty() || indices.empty()) {
        if (error_message != nullptr) {
            *error_message += "OBJ 无有效三角几何\n";
        }
        return false;
    }

    lumen::render::compute_pbr_mesh_tangents(interleaved, indices);

    auto vbuf = std::make_unique<lumen::render::VertexBuffer>();
    if (!vbuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, interleaved.data(),
            interleaved.size() * sizeof(lumen::render::PbrInterleavedVertex))) {
        if (error_message != nullptr) {
            *error_message += "OBJ 顶点缓冲上传失败\n";
        }
        return false;
    }
    auto ibuf = std::make_unique<lumen::render::IndexBuffer>();
    ibuf->set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!ibuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, indices.data(),
            indices.size() * sizeof(std::uint32_t))) {
        if (error_message != nullptr) {
            *error_message += "OBJ 索引缓冲上传失败\n";
        }
        return false;
    }

    lumen::asset::AssetRegistry &assets = lumen::asset::AssetRegistry::instance();
    lumen::asset::MaterialRegistry &mat_registry = assets.materials();
    lumen::render::MaterialLoadDesc default_desc {};
    out.materials.push_back(mat_registry.get_or_create(
        assets.textures(), ctx, transfer_queue, cmd_pool, default_desc));

    const lumen::render::VertexLayout layout =
        lumen::render::make_vertex_layout_pbr_forward_tangent();

    lumen::asset::geometry::Mesh mesh {};
    mesh.primitives.push_back(lumen::asset::geometry::Primitive {
        .vertexByteOffset = 0,
        .firstIndex = 0,
        .indexCount = static_cast<std::uint32_t>(indices.size()),
        .baseVertex = 0,
        .layout = layout,
        .material = &out.materials[0]->material,
    });
    out.model.push_back(std::move(mesh));

    out.vertexBuffer = std::move(vbuf);
    out.indexBuffer = std::move(ibuf);
    out.statsVertexCount = interleaved.size();
    out.statsIndexCount = indices.size();

    lumen::scene::SceneNode root {};
    root.parent_index = -1;
    root.name = "ObjRoot";
    root.local_transform = glm::mat4(1.0F);
    root.mesh_index = 0;
    out.scene_nodes.push_back(root);

    LUMEN_LOG_DEBUG("load_obj: {} 顶点, {} 索引", out.statsVertexCount,
                    out.statsIndexCount);
    return true;
}

} // namespace

std::unique_ptr<IMeshSceneImporter> make_obj_mesh_scene_importer() {
    return std::make_unique<ObjMeshSceneImporter>();
}

} // namespace lumen::asset
