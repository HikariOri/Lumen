/**
 * @file gltf_loader.hpp
 * @brief glTF 2.0 / GLB 加载（Assimp），材质贴图路径由导入器解析（外链贴图；内嵌贴图路径形如 *N 时当前不填路径）
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/obj_loader.hpp"

namespace lumen {
namespace scene {
struct MaterialComponent;
}
namespace core {

/// 合并网格中一段连续索引，对应 glTF 一个 primitive 的材质（多材质模型必需分段绘制）。
struct GltfSubmeshRange {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = -1;
};

/**
 * @brief 从 .gltf（JSON）或 .glb（二进制）加载几何与 PBR 材质贴图路径
 * @param filePath 模型文件绝对路径或相对路径
 * @param outMesh 输出网格（合并默认场景中所有三角图元）
 * @param outMaterial 输出材质：无 outSubmeshes 时为场景中首个材质；若提供分段信息则为**按三角面数占优**的材质（便于 Inspector）
 * @param outSubmeshes 非空时写入每段 firstIndex / indexCount / materialIndex，且必须同时提供 outAllMaterials
 * @param outAllMaterials 非空时写入 glTF 中全部材质（下标与 model.materials 一致）
 * @return 成功返回 true
 */
bool load_gltf(std::string_view filePath, ObjMesh &outMesh,
               scene::MaterialComponent &outMaterial,
               std::vector<GltfSubmeshRange> *outSubmeshes = nullptr,
               std::vector<scene::MaterialComponent> *outAllMaterials = nullptr);

} // namespace core
} // namespace lumen
