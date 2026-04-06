/**
 * @file id_lookup.hpp
 * @brief 在 `registry` 中按 `IDComponent::id` 查找实体（线性扫描，适合编辑器规模）
 */

#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "scene/components.hpp"

namespace lumen {
namespace scene {

[[nodiscard]] inline std::optional<entt::entity>
find_entity_with_id(const entt::registry &reg, lumen::core::ID id) {
    if (id == lumen::core::INVALID_ID) {
        return std::nullopt;
    }
    for (const auto e : reg.view<IDComponent>()) {
        if (reg.get<IDComponent>(e).id == id) {
            return e;
        }
    }
    return std::nullopt;
}

} // namespace scene
} // namespace lumen
