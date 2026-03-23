/**
 * @file mesh_render.hpp
 * @brief 网格分段、材质槽与多子网格绘制顺序（`render-engine-features.md` 7a）
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/gltf_loader.hpp"
#include "scene/components.hpp"

namespace lumen {
namespace scene {

/// 合并缓冲中的一段连续索引，绑定一个材质槽（与 `core::GltfSubmeshRange` 语义一致）。
struct SubMeshSlice {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    /// `MeshMaterialSlotsComponent::slots` 下标
    std::uint32_t material_slot = 0;
};

/**
 * @brief 可绘制网格：多子网格时非空；空表示单段绘制（用实体上 `MaterialComponent`）
 */
struct MeshComponent {
    std::vector<SubMeshSlice> submeshes;
    /// 多材质时与 Inspector 同步的主槽（通常为三角面最多的材质）
    std::uint32_t dominant_material_slot = 0;
};

/**
 * @brief 材质实例：可选引用资产 id，resolved 为 GPU/着色器使用的最终参数
 */
struct MaterialInstance {
    std::string source_asset_id;
    MaterialComponent resolved;
};

/**
 * @brief 与子网格一一对应的材质槽列表（下标与 `SubMeshSlice::material_slot` 对应）
 */
struct MeshMaterialSlotsComponent {
    std::vector<MaterialInstance> slots;
};

void populate_mesh_from_gltf_data(
    MeshComponent &mesh, MeshMaterialSlotsComponent &slots,
    const std::vector<core::GltfSubmeshRange> &ranges,
    const std::vector<MaterialComponent> &materials);

/// 将实体上主 `MaterialComponent` 写回 `slots[dominant_material_slot]`（多材质编辑用）
void sync_material_component_to_dominant_slot(
    const MaterialComponent &primary, MeshComponent &mesh,
    MeshMaterialSlotsComponent *slots);

/**
 * @brief 按 Alpha/双面/材质哈希排序子网格索引，减少管线切换（同键连续）
 */
void build_submesh_batch_order(const MeshComponent &mesh,
                               const MeshMaterialSlotsComponent &slots,
                               std::vector<std::uint32_t> &out_submesh_indices);

[[nodiscard]] inline bool
mesh_uses_submesh_materials(const MeshComponent *mesh,
                            const MeshMaterialSlotsComponent *slots) {
    return mesh != nullptr && slots != nullptr && !mesh->submeshes.empty() &&
           !slots->slots.empty();
}

} // namespace scene
} // namespace lumen
