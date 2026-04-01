/**
 * @file pick.hpp
 * @brief ID Map 拾取：`entt::entity` 与 R32_UINT 像素值的编解码（见 `note/pick.md`）
 */

#pragma once

#include <cstdint>

#include <entt/entt.hpp>

namespace lumen {
namespace scene {

/// 背景 / 无效：`0`；有效 ID 为 `to_integral(entity) + 1`
[[nodiscard]] inline std::uint32_t
encode_pick_entity_id(const entt::entity e) noexcept {
    if (e == entt::null) {
        return 0;
    }
    return entt::to_integral(e) + 1U;
}

[[nodiscard]] inline entt::entity
decode_pick_entity_id(const std::uint32_t encoded) noexcept {
    if (encoded == 0) {
        return entt::null;
    }
    return static_cast<entt::entity>(encoded - 1U);
}

} // namespace scene
} // namespace lumen
