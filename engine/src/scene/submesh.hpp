/**
 * @file submesh.hpp
 * @brief SubMesh 实体：按 `SubMeshRendererComponent` 收集 `RenderItem`，以及在节点下挂子实体
 *
 * @details
 * 与 `note/submesh.md` 一致：每个可绘制 primitive 对应一个子实体，共享 `Mesh*`，
 * 用 `primitiveIndex` 定位；世界矩阵为父链 × 局部 `TransformComponent`。
 * `MeshBuffer` 须与 `mesh` 数据来源一致（通常同一 `SceneMeshAsset::geometry()`，见
 * `scene/scene_mesh_asset.hpp`）。弱引用组件用本头文件的
 * `append_submesh_instance_ref_render_items` /
 * `append_mesh_instance_ref_renderer_render_items`（单实体展开见 `scene/render_item.hpp`）。
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
#include "scene/scene_mesh_asset.hpp"
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
inline void append_submesh_render_items(
    const lumen::asset::geometry::MeshBuffer &meshBuffer,
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
        const lumen::asset::geometry::Primitive &primitive =
            subMeshRenderer.mesh->primitives[subMeshRenderer.primitiveIndex];
        const glm::mat4 world = world_matrix(registry, entity);
        append_primitive_render_item(meshBuffer, primitive, world, pipelineKey,
                                     subMeshRenderer.materialOverride, outItems,
                                     entity);
    }
}

/**
 * @brief 遍历 `SubMeshInstanceRefRendererComponent`，解析后追加 `RenderItem`（资产已卸载则跳过）
 */
inline void append_submesh_instance_ref_render_items(
    const entt::registry &registry, std::uint64_t pipelineKey,
    std::vector<RenderItem> &outItems) {
    const auto view = registry.view<SubMeshInstanceRefRendererComponent>();
    for (const auto entity : view) {
        const auto &sr =
            view.get<SubMeshInstanceRefRendererComponent>(entity);
        const std::optional<lumen::asset::SubMeshInstanceRef::ResolvedPrim>
            rp = sr.submeshRef.resolve();
        if (!rp.has_value()) {
            continue;
        }
        const glm::mat4 world = world_matrix(registry, entity);
        append_primitive_render_item(rp->meshBuffer, *rp->primitive, world,
                                   pipelineKey, sr.materialOverride, outItems,
                                   entity);
    }
}

/**
 * @brief 遍历 `MeshInstanceRefRendererComponent`，整网按 primitive 展开
 */
inline void append_mesh_instance_ref_renderer_render_items(
    const entt::registry &registry, std::uint64_t pipelineKey,
    std::vector<RenderItem> &outItems) {
    const auto view = registry.view<MeshInstanceRefRendererComponent>();
    for (const auto entity : view) {
        const auto &mr =
            view.get<MeshInstanceRefRendererComponent>(entity);
        const glm::mat4 world = world_matrix(registry, entity);
        append_mesh_instance_ref_render_items(mr.meshRef, world, pipelineKey,
                                              outItems, entity);
    }
}

/**
 * @brief 在 `parent` 下为 `mesh` 的每个**可绘制** primitive 创建子实体
 *
 * @param[in] subentityNamePrefix 子实体 `TagComponent` 前缀，形如 `{prefix}_{i}`
 * @return 成功创建并挂到 `parent` 下的实体（`primitiveIndex` 与 `mesh.primitives` 下标一致）
 */
inline std::vector<entt::entity>
attach_submesh_children(Scene &scene, entt::entity parent,
                        const lumen::asset::geometry::Mesh &mesh,
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

/**
 * @brief 与 `attach_submesh_children` 相同拓扑，子实体挂 `SubMeshInstanceRefRendererComponent`
 */
inline std::vector<entt::entity> attach_submesh_children_with_instance_refs(
    Scene &scene, entt::entity parent, const lumen::asset::geometry::Mesh &mesh,
    std::weak_ptr<SceneMeshAsset> sceneWp, std::uint32_t meshIndex,
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
        registry.emplace<SubMeshInstanceRefRendererComponent>(
            sub,
            SubMeshInstanceRefRendererComponent{
                .submeshRef = { .meshInstance = { .scene = sceneWp,
                                                   .meshIndex = meshIndex },
                                .primitiveIndex = i },
                .materialOverride = nullptr });
        created.push_back(sub);
    }
    return created;
}

} // namespace lumen::scene
