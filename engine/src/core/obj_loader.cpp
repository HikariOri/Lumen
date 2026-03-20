/**
 * @file obj_loader.cpp
 * @brief OBJ 加载实现，基于 tinyobjloader
 */

#include "core/obj_loader.hpp"
#include "core/logger.hpp"

#include <array>
#include <cstddef>
#include <string>

#include <tiny_obj_loader.h>

namespace lumen {
namespace core {

namespace {

std::string get_mtl_base_dir(std::string_view filePath) {
    std::string path { filePath };
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

glm::vec3 get_vertex(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 3;
    if (idx < 0 || (base + 2) >= attrib.vertices.size()) {
        return glm::vec3(0.F);
    }
    return glm::vec3(static_cast<float>(attrib.vertices[base + 0]),
                     static_cast<float>(attrib.vertices[base + 1]),
                     static_cast<float>(attrib.vertices[base + 2]));
}

glm::vec3 get_normal(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 3;
    if (idx < 0 || (base + 2) >= attrib.normals.size()) {
        return glm::vec3(0.F, 1.F, 0.F); // fallback
    }
    return glm::vec3(static_cast<float>(attrib.normals[base + 0]),
                     static_cast<float>(attrib.normals[base + 1]),
                     static_cast<float>(attrib.normals[base + 2]));
}

glm::vec2 get_texcoord(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 2;
    if (idx < 0 || (base + 1) >= attrib.texcoords.size()) {
        return glm::vec2(0.F);
    }
    return glm::vec2(static_cast<float>(attrib.texcoords[base + 0]),
                     static_cast<float>(attrib.texcoords[base + 1]));
}

glm::vec3 compute_face_normal(const glm::vec3 &v0, const glm::vec3 &v1,
                              const glm::vec3 &v2) {
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 n = glm::cross(e1, e2);
    const float len = glm::length(n);
    if (len < 1e-8F) {
        return glm::vec3(0.F, 1.F, 0.F);
    }
    return n / len;
}

} // namespace

bool load_obj(std::string_view filePath, ObjMesh &outMesh) {
    outMesh.vertices.clear();
    outMesh.indices.clear();

    std::string path { filePath };
    std::string mtlDir = get_mtl_base_dir(path);

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.mtl_search_path = mtlDir;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        if (!reader.Error().empty()) {
            LUMEN_LOG_ERROR("OBJ 加载失败 {}: {}", path, reader.Error());
        }
        if (!reader.Warning().empty()) {
            LUMEN_LOG_WARN("OBJ 警告 {}: {}", path, reader.Warning());
        }
        return false;
    }

    if (!reader.Warning().empty()) {
        LUMEN_LOG_DEBUG("OBJ 警告 {}: {}", path, reader.Warning());
    }

    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();

    bool hasNormals = !attrib.normals.empty();
    bool hasTexcoords = !attrib.texcoords.empty();

    for (const auto &shape : shapes) {
        const auto &mesh = shape.mesh;
        const size_t numFaces = mesh.num_face_vertices.size();

        for (size_t f = 0; f < numFaces; ++f) {
            if (mesh.num_face_vertices[f] != 3) {
                continue; // 非三角形（triangulate 后应均为 3）
            }

            std::array<glm::vec3, 3> v;
            std::array<glm::vec3, 3> n;
            std::array<glm::vec2, 3> uv;

            for (int i = 0; i < 3; ++i) {
                const size_t idxOffset = f * 3 + static_cast<size_t>(i);
                const auto &idx = mesh.indices[idxOffset];
                v[static_cast<size_t>(i)] =
                    get_vertex(attrib, idx.vertex_index);
                n[static_cast<size_t>(i)] =
                    hasNormals ? get_normal(attrib, idx.normal_index)
                               : glm::vec3(0.F);
                uv[static_cast<size_t>(i)] =
                    hasTexcoords ? get_texcoord(attrib, idx.texcoord_index)
                                 : glm::vec2(0.F);
            }

            if (!hasNormals) {
                const glm::vec3 faceN = compute_face_normal(v[0], v[1], v[2]);
                n[0] = n[1] = n[2] = faceN;
            }

            const auto base = static_cast<uint32_t>(outMesh.vertices.size());
            for (int i = 0; i < 3; ++i) {
                const auto iu = static_cast<size_t>(i);
                outMesh.vertices.push_back(ObjVertex { v[iu], n[iu], uv[iu] });
                outMesh.indices.push_back(base + static_cast<uint32_t>(i));
            }
        }
    }

    LUMEN_LOG_DEBUG("OBJ 加载成功 {}: {} 顶点, {} 三角形", path,
                    outMesh.vertices.size(), outMesh.indices.size() / 3);
    return true;
}

} // namespace core
} // namespace lumen
