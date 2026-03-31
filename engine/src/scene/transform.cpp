/**
 * @file transform.cpp
 */

#include "scene/transform.hpp"

#include "scene/components.hpp"
#include "scene/id_lookup.hpp"

#include <entt/entt.hpp>

namespace lumen::scene {

glm::mat4 world_matrix(const ::entt::registry &registry, ::entt::entity entity,
                       int max_depth) {
    if (!registry.valid(entity) || max_depth <= 0) {
        return { 1.0F };
    }

    glm::mat4 local { 1.0F };
    if (const auto *tr = registry.try_get<TransformComponent>(entity)) {
        local = tr->get_transform();
    }

    const auto *rel = registry.try_get<RelationshipComponent>(entity);
    if (!rel || rel->parent == lumen::core::INVALID_ID) {
        return local;
    }
    const auto parent_ent = find_entity_with_id(registry, rel->parent);
    if (!parent_ent || !registry.valid(*parent_ent)) {
        return local;
    }

    return world_matrix(registry, *parent_ent, max_depth - 1) * local;
}

} // namespace lumen::scene
