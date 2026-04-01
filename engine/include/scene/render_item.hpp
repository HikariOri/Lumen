/**
 * @file render_item.hpp
 * @brief 扁平渲染项：`Primitive` + 材质 + 实例矩阵 + 管线键
 *
 * @details
 * 由 `Mesh` 与世界（或局部）模型矩阵展开得到，供渲染循环按项绑定管线 / 描述集并调用
 * `vkCmdDrawIndexed`。运行时**不应**再遍历 glTF Node 树；变换应在写入 `model` 前结算完毕。
 *
 * 典型流程：持有与 `Mesh` 配套的 `MeshBuffer` →
 * `append_mesh_render_items(geo, *mr.mesh, world, pipeline_key, items)` → 排序或分桶 →
 * 按项 `vkCmdBind*`（可用 `item` 内缓冲指针）后 `vkCmdDrawIndexed`。
 *
 * @see `scene/mesh.hpp`
 * @see `scene/components.hpp`（`MeshRendererComponent`）
 *
 * @ingroup lumen_scene_mesh
 */

#pragma once

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>

#include "render/material/material.hpp"
#include "scene/mesh.hpp"

namespace lumen::scene {

/**
 * @brief 单次 draw 所需的逻辑打包（扁平队列元素）
 */
struct RenderItem {
    const Primitive *primitive {}; ///< 非空；索引范围、布局、拓扑（不含 VB/IB）
    /// 与 `primitive` 配套的大缓冲；由 `append_*_render_items` 从 `MeshBuffer` 填入
    const render::VertexBuffer *vertex_buffer {};
    const render::IndexBuffer *index_buffer {};
    const render::Material *material {}; ///< 可沿用 `primitive->material`，或由管线覆盖默认

    glm::mat4 model { 1.0F }; ///< 对象到世界的变换（写入 object UBO 等）

    std::uint64_t pipeline_key {}; ///< 排序 / 哈希用；`0` 表示调用方不使用

    [[nodiscard]] bool is_valid_for_draw() const {
        return primitive != nullptr && primitive->is_drawable() && vertex_buffer != nullptr &&
               index_buffer != nullptr;
    }
};

/**
 * @brief 将 `mesh` 中每个可绘制 primitive 追加为一条 `RenderItem`
 *
 * @param[in] geo            与 `mesh` 顶点/索引数据对应的大缓冲视图
 * @param[in] mesh           网格资源
 * @param[in] model          写入每项的模型矩阵（通常为世界矩阵）
 * @param[in] pipeline_key   写入每项的管线键
 * @param[out] out           输出容器；在末尾 **追加**，不清空原有元素
 *
 * @note
 * `geo` 无效时直接返回。跳过 `!prim.is_drawable()` 的项。
 */
inline void append_mesh_render_items(const MeshBuffer &geo, const Mesh &mesh,
                                     const glm::mat4 &model, std::uint64_t pipeline_key,
                                     std::vector<RenderItem> &out) {
    if (!geo.valid()) {
        return;
    }
    out.reserve(out.size() + mesh.primitives.size());
    for (const auto &prim : mesh.primitives) {
        if (!prim.is_drawable()) {
            continue;
        }
        RenderItem item {};
        item.primitive = &prim;
        item.vertex_buffer = geo.vertex_buffer;
        item.index_buffer = geo.index_buffer;
        item.material = prim.material;
        item.model = model;
        item.pipeline_key = pipeline_key;
        out.push_back(item);
    }
}

/**
 * @brief 将 `model` 中每个 `Mesh` 的可绘制 primitive 依次追加为 `RenderItem`
 */
inline void append_model_render_items(const MeshBuffer &geo, const Model &model,
                                      const glm::mat4 &world, std::uint64_t pipeline_key,
                                      std::vector<RenderItem> &out) {
    for (const Mesh &m : model) {
        append_mesh_render_items(geo, m, world, pipeline_key, out);
    }
}

} // namespace lumen::scene
