/**
 * @file scene/scene_mesh_spawn.hpp
 * @brief 将 `SceneMeshAsset::scene_nodes` 实例化为 ECS 层级，并在含 mesh
 * 的节点上挂 SubMesh 子实体或整网 `MeshRendererComponent`（见
 * `SceneMeshSpawnOptions`）
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "asset/asset_handle.hpp"
#include "scene/components.hpp"
#include "scene/scene.hpp"
#include "scene/scene_mesh_asset.hpp"
#include "scene/submesh.hpp"

namespace lumen::scene {

struct SceneMeshSpawnResult {
    std::vector<entt::entity> node_entities {};
    std::uint32_t drawable_primitive_instances { 0 };
};

struct SceneMeshSpawnOptions {
    bool attach_submesh_children { true };
    std::weak_ptr<SceneMeshAsset> owning_scene {};
    /// 若有效，在含 mesh 的节点实体上写入 `SceneMeshAssetHandleComponent`
    lumen::asset::SceneMeshAssetHandle scene_mesh_handle {};
};

[[nodiscard]] inline std::uint32_t
count_drawable_primitives(const lumen::asset::geometry::Mesh &mesh) {
    std::uint32_t n { 0 };
    for (const lumen::asset::geometry::Primitive &p : mesh.primitives) {
        if (p.is_drawable()) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] inline SceneMeshSpawnResult
spawn_scene_mesh_hierarchy(Scene &scene, const SceneMeshAsset &asset,
                           std::string_view submeshPrefix = "SceneSub",
                           const SceneMeshSpawnOptions &options = {}) {
    SceneMeshSpawnResult result {};
    result.node_entities.reserve(asset.scene_nodes.size());
    const std::string prefixBase { submeshPrefix };

    for (std::size_t i = 0; i < asset.scene_nodes.size(); ++i) {
        const SceneNode &node = asset.scene_nodes[i];
        std::string tag =
            node.name.empty() ? ("Node_" + std::to_string(i)) : node.name;
        const entt::entity e = scene.create_entity(tag);
        result.node_entities.push_back(e);

        scene.registry().get<TransformComponent>(e).set_transform(
            node.local_transform);

        const int p = node.parent_index;
        if (p >= 0 &&
            static_cast<std::size_t>(p) < result.node_entities.size()) {
            (void)scene.set_parent(
                e, result.node_entities[static_cast<std::size_t>(p)]);
        }

        if (node.mesh_index >= 0 &&
            static_cast<std::size_t>(node.mesh_index) < asset.model.size()) {
            const lumen::asset::geometry::Mesh &mesh =
                asset.model[static_cast<std::size_t>(node.mesh_index)];
            result.drawable_primitive_instances +=
                count_drawable_primitives(mesh);
            const std::uint32_t meshU32 =
                static_cast<std::uint32_t>(node.mesh_index);
            const bool useRef = options.owning_scene.lock() != nullptr;
            if (options.attach_submesh_children) {
                const std::string subPrefix =
                    prefixBase + "_" + std::to_string(i);
                if (useRef) {
                    (void)attach_submesh_children_with_instance_refs(
                        scene, e, mesh, options.owning_scene, meshU32,
                        subPrefix);
                    if (options.scene_mesh_handle.valid()) {
                        scene.registry()
                            .emplace_or_replace<SceneMeshAssetHandleComponent>(
                                e, SceneMeshAssetHandleComponent {
                                       .handle = options.scene_mesh_handle });
                    }
                } else {
                    (void)attach_submesh_children(scene, e, mesh, subPrefix);
                }
            } else if (useRef) {
                scene.registry().emplace<MeshInstanceRefRendererComponent>(
                    e, MeshInstanceRefRendererComponent {
                           .meshRef = { .scene = options.owning_scene,
                                        .meshIndex = meshU32 } });
                if (options.scene_mesh_handle.valid()) {
                    scene.registry()
                        .emplace_or_replace<SceneMeshAssetHandleComponent>(
                            e, SceneMeshAssetHandleComponent {
                                   .handle = options.scene_mesh_handle });
                }
            } else {
                scene.registry().emplace<MeshRendererComponent>(
                    e, MeshRendererComponent { .mesh = &mesh });
            }
        }
    }

    return result;
}

} // namespace lumen::scene
