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

#include <glm/glm.hpp>

#include "render/material/material.hpp"

namespace lumen {
namespace core {

/**
 * @brief CPU 侧顶点（position / normal / UV），与 glTF 合并网格输出一致
 */
struct CpuVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
};

/**
 * @brief CPU 侧合并网格（统一索引），供上传 GPU 顶点/索引缓冲
 */
struct CpuMesh {
    std::vector<CpuVertex> vertices;
    std::vector<std::uint32_t> indices;
};

/**
 * @brief 合并网格上的一段索引范围（对应 glTF primitive）与材质索引
 */
struct PrimitiveSlice {
    std::uint32_t first_index {};
    std::uint32_t index_count {};
    /// glTF `materials` 数组下标；-1 表示无材质
    int material_index { -1 };
};

/**
 * @brief 加载 glTF / GLB 模型
 *
 * @details
 * 从 glTF 2.0 文件加载：
 * - 几何数据（合并为单一 CpuMesh）
 * - 可选：主材质摘要、`PrimitiveSlice` 列表、全量材质表（PBR 路径与因子）
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
 * 5. 合并所有 primitive → 单一 CpuMesh
 * 6. 记录 primitive 索引范围（如需要）
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
 * @param[out] out_main_material
 *   可选；非空时写入主材质描述：
 *   - 未请求 primitive 切片 → 场景遍历到的第一个材质
 *   - 请求切片 → 按三角面覆盖选出的主导材质
 *
 * @param[out] out_primitive_slices
 *   可选；非空时写入每个 primitive 的全局 `first_index` / `index_count` /
 *   `material_index`（须与 @a out_all_materials 同时提供）
 *
 * @param[out] out_all_materials
 *   可选；与 @a out_primitive_slices 成对使用，顺序与 glTF `materials` 一致
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
 * CpuMesh mesh;
 * lumen::render::MaterialLoadDesc mat;
 * load_gltf("model.glb", mesh, &mat);
 * @endcode
 *
 * 多材质：
 * @code
 * CpuMesh mesh;
 * lumen::render::MaterialLoadDesc main_mat;
 * std::vector<PrimitiveSlice> slices;
 * std::vector<lumen::render::MaterialLoadDesc> materials;
 *
 * load_gltf("model.glb", mesh, &main_mat, &slices, &materials);
 *
 * for (const auto& sub : slices) {
 *     bind(materials[sub.material_index]);
 *     drawIndexed(sub.first_index, sub.index_count);
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
 * 若提供 @a out_primitive_slices，必须同时提供 @a out_all_materials
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
bool load_gltf(std::string_view file_path, CpuMesh &out_mesh,
               render::MaterialLoadDesc *out_main_material = nullptr,
               std::vector<PrimitiveSlice> *out_primitive_slices = nullptr,
               std::vector<render::MaterialLoadDesc> *out_all_materials =
                   nullptr);

} // namespace core
} // namespace lumen
