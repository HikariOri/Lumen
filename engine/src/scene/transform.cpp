/**
 * @file transform.cpp
 */

#include "scene/transform.hpp"

#include "scene/components.hpp"

#include <entt/entt.hpp>

namespace lumen::scene {

glm::mat4 world_matrix(const ::entt::registry &registry, ::entt::entity entity,
                       int max_depth) {
    if (!registry.valid(entity) || max_depth <= 0) {
        return glm::mat4(1.0f);
    }

    glm::mat4 local { 1.0f };
    if (const auto *tr = registry.try_get<TransformComponent>(entity)) {
        local = tr->matrix;
    }

    const auto *par = registry.try_get<ParentComponent>(entity);
    if (!par || par->parent == ::entt::null || !registry.valid(par->parent)) {
        return local;
    }

    return world_matrix(registry, par->parent, max_depth - 1) * local;
}

} // namespace lumen::scene
