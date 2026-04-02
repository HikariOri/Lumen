/**
 * @file asset/mesh_instance_ref.hpp
 * @brief 弱引用 `SceneMeshAsset` 内单个 `Mesh`，避免裸指针在卸载后悬空
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "asset/geometry/mesh_asset.hpp"
#include "scene/scene_mesh_asset.hpp"

namespace lumen::asset {

struct MeshInstanceRef {
    std::weak_ptr<lumen::scene::SceneMeshAsset> scene {};
    std::uint32_t meshIndex { 0 };

    struct Resolved {
        const lumen::asset::geometry::Mesh *mesh {};
        lumen::asset::geometry::MeshBuffer meshBuffer {};
    };

    [[nodiscard]] std::optional<Resolved> resolve() const {
        const std::shared_ptr<lumen::scene::SceneMeshAsset> sp = scene.lock();
        if (!sp) {
            return std::nullopt;
        }
        if (meshIndex >= sp->model.size()) {
            return std::nullopt;
        }
        return Resolved { .mesh = &sp->model[meshIndex],
                          .meshBuffer = sp->geometry() };
    }
};

struct SubMeshInstanceRef {
    MeshInstanceRef meshInstance {};
    std::uint32_t primitiveIndex { 0 };

    struct ResolvedPrim {
        const lumen::asset::geometry::Primitive *primitive {};
        lumen::asset::geometry::MeshBuffer meshBuffer {};
    };

    [[nodiscard]] std::optional<ResolvedPrim> resolve() const {
        const std::optional<MeshInstanceRef::Resolved> mr =
            meshInstance.resolve();
        if (!mr.has_value()) {
            return std::nullopt;
        }
        if (primitiveIndex >= mr->mesh->primitives.size()) {
            return std::nullopt;
        }
        return ResolvedPrim { .primitive = &mr->mesh->primitives[primitiveIndex],
                              .meshBuffer = mr->meshBuffer };
    }
};

} // namespace lumen::asset
