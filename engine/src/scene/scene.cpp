/**
 * @file scene.cpp
 */

#include "scene/scene.hpp"

#include "scene/id_lookup.hpp"

#include <entt/entt.hpp>

namespace lumen::scene {
namespace {

[[nodiscard]] bool parent_is_under_child(const ::entt::registry &reg,
                                         ::entt::entity child,
                                         ::entt::entity parent) {
    if (parent == ::entt::null || child == ::entt::null || !reg.valid(parent)) {
        return false;
    }
    ::entt::entity x = parent;
    for (int i = 0; i < 64 && reg.valid(x); ++i) {
        if (x == child) {
            return true;
        }
        const auto *rel = reg.try_get<RelationshipComponent>(x);
        if (!rel || rel->parent == lumen::core::INVALID_ID) {
            return false;
        }
        const auto next = find_entity_with_id(reg, rel->parent);
        if (!next || !reg.valid(*next)) {
            return false;
        }
        x = *next;
    }
    return false;
}

} // namespace

::entt::entity Scene::create_entity(std::string_view name) {
    const ::entt::entity e = reg_.create();
    reg_.emplace<IDComponent>(
        e, IDComponent{ .id = lumen::core::generate_random_id() });
    reg_.emplace<TransformComponent>(e);
    reg_.emplace<TagComponent>(e, std::string(name));
    return e;
}

bool Scene::set_parent(::entt::entity child, ::entt::entity parent) {
    if (!reg_.valid(child)) {
        return false;
    }
    if (parent != ::entt::null && !reg_.valid(parent)) {
        return false;
    }
    if (parent == child) {
        return false;
    }
    if (parent != ::entt::null && parent_is_under_child(reg_, child, parent)) {
        return false;
    }
    if (parent == ::entt::null) {
        reg_.remove<RelationshipComponent>(child);
        return true;
    }
    const auto *parent_idc = reg_.try_get<IDComponent>(parent);
    if (!parent_idc || parent_idc->id == lumen::core::INVALID_ID) {
        return false;
    }
    auto &rel = reg_.get_or_emplace<RelationshipComponent>(child);
    rel.parent = parent_idc->id;
    return true;
}

void Scene::destroy_entity(::entt::entity entity) {
    if (!reg_.valid(entity)) {
        return;
    }
    lumen::core::ID dead_id { lumen::core::INVALID_ID };
    if (const auto *idc = reg_.try_get<IDComponent>(entity)) {
        dead_id = idc->id;
    }
    if (dead_id != lumen::core::INVALID_ID) {
        for (const auto c : reg_.view<RelationshipComponent>()) {
            auto &rc = reg_.get<RelationshipComponent>(c);
            if (rc.parent == dead_id) {
                reg_.remove<RelationshipComponent>(c);
            }
        }
    }
    reg_.destroy(entity);
}

std::vector<::entt::entity> Scene::children_of(::entt::entity parent) const {
    std::vector<::entt::entity> out;
    if (!reg_.valid(parent)) {
        return out;
    }
    const auto *pidc = reg_.try_get<IDComponent>(parent);
    if (!pidc || pidc->id == lumen::core::INVALID_ID) {
        return out;
    }
    const lumen::core::ID pid = pidc->id;
    for (const auto e : reg_.view<RelationshipComponent>()) {
        if (reg_.get<RelationshipComponent>(e).parent == pid) {
            out.push_back(e);
        }
    }
    return out;
}

} // namespace lumen::scene
