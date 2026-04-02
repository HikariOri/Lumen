/**
 * @file render_item.hpp
 * @brief 扁平渲染项：`Primitive` + 材质 + 实例矩阵 + 管线键
 *
 * @details
 * 由 `Mesh` 与世界（或局部）模型矩阵展开得到，供渲染循环按项绑定管线 / 描述集并调用
 * `vkCmdDrawIndexed`。运行时**不应**再遍历 glTF Node 树；变换应在写入 `model` 前结算完毕。
 *
 * 典型流程：持有与 `Mesh` 配套的 `MeshBuffer` →
 * `append_mesh_render_items(meshBuffer, *mr.mesh, world, pipelineKey, items)` → 排序或分桶 →
 * 按项 `vkCmdBind*`（可用 `item` 内缓冲指针）后 `vkCmdDrawIndexed`。
 *
 * @see `asset/geometry/mesh_asset.hpp`
 * @see `scene/components.hpp`（`MeshRendererComponent`、`SubMeshRendererComponent`）
 * @see `scene/submesh.hpp`（按实体收集 SubMesh）
 *
 * @ingroup lumen_scene_mesh
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <entt/entt.hpp>
#include <glm/mat4x4.hpp>

#include "asset/mesh_instance_ref.hpp"
#include "asset/geometry/mesh_asset.hpp"
#include "render/material/material.hpp"

namespace lumen::scene {

/**
 * @brief 单次 draw 所需的逻辑打包（扁平队列元素）
 */
struct RenderItem {
    const lumen::asset::geometry::Primitive *primitive {}; ///< 非空；索引范围、布局、拓扑（不含 VB/IB）
    /// 与 `primitive` 配套的大缓冲；由 `append_*_render_items` 从 `MeshBuffer` 填入
    const render::VertexBuffer *vertexBuffer {};
    const render::IndexBuffer *indexBuffer {};
    const render::Material *material {}; ///< 可沿用 `primitive->material`，或由管线覆盖默认

    glm::mat4 model { 1.0F }; ///< 对象到世界的变换（写入 object UBO 等）

    std::uint64_t pipelineKey {}; ///< 排序 / 哈希用；`0` 表示调用方不使用

    /// ID Map 拾取：子实体写入自身；`entt::null` 表示不参与 ID Pass
    entt::entity pick_entity { entt::null };

    [[nodiscard]] bool is_valid_for_draw() const {
        return primitive != nullptr && primitive->is_drawable() && vertexBuffer != nullptr &&
               indexBuffer != nullptr;
    }
};

/**
 * @brief 将单条可绘制 `Primitive` 追加为 `RenderItem`
 *
 * @param[in] materialOverride 非空时覆盖 `primitive.material`
 * @param[in] pick_entity      ID Map 用；`entt::null` 则跳过 ID Pass 绘制
 *
 * @note
 * `meshBuffer` 无效或 `!primitive.is_drawable()` 时直接返回。
 */
inline void append_primitive_render_item(
    const lumen::asset::geometry::MeshBuffer &meshBuffer,
    const lumen::asset::geometry::Primitive &primitive,
                                         const glm::mat4 &model,
                                         std::uint64_t pipelineKey,
                                         const render::Material *materialOverride,
                                         std::vector<RenderItem> &outItems,
                                         entt::entity pick_entity = entt::null) {
    if (!meshBuffer.valid() || !primitive.is_drawable()) {
        return;
    }
    RenderItem item {};
    item.primitive = &primitive;
    item.vertexBuffer = meshBuffer.vertexBuffer;
    item.indexBuffer = meshBuffer.indexBuffer;
    item.material =
        materialOverride != nullptr ? materialOverride : primitive.material;
    item.model = model;
    item.pipelineKey = pipelineKey;
    item.pick_entity = pick_entity;
    outItems.push_back(item);
}

/**
 * @brief 将 `mesh` 中每个可绘制 primitive 追加为一条 `RenderItem`
 *
 * @param[in] meshBuffer     与 `mesh` 顶点/索引数据对应的大缓冲视图
 * @param[in] mesh           网格资源
 * @param[in] model          写入每项的模型矩阵（通常为世界矩阵）
 * @param[in] pipelineKey    写入每项的管线键
 * @param[out] outItems      输出容器；在末尾 **追加**，不清空原有元素
 *
 * @note
 * `meshBuffer` 无效时直接返回。跳过 `!primitive.is_drawable()` 的项。
 */
inline void append_mesh_render_items(
    const lumen::asset::geometry::MeshBuffer &meshBuffer,
    const lumen::asset::geometry::Mesh &mesh,
                                     const glm::mat4 &model,
                                     std::uint64_t pipelineKey,
                                     std::vector<RenderItem> &outItems,
                                     entt::entity pick_entity = entt::null) {
    if (!meshBuffer.valid()) {
        return;
    }
    outItems.reserve(outItems.size() + mesh.primitives.size());
    for (const auto &primitive : mesh.primitives) {
        append_primitive_render_item(meshBuffer, primitive, model, pipelineKey,
                                   nullptr, outItems, pick_entity);
    }
}

/**
 * @brief 将 `model` 中每个 `Mesh` 的可绘制 primitive 依次追加为 `RenderItem`
 */
inline void append_model_render_items(
    const lumen::asset::geometry::MeshBuffer &meshBuffer,
    const lumen::asset::geometry::Model &model,
                                      const glm::mat4 &world,
                                      std::uint64_t pipelineKey,
                                      std::vector<RenderItem> &outItems,
                                      entt::entity pick_entity = entt::null) {
    for (const lumen::asset::geometry::Mesh &meshPart : model) {
        append_mesh_render_items(meshBuffer, meshPart, world, pipelineKey,
                                 outItems, pick_entity);
    }
}

/**
 * @brief 由 `MeshInstanceRef` 解析后展开 `RenderItem`；场景资产已卸载时无操作
 */
inline void append_mesh_instance_ref_render_items(
    const lumen::asset::MeshInstanceRef &ref, const glm::mat4 &world,
    std::uint64_t pipelineKey, std::vector<RenderItem> &outItems,
    entt::entity pick_entity = entt::null) {
    const std::optional<lumen::asset::MeshInstanceRef::Resolved> r =
        ref.resolve();
    if (!r.has_value()) {
        return;
    }
    append_mesh_render_items(r->meshBuffer, *r->mesh, world, pipelineKey,
                             outItems, pick_entity);
}

} // namespace lumen::scene
