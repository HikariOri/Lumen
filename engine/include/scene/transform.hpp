/**
 * @file transform.hpp
 * @brief 层级世界矩阵计算（parent * local 链）
 */

#pragma once

#include <entt/entt.hpp>
#include <glm/mat4x4.hpp>

namespace lumen {
namespace scene {

/**
 * @brief 计算实体世界矩阵；无局部变换分量时视为单位矩阵
 * @param max_depth 防止环导致栈溢出
 */
[[nodiscard]] glm::mat4 world_matrix(const ::entt::registry &registry,
                                     ::entt::entity entity,
                                     int max_depth = 64);

} // namespace scene
} // namespace lumen
