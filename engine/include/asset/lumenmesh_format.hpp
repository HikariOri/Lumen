/**
 * @file asset/lumenmesh_format.hpp
 * @brief 运行时 `.lumenmesh` v1 布局（magic + 顶点/索引/primitive/节点表）
 *
 * @details
 * 文件布局（小端，`vertexStride` 须等于
 * `sizeof(lumen::render::PbrInterleavedVertex)`，见
 * `render/pbr_interleaved_vertex.hpp`）：
 * - `LumenMeshFileHeader`
 * - 顶点字节：`vertexCount * vertexStride`
 * - 索引：`indexCount` 个 `uint32_t`
 * - `LumenMeshPrimDesc` × `primitiveCount`
 * - `LumenMeshNodeDesc` × `nodeCount`（`name` 为 UTF-8，无 `\0`）
 */

#pragma once

#include <cstdint>

namespace lumen::asset::lumenmesh {

inline constexpr std::uint32_t FILE_VERSION { 1 };
inline constexpr char MAGIC[8] { 'L', 'U', 'M', 'M', 'S', 'H', '1', '\0' };

#pragma pack(push, 1)
struct LumenMeshFileHeader {
    char magic[8] {};
    std::uint32_t version { 0 };
    std::uint32_t vertex_count { 0 };
    std::uint32_t index_count { 0 };
    std::uint32_t primitive_count { 0 };
    std::uint32_t node_count { 0 };
    std::uint32_t vertex_stride { 0 };
    std::uint32_t reserved0 { 0 };
};

struct LumenMeshPrimDesc {
    std::uint32_t first_index { 0 };
    std::uint32_t index_count { 0 };
    std::int32_t material_index { 0 };
};

struct LumenMeshNodeDesc {
    std::int32_t parent_index { -1 };
    std::int32_t mesh_index { -1 };
    float local_transform[16] {};
    std::uint32_t name_length { 0 };
};
#pragma pack(pop)

} // namespace lumen::asset::lumenmesh
