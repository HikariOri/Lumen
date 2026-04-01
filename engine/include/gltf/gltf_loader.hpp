/**
 * @file gltf/gltf_loader.hpp
 * @brief glTF 2.0 / GLB 几何与 PBR 材质描述加载（tinygltf）
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "render/material/material.hpp"

namespace lumen {
namespace core {

struct CpuVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
};

struct CpuMesh {
    std::vector<CpuVertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// 大合并网格上的一段索引范围：对应 glTF 的一个 primitive
struct PrimitiveSlice {
    /// `tinygltf::Model::meshes` 下标，与 `scene::Model[i]` 对齐
    int meshIndex { -1 };
    std::uint32_t firstIndex {};
    std::uint32_t indexCount {};
    /// glTF `materials` 下标；-1 表示无材质
    int materialIndex { -1 };
};

/**
 * @brief 加载 glTF / GLB：合并为单一 CpuMesh，可选 primitive 切片与全材质表
 *
 * @param outMainMaterial 若非空：未请求切片时填第一个可用材质；请求切片时填三角面最多的材质
 * @param outPrimitiveSlices 与 @a outAllMaterials 须同时提供或同时省略
 * @param outGltfMeshCount 若非空：写入 glTF `meshes` 数组长度（`scene::Model` 槽位数量）
 */
bool load_gltf(std::string_view filePath, CpuMesh &outMesh,
               render::MaterialLoadDesc *outMainMaterial = nullptr,
               std::vector<PrimitiveSlice> *outPrimitiveSlices = nullptr,
               std::vector<render::MaterialLoadDesc> *outAllMaterials = nullptr,
               std::size_t *outGltfMeshCount = nullptr);

} // namespace core
} // namespace lumen
