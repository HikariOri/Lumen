/**
 * @file directional_light.hpp
 * @brief 将场景中带 `DirectionalLightComponent` 的实体打包为 UBO 用 `vec4[4]`
 */

#pragma once

#include <cstddef>

#include <entt/entt.hpp>
#include <glm/vec4.hpp>

namespace lumen {
namespace scene {

/// 与 `examples/demo3d/shaders/cube.frag` 中 light0..light3 数量一致
inline constexpr std::size_t kMaxDirectionalLightsUbo { 4 };

/**
 * @brief 收集至多 4 盏定向光写入 `out_lights`（其余填 0）
 *
 * 按 `ObjectId::id` 升序稳定选取，便于调试与序列化对齐。
 * 世界方向：`world_dir = mat3(world_matrix(entity)) * direction`（再写入 xyz；w 为 intensity）。
 */
void pack_directional_lights_for_ubo(const ::entt::registry &registry,
                                     glm::vec4 (&out_lights)[kMaxDirectionalLightsUbo]);

} // namespace scene
} // namespace lumen
