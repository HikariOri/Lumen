/**
 * @file light.hpp
 * @brief 将 `LightComponent` 打包为与 `demo3d` UBO 对齐的 `GPULight` 数组
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <entt/entt.hpp>
#include <glm/vec4.hpp>

namespace lumen {
namespace scene {

/// 与 `examples/demo3d/shaders/cube.frag` / `skybox.*` 中 UBO 的 `lights[]`
/// 长度一致
inline constexpr std::size_t kMaxLightsUbo { 8 };

/**
 * @brief GPU 端光源（std140 下 4×vec4）
 *
 * - `position.w`：类型 —— 0=方向光，1=点光，2=聚光；点/聚光时 xyz 为世界位置
 * - `direction.xyz`：方向光为「表面 →
 * 光源」的世界单位向量；聚光为锥轴（光发射方向）世界单位向量
 * - `color.rgb` + `color.w`：颜色 × 强度（由 CPU 预乘强度写入 w 或分开：此处 w
 * 存 intensity）
 * -
 * `params`：`x`=有效距离（点/聚光）；`y`=cos(外锥角)、`z`=cos(内锥角)（仅聚光）
 */
struct GPULight {
    glm::vec4 position {};
    glm::vec4 direction {};
    glm::vec4 color {};
    glm::vec4 params {};
};

/**
 * @brief 收集至多 `kMaxLightsUbo` 盏灯；`out_count` 为实际数量
 *
 * 按 `ObjectId::id` 升序选取，与原先定向光打包策略一致。
 */
void pack_lights_for_ubo(const ::entt::registry &registry, GPULight *out_lights,
                         std::uint32_t &out_count);

} // namespace scene
} // namespace lumen
