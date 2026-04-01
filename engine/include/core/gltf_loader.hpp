/**
 * @file gltf_loader.hpp
 * @brief glTF 2.0 / GLB 模型加载接口（基于 tinygltf）
 *
 * @details
 * 提供从 glTF（.gltf JSON）或 GLB（二进制）文件加载网格数据与 PBR
 * 材质信息的功能。
 *
 * 支持：
 * - 几何数据（positions / normals / UV）
 * - PBR 材质参数（BaseColor / MetallicRoughness 等）
 * - 贴图路径解析：
 *   - PNG / JPEG（通过 stb）
 *   - KTX / KTX2（通过 libktx）
 *
 * 输出数据适用于：
 * - Vulkan Vertex / Index Buffer
 * - 基于 PBR 的渲染流程（IBL / Forward / Deferred）
 *
 * @note
 * glTF 结构：
 *   Scene → Node → Mesh → Primitive
 *
 * 每个 primitive：
 *   - 独立材质
 *   - 独立 index range
 *
 * CPU 侧推荐的网格载体见 `scene/mesh.hpp`（`Mesh` / `Primitive` / `Model`）。
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/gltf_material.hpp"
#include "core/obj_loader.hpp"

namespace lumen {
namespace core {

/**
 * @brief 子网格索引范围（对应 glTF primitive）
 *
 * @details
 * glTF 中一个 mesh 可能包含多个 primitive，每个 primitive：
 * - 使用不同材质
 * - 对应一段连续的 index buffer
 *
 * 在 Vulkan 中需要：
 * - 每个 submesh 单独调用 vkCmdDrawIndexed
 *
 * @note
 * 若模型包含多个材质，必须使用 submesh 分段渲染
 */
struct GltfSubmeshRange {

    /** @brief 起始索引（Index Buffer offset） */
    std::uint32_t firstIndex = 0;

    /** @brief 索引数量（该 primitive 的 index count） */
    std::uint32_t indexCount = 0;

    /**
     * @brief 材质索引（对应 glTF materials 数组）
     * @note -1 表示无材质
     */
    int materialIndex = -1;
};

/**
 * @brief 加载 glTF / GLB 模型
 *
 * @details
 * 从 glTF 2.0 文件加载：
 * - 几何数据（合并为单一 ObjMesh）
 * - 材质信息（PBR）
 * - 子网格分段（可选）
 *
 * ============================================================
 * 数据处理流程
 * ============================================================
 *
 * 1. 解析 glTF 文件（tinygltf）
 * 2. 遍历默认场景（default scene）
 * 3. 收集所有 mesh / primitive
 * 4. 提取：
 *    - POSITION
 *    - NORMAL
 *    - TEXCOORD_0
 * 5. 合并所有 primitive → 单一 ObjMesh
 * 6. 记录 submesh range（如需要）
 *
 * ============================================================
 * 材质处理
 * ============================================================
 *
 * - 支持 PBR Metallic-Roughness 模型
 * - 解析贴图路径：
 *   - baseColorTexture
 *   - metallicRoughnessTexture
 *   - normalTexture
 *   - emissiveTexture
 *
 * ============================================================
 * 参数说明
 * ============================================================
 *
 * @param[in]  filePath
 *   glTF / GLB 文件路径（支持相对路径）
 *
 * @param[out] outMesh
 *   输出合并网格：
 *   - 包含所有 primitive 的顶点与索引
 *
 * @param[out] outMaterial
 *   输出主材质：
 *   - 若未请求 submesh → 使用场景第一个材质
 *   - 若使用 submesh → 选择“覆盖三角面最多”的材质
 *     （用于 Inspector 默认显示）
 *
 * @param[out] outSubmeshes
 *   可选：
 *   - 若非空 → 写入每个 primitive 的索引范围
 *   - 用于多材质渲染（多 draw call）
 *
 * @param[out] outAllMaterials
 *   可选：
 *   - 若非空 → 输出 glTF 中所有材质
 *   - 顺序与 glTF materials 数组一致
 *
 * ============================================================
 * 返回值
 * ============================================================
 *
 * @return
 * - true  加载成功
 * - false 加载失败（错误信息通过日志输出）
 *
 * ============================================================
 * 使用方式
 * ============================================================
 *
 * 单材质（简单场景）：
 * @code
 * ObjMesh mesh;
 * GltfMaterialData mat;
 * load_gltf("model.glb", mesh, mat);
 * @endcode
 *
 * 多材质（推荐）：
 * @code
 * ObjMesh mesh;
 * GltfMaterialData mainMat;
 * std::vector<GltfSubmeshRange> submeshes;
 * std::vector<GltfMaterialData> materials;
 *
 * load_gltf("model.glb", mesh, mainMat, &submeshes, &materials);
 *
 * for (const auto& sub : submeshes) {
 *     bind(materials[sub.materialIndex]);
 *     drawIndexed(sub.firstIndex, sub.indexCount);
 * }
 * @endcode
 *
 * ============================================================
 * 注意事项
 * ============================================================
 *
 * @note
 * - glTF 使用右手坐标系（Y-up）
 * - 数据通常已优化（无需像 OBJ 那样去重）
 *
 * @warning
 * - 若提供 outSubmeshes，必须同时提供 outAllMaterials
 * - 否则材质索引无意义
 *
 * @warning
 * - 未正确处理 submesh 将导致多材质模型渲染错误
 *
 * ============================================================
 * 扩展建议
 * ============================================================
 *
 * - 支持 tangent（normal mapping 必需）
 * - 支持 skinning（动画）
 * - 支持 node transform（层级变换）
 * - 支持 instancing
 */
bool load_gltf(
    std::string_view filePath, ObjMesh &outMesh,
    GltfMaterialData &outMaterial,
    std::vector<GltfSubmeshRange> *outSubmeshes = nullptr,
    std::vector<GltfMaterialData> *outAllMaterials = nullptr);

} // namespace core
} // namespace lumen
