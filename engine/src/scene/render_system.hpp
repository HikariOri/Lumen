/**
 * @file render_system.hpp
 * @brief 从 ECS 收集 `RenderItem`、可选按管线/材质排序（无 Vulkan）
 *
 * @details
 * 对应 `note/RenderSystem.md` 中「Collect → Build → Sort → Submit」里的
 * **Collect** 与
 * **Sort**；**Submit**（`vkCmd*`、RenderPass、Framebuffer）仍由宿主在 Pass
 * 内完成。 多 Pass（如主色 + Pick）可 **共用** 同一次 `collect_render_items`
 * 的结果，仅在各自 Pass 内绑定目标后绘制。
 *
 * 后续可扩展工业级 `RenderQueue`、位域 `SortingKey`、opaque/transparent
 * 多队列等；本阶段保持 扁平 `std::vector<RenderItem>` 与简单排序键。
 *
 * @see `scene/submesh.hpp`、`scene/render_item.hpp`
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <vector>

#include <entt/entt.hpp>

#include "asset/geometry/mesh_asset.hpp"
#include "scene/render_item.hpp"
#include "scene/submesh.hpp"
#include "scene/transform.hpp"

namespace lumen::scene {

/// `RenderCollectOptions::source_mask` 位：SubMeshInstanceRef（默认示例路径）
inline constexpr std::uint32_t k_render_collect_submesh_instance_ref = 1U << 0;
/// 与 `append_submesh_render_items` 一致，须设置 `shared_mesh_buffer`
inline constexpr std::uint32_t k_render_collect_submesh_renderer = 1U << 1;
/// 整网 `MeshRendererComponent`，须设置 `shared_mesh_buffer`
inline constexpr std::uint32_t k_render_collect_mesh_renderer = 1U << 2;
inline constexpr std::uint32_t k_render_collect_mesh_instance_ref_renderer =
    1U << 3;

/**
 * @brief 收集策略：管线键、来源掩码、是否与已有 `out` 追加
 */
struct RenderCollectOptions {
    std::uint64_t pipeline_key { 0 };
    /// 位或 `k_render_collect_*`；默认仅 SubMeshInstanceRef
    std::uint32_t source_mask { k_render_collect_submesh_instance_ref };
    /// 为 true 时在 `out` 末尾追加；默认 false 会先 `out.clear()`
    bool append_only { false };
    /// SubMeshRenderer / MeshRenderer 与场景大 VB/IB
    /// 一致时传入；否则对应来源被跳过
    const lumen::asset::geometry::MeshBuffer *shared_mesh_buffer {};
};

/**
 * @brief 遍历 `MeshRendererComponent`，按整网 primitive 展开并追加 `RenderItem`
 */
inline void append_mesh_renderer_render_items(
    const lumen::asset::geometry::MeshBuffer &meshBuffer,
    const entt::registry &registry, std::uint64_t pipeline_key,
    std::vector<RenderItem> &out_items) {
    if (!meshBuffer.valid()) {
        return;
    }
    const auto view = registry.view<MeshRendererComponent>();
    for (const auto entity : view) {
        const auto &mr = view.get<MeshRendererComponent>(entity);
        if (mr.mesh == nullptr) {
            continue;
        }
        const glm::mat4 world = world_matrix(registry, entity);
        append_mesh_render_items(meshBuffer, *mr.mesh, world, pipeline_key,
                                 out_items, entity);
    }
}

/**
 * @brief 按 `opts.source_mask` 依次调用现有 `append_*_render_items`
 */
inline void collect_render_items(const entt::registry &reg,
                                 std::vector<RenderItem> &out,
                                 const RenderCollectOptions &opts = {}) {
    if (!opts.append_only) {
        out.clear();
    }
    const std::uint32_t m = opts.source_mask;
    if ((m & k_render_collect_submesh_instance_ref) != 0U) {
        append_submesh_instance_ref_render_items(reg, opts.pipeline_key, out);
    }
    if ((m & k_render_collect_submesh_renderer) != 0U) {
        if (opts.shared_mesh_buffer != nullptr &&
            opts.shared_mesh_buffer->valid()) {
            append_submesh_render_items(*opts.shared_mesh_buffer, reg,
                                        opts.pipeline_key, out);
        }
    }
    if ((m & k_render_collect_mesh_renderer) != 0U) {
        if (opts.shared_mesh_buffer != nullptr &&
            opts.shared_mesh_buffer->valid()) {
            append_mesh_renderer_render_items(*opts.shared_mesh_buffer, reg,
                                              opts.pipeline_key, out);
        }
    }
    if ((m & k_render_collect_mesh_instance_ref_renderer) != 0U) {
        append_mesh_instance_ref_renderer_render_items(reg, opts.pipeline_key,
                                                       out);
    }
}

/**
 * @brief 按 pipeline → 材质 →
 * 几何缓冲减少状态切换；同键下顺序由指针全序保证稳定
 */
inline void
sort_render_items_for_minimal_state_change(std::vector<RenderItem> &items) {
    std::ranges::sort(items, [](const RenderItem &a, const RenderItem &b) {
        const auto pa = std::tie(a.pipelineKey, a.material, a.vertexBuffer,
                                 a.indexBuffer, a.primitive, a.pick_entity);
        const auto pb = std::tie(b.pipelineKey, b.material, b.vertexBuffer,
                                 b.indexBuffer, b.primitive, b.pick_entity);
        if (pa != pb) {
            return pa < pb;
        }
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                const float x = a.model[c][r];
                const float y = b.model[c][r];
                if (x < y) {
                    return true;
                }
                if (y < x) {
                    return false;
                }
            }
        }
        return false;
    });
}

} // namespace lumen::scene
