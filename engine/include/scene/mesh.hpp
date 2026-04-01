/**
 * @defgroup lumen_scene_mesh Scene mesh
 * @brief glTF 风格网格资源：`Mesh`、`Primitive`、`Model` 及与 `RenderItem` /
 * ECS 的衔接
 *
 * @details
 * - **数据来源**：glTF 仅作导入；运行时以本模块类型为权威，不保留 Node 树。
 * - **绘制粒度**：一次 indexed draw 对应一个 `Primitive`；默认材质在
 * `Primitive::material`。按 primitive 独立控制时可用 ECS
 * `SubMeshRendererComponent`（见 `scene/submesh.hpp`）。
 * - **共享缓冲**：`MeshBuffer` 持有（非拥有）一对全局 VB/IB；各
 * `Primitive` 只记录 `vertex_byte_offset`、`first_index` 等，绘制时先 bind
 * 大缓冲再 `vkCmdDrawIndexed`（见 `note/几何buffer.md`）。
 */

/**
 * @file mesh.hpp
 * @brief glTF 风格网格：`Mesh` = `Primitive` 集合；`Primitive` = 在大缓冲中的
 * 一段索引范围 + 材质（不持有 `VkBuffer` 指针）
 *
 * @details
 * glTF：`Mesh` → 若干 `Primitive`，每个 primitive 独立材质与索引范围。
 *
 * @note
 * `Primitive::is_drawable()` 要求 `layout` 非空且 `index_count > 0`。实际
 * `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` 用的缓冲由 `MeshBuffer`
 * 或 `RenderItem` 提供。
 *
 * @see `scene/render_item.hpp`（展开为扁平 Draw）
 * @see `render/vertex_layout.hpp`（`Primitive::layout`）
 *
 * @ingroup lumen_scene_mesh
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/vec3.hpp>

#include "render/vertex_layout.hpp"

namespace lumen::render {
class VertexBuffer;
class IndexBuffer;
struct Material;
} // namespace lumen::render

namespace lumen::scene {

/**
 * @brief 一对共享 GPU 几何缓冲的非拥有视图（`note/几何buffer.md`）
 *
 * @details
 * 多个 `Mesh` / `Primitive` 共用同一块顶点与索引数据时，由加载器或资源持有者
 * 独占 `VertexBuffer` / `IndexBuffer`，此处仅保存指针供绘制路径 `vkCmdBind*`。
 */
struct MeshBuffer {
    const render::VertexBuffer *vertexBuffer {}; ///< 大顶点缓冲
    const render::IndexBuffer *indexBuffer {}; ///< 大索引缓冲

    [[nodiscard]] bool valid() const {
        return vertexBuffer != nullptr && indexBuffer != nullptr;
    }
};

/**
 * @brief 最小可绘制单元（≈ glTF primitive）
 *
 * @details
 * 仅描述在大缓冲中的位置，**不**持有 `VertexBuffer` / `IndexBuffer`。渲染时：
 * `vkCmdBindVertexBuffers(..., &vertex_byte_offset)`，
 * `vkCmdBindIndexBuffer(<MeshBuffer>.indexBuffer, ...)`，
 * `vkCmdDrawIndexed(..., first_index, base_vertex, ...)`。
 *
 * @note
 * `topology` 须与创建 `GraphicsPipeline` 时的图元拓扑一致（当前默认三角列表）。
 */
struct Primitive {
    std::uint64_t vertex_byte_offset {
        0
    }; ///< `vkCmdBindVertexBuffers` 的 `pOffsets[i]`（字节）
    std::uint32_t first_index { 0 }; ///< `vkCmdDrawIndexed` 的 `firstIndex`
    std::uint32_t index_count { 0 }; ///< `vkCmdDrawIndexed` 的 `indexCount`
    std::int32_t base_vertex {
        0
    }; ///< `vkCmdDrawIndexed` 的 `vertexOffset`（基顶点）

    render::VertexLayout
        layout {}; ///< 与管线 vertex input state 一致；`is_drawable()` 要求非空

    const render::Material
        *material {}; ///< 本 primitive 材质；`nullptr` 时由渲染路径选默认

    VkPrimitiveTopology topology {
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    }; ///< 图元拓扑

    /// mesh 空间下该 primitive 所用顶点的 **AABB 中心**，用于 SubMesh Gizmo 与
    /// 「几何中心」对齐；默认 (0,0,0)
    glm::vec3 local_pivot { 0.0F, 0.0F, 0.0F };
    /// 与 `local_pivot` 配套的 AABB 半尺寸 `(bmax - bmin) * 0.5`（mesh 空间）
    glm::vec3 local_aabb_half_extent { 0.0F, 0.0F, 0.0F };

    /**
     * @brief 是否具备绘制所需的最小条件（与缓冲绑定无关）
     */
    [[nodiscard]] bool is_drawable() const {
        return index_count > 0U && !layout.empty();
    }
};

/**
 * @brief 几何容器（≈ glTF 单个 mesh）
 *
 * @details
 * 仅持有 `primitives` 列表，不强制独占 GPU 缓冲；资源生命周期由加载器 /
 * 场景持有方管理。
 */
struct Mesh {
    std::vector<Primitive>
        primitives; ///< 子图元列表，顺序通常与 glTF primitive 顺序一致
};

/**
 * @brief 合并所有可绘制 primitive 的 AABB（`local_pivot ± local_aabb_half_extent`）
 *
 * @return 是否存在至少一个 `is_drawable()` 的 primitive
 */
inline bool drawable_mesh_local_bounds(const Mesh &mesh, glm::vec3 *out_center,
                                       glm::vec3 *out_half_extent) {
    if (out_center == nullptr || out_half_extent == nullptr) {
        return false;
    }
    constexpr float kHuge { 1.0e30F };
    glm::vec3 bmin(kHuge);
    glm::vec3 bmax(-kHuge);
    bool any { false };
    for (const Primitive &p : mesh.primitives) {
        if (!p.is_drawable()) {
            continue;
        }
        any = true;
        const glm::vec3 mn = p.local_pivot - p.local_aabb_half_extent;
        const glm::vec3 mx = p.local_pivot + p.local_aabb_half_extent;
        bmin = glm::min(bmin, mn);
        bmax = glm::max(bmax, mx);
    }
    if (!any) {
        return false;
    }
    *out_center = 0.5F * (bmin + bmax);
    *out_half_extent = 0.5F * (bmax - bmin);
    return true;
}

/**
 * @brief 模型（≈ glTF 资产内多个 mesh 的列表）
 *
 * @details
 * 实例变换由 `TransformComponent`、节点层级等表达；同一 `Mesh` 可被多处引用。
 *
 * @note
 * `gltf/gltf_scene_mesh.hpp` 的 `load_gltf_scene_mesh` 将默认场景合并为一块
 * CPU/GPU 几何；`Model[i]` 与 glTF `meshes[i]` 对齐，每个 `Mesh::primitives[j]`
 * 对应 glTF 的一个 primitive。全部 `Mesh` 共用 `GltfSceneMesh::geometry()`。
 */
using Model = std::vector<Mesh>;

} // namespace lumen::scene
