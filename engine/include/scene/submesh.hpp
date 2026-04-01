/**
 * @file submesh.hpp
 * @brief SubMesh 实体：按 `SubMeshRendererComponent` 收集 `RenderItem`，以及在节点下挂子实体
 *
 * @details
 * 与 `note/submesh.md` 一致：每个可绘制 primitive 对应一个子实体，共享 `Mesh*`，
 * 用 `primitiveIndex` 定位；世界矩阵为父链 × 局部 `TransformComponent`。
 * `MeshBuffer` 须与 `mesh` 数据来源一致（通常同一 `GltfSceneMesh::geometry()`，见
 * `gltf/gltf_scene_mesh.hpp`）。
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "scene/components.hpp"
#include "scene/render_item.hpp"
#include "scene/scene.hpp"
#include "scene/transform.hpp"

namespace lumen::scene {

/**
 * @brief 遍历带 `SubMeshRendererComponent` 的实体，追加 `RenderItem`
 *
 * @param[in] meshBuffer     与这些 `Mesh` 配套的大缓冲（调用方保证一致）
 * @param[in] registry       场景注册表
 * @param[in] pipelineKey    写入每项的管线键
 * @param[out] outItems      在末尾追加
 */
inline void append_submesh_render_items(const MeshBuffer &meshBuffer,
                                        const entt::registry &registry,
                                        std::uint64_t pipelineKey,
                                        std::vector<RenderItem> &outItems) {
    const auto view = registry.view<SubMeshRendererComponent>();
    for (const auto entity : view) {
        const auto &subMeshRenderer =
            view.get<SubMeshRendererComponent>(entity);
        if (subMeshRenderer.mesh == nullptr) {
            continue;
        }
        if (subMeshRenderer.primitiveIndex >=
            subMeshRenderer.mesh->primitives.size()) {
            continue;
        }
        const Primitive &primitive =
            subMeshRenderer.mesh->primitives[subMeshRenderer.primitiveIndex];
        const glm::mat4 world = world_matrix(registry, entity);
        append_primitive_render_item(meshBuffer, primitive, world, pipelineKey,
                                     subMeshRenderer.materialOverride, outItems,
                                     entity);
    }
}

/**
 * @brief 在 `parent` 下为 `mesh` 的每个**可绘制** primitive 创建子实体
 *
 * @param[in] subentityNamePrefix 子实体 `TagComponent` 前缀，形如 `{prefix}_{i}`
 * @return 成功创建并挂到 `parent` 下的实体（`primitiveIndex` 与 `mesh.primitives` 下标一致）
 */
inline std::vector<entt::entity>
attach_submesh_children(Scene &scene, entt::entity parent, const Mesh &mesh,
                        std::string_view subentityNamePrefix = "SubMesh") {
    std::vector<entt::entity> created {};
    entt::registry &registry = scene.registry();
    if (!registry.valid(parent)) {
        return created;
    }
    const std::string namePrefix { subentityNamePrefix };
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(mesh.primitives.size());
         ++i) {
        if (!mesh.primitives[i].is_drawable()) {
            continue;
        }
        const std::string tag = namePrefix + "_" + std::to_string(i);
        const entt::entity sub = scene.create_entity(tag);
        if (!scene.set_parent(sub, parent)) {
            scene.destroy_entity(sub);
            continue;
        }
        registry.emplace<SubMeshRendererComponent>(
            sub, SubMeshRendererComponent{ .mesh = &mesh,
                                           .primitiveIndex = i,
                                           .materialOverride = nullptr });
        created.push_back(sub);
    }
    return created;
}

} // namespace lumen::scene
