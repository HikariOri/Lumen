/**
 * @file scene.cpp
 */

#include "scene/scene.hpp"

#include "core/logger.hpp"

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
        const auto *p = reg.try_get<ParentComponent>(x);
        if (!p || p->parent == ::entt::null || !reg.valid(p->parent)) {
            return false;
        }
        x = p->parent;
    }
    return false;
}

} // namespace

std::uint32_t Scene::allocate_object_id_() {
    if (next_object_id_ == ObjectId::kInvalid) {
        LUMEN_LOG_ERROR(
            "Scene::allocate_object_id_: ObjectId 空间已用尽（uint32 已绕回 0）");
        return ObjectId::kInvalid;
    }
    const std::uint32_t k = next_object_id_;
    ++next_object_id_;
    if (next_object_id_ == ObjectId::kInvalid) {
        LUMEN_LOG_WARN(
            "Scene: 已达 ObjectId 最大值 0xFFFFFFFF，后续 create_entity 将无法分配新 "
            "ID");
    }
    return k;
}

::entt::entity Scene::create_entity(std::string_view name) {
    const ::entt::entity e = reg_.create();
    const std::uint32_t k = allocate_object_id_();
    if (k != ObjectId::kInvalid) {
        reg_.emplace<ObjectId>(e, ObjectId { k });
        object_id_to_entity_.emplace(k, e);
    } else {
        reg_.emplace<ObjectId>(e);
    }
    reg_.emplace<Transform>(e);
    reg_.emplace<NameComponent>(e, std::string(name));
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
        reg_.remove<ParentComponent>(child);
        return true;
    }
    reg_.emplace_or_replace<ParentComponent>(child, ParentComponent { parent });
    return true;
}

void Scene::destroy_entity(::entt::entity entity) {
    if (!reg_.valid(entity)) {
        return;
    }
    if (const auto *oid = reg_.try_get<ObjectId>(entity)) {
        if (oid->id != ObjectId::kInvalid) {
            object_id_to_entity_.erase(oid->id);
        }
    }
    for (const auto c : reg_.view<ParentComponent>()) {
        auto &pc = reg_.get<ParentComponent>(c);
        if (pc.parent == entity) {
            reg_.remove<ParentComponent>(c);
        }
    }
    reg_.destroy(entity);
}

std::vector<::entt::entity> Scene::children_of(::entt::entity parent) const {
    std::vector<::entt::entity> out;
    for (const auto e : reg_.view<ParentComponent>()) {
        if (reg_.get<ParentComponent>(e).parent == parent) {
            out.push_back(e);
        }
    }
    return out;
}

::entt::entity Scene::primary_drawable() const {
    for (const auto e : reg_.view<DrawableTag>()) {
        return e;
    }
    return ::entt::null;
}

std::uint32_t Scene::object_id_for(::entt::entity entity) const {
    if (!reg_.valid(entity)) {
        return ObjectId::kInvalid;
    }
    const auto *oid = reg_.try_get<ObjectId>(entity);
    if (!oid) {
        return ObjectId::kInvalid;
    }
    return oid->id;
}

std::optional<::entt::entity>
Scene::entity_from_object_id(std::uint32_t object_id) const {
    if (object_id == ObjectId::kInvalid) {
        return std::nullopt;
    }
    const auto it = object_id_to_entity_.find(object_id);
    if (it == object_id_to_entity_.end()) {
        return std::nullopt;
    }
    if (!reg_.valid(it->second)) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace lumen::scene
