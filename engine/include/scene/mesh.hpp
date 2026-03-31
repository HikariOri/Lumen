/**
 * @file mesh.hpp
 * @brief glTF 风格网格：Mesh = Primitive 集合；Primitive = 一次 indexed draw + 材质
 *
 * glTF：`Mesh` → 若干 `Primitive`，每个 primitive 有独立材质与索引范围；多个 primitive
 * 可共享同一块顶点/索引缓冲（仅 offset / firstIndex 不同），加载时不应盲目拷贝缓冲。
 *
 * 渲染单位是 Primitive，而非整个 Mesh。
 */

#pragma once

#include <cstdint>
#include <vector>

namespace lumen::render {
class VertexBuffer;
class IndexBuffer;
} // namespace lumen::render

namespace lumen::scene {

struct PBRMaterial;

/**
 * @brief 最小可绘制单元（≈ glTF primitive）：一次 `vkCmdDrawIndexed`
 *
 * `vertex_buffer` / `index_buffer` 可由同一 Mesh 内多个 Primitive
 * 指向相同实例；通过 `vertex_byte_offset`、`first_index` 区分子范围。
 */
struct Primitive {
    const render::VertexBuffer *vertex_buffer {};
    const render::IndexBuffer *index_buffer {};
    /// `vkCmdBindVertexBuffers` 的 `pOffsets`（字节）
    std::uint64_t vertex_byte_offset { 0 };
    /// `vkCmdDrawIndexed` 的 `firstIndex`
    std::uint32_t first_index { 0 };
    std::uint32_t index_count { 0 };
    /**
     * 本 primitive 使用的材质；空则由渲染路径决定默认材质。
     *
     * @note 与 Vulkan 一致：不同 `PBRMaterial` 通常需切换 pipeline / descriptor
     * set（或 bindless 索引）；仅几何循环不够时要在渲染器中按材质绑定。
     */
    const PBRMaterial *material {};

    [[nodiscard]] bool is_drawable() const {
        return vertex_buffer != nullptr && index_buffer != nullptr &&
               index_count > 0U;
    }
};

/**
 * @brief 几何容器（≈ glTF mesh）：仅持有 primitive 列表，不强制独占 GPU 资源
 */
struct Mesh {
    std::vector<Primitive> primitives;
};

/**
 * @brief 模型（≈ glTF 中与 node 解耦的 mesh 集合）：多 mesh 资产
 *
 * 实例化与变换由场景中的 Node / `TransformComponent` 等表达，一个 mesh
 * 可被多个节点引用。
 */
struct Model {
    std::vector<Mesh> meshes;
};

} // namespace lumen::scene
