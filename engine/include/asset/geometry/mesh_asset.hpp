/**
 * @file asset/geometry/mesh_asset.hpp
 * @brief 格式无关的运行时网格：`Mesh`、`Primitive`、`Model`、`MeshBuffer`
 *
 * @details
 * 与 glTF primitive/mesh 概念对齐，但 **不** 表示数据来源；可由 glTF、OBJ、
 * `.lumenmesh` 等 Importer 填充。顶点处于 mesh 局部空间；层级由
 * `SceneMeshAsset::scene_nodes` 与 `spawn_scene_mesh_hierarchy` 写入 ECS。
 */

#pragma once

#include <cstdint>
#include <vector>

#include "render/vulkan.hpp"

#include <glm/vec3.hpp>

#include "render/vertex_layout.hpp"

namespace lumen::render {
class VertexBuffer;
class IndexBuffer;
struct Material;
} // namespace lumen::render

namespace lumen::asset::geometry {

struct MeshBuffer {
    const render::VertexBuffer *vertexBuffer {};
    const render::IndexBuffer *indexBuffer {};

    [[nodiscard]] bool valid() const {
        return vertexBuffer != nullptr && indexBuffer != nullptr;
    }
};

struct Primitive {
    std::uint64_t vertexByteOffset { 0 };
    std::uint32_t firstIndex { 0 };
    std::uint32_t indexCount { 0 };
    std::int32_t baseVertex { 0 };

    render::VertexLayout layout {};
    const render::Material *material {};

    vk::PrimitiveTopology topology { vk::PrimitiveTopology::eTriangleList };

    glm::vec3 localPivot { 0.0F, 0.0F, 0.0F };
    glm::vec3 localAabbHalfExtent { 0.0F, 0.0F, 0.0F };

    [[nodiscard]] bool is_drawable() const {
        return indexCount > 0U && !layout.empty();
    }
};

struct Mesh {
    std::vector<Primitive> primitives;
};

inline bool drawable_mesh_local_bounds(const Mesh &mesh, glm::vec3 *out_center,
                                       glm::vec3 *out_half_extent) {
    if (out_center == nullptr || out_half_extent == nullptr) {
        return false;
    }
    constexpr float BOUNDS_SENTINEL { 1.0e30F };
    glm::vec3 boxMin(BOUNDS_SENTINEL);
    glm::vec3 boxMax(-BOUNDS_SENTINEL);
    bool hasAny { false };
    for (const Primitive &p : mesh.primitives) {
        if (!p.is_drawable()) {
            continue;
        }
        hasAny = true;
        const glm::vec3 minCorner = p.localPivot - p.localAabbHalfExtent;
        const glm::vec3 maxCorner = p.localPivot + p.localAabbHalfExtent;
        boxMin = glm::min(boxMin, minCorner);
        boxMax = glm::max(boxMax, maxCorner);
    }
    if (!hasAny) {
        return false;
    }
    *out_center = 0.5F * (boxMin + boxMax);
    *out_half_extent = 0.5F * (boxMax - boxMin);
    return true;
}

using Model = std::vector<Mesh>;

} // namespace lumen::asset::geometry
