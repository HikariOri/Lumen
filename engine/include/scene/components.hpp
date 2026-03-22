/**
 * @file components.hpp
 * @brief EnTT 场景组件：对象 ID、名称、变换、父子关系、可绘制标记
 */

#pragma once

#include <cstdint>
#include <string>

#include <entt/entt.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace lumen {
namespace scene {

/**
 * @brief 场景对象 ID：与 `entt::entity` 无关，供编辑器、序列化、**GPU Picking** 共用
 *
 * **约定（为后续 Pick 预留）**
 * - `id` 为 **`std::uint32_t`**，可直接写入 **R32_UINT** 离屏目标、`uint` push constant、
 *   或打包进 MRT（需注意编码与清除值）。
 * - **`kInvalid == 0`**：表示「无对象 / Pick 未命中 / 清除色」，**不得**分配给实体。
 * - 合法 ID 由 `Scene::create_entity` 从 **1** 起单调分配；销毁实体时从查找表中移除，**数值不回收**
 *   （避免与已记录的 Pick 结果或历史混淆）；若将来需要复用 ID，应单独改策略并更新本文档。
 * - 渲染/拾取 Pass 中：将本组件的 `id` 输出；读回像素后调用 `Scene::entity_from_object_id(id)`
 *   解析为 `entt::entity`。
 */
struct ObjectId {
    static constexpr std::uint32_t kInvalid { 0 };

    std::uint32_t id { kInvalid };
};

/// 编辑器与层级显示用名称
struct NameComponent {
    std::string name;
};

/// 相对父节点的变换矩阵（列主序，与 glm / Vulkan 一致）；无父时即为世界矩阵
struct TransformComponent {
    glm::mat4 matrix { 1.0f };
};

/// 子节点指向父实体；无此组件或父为 null 表示根
struct ParentComponent {
    ::entt::entity parent { ::entt::null };
};

/// 标记参与当前 demo 网格绘制的实体（首个带此标记者用于主视图矩阵）
struct DrawableTag {};

/**
 * @brief 定向光（与 demo3d `cube.frag` 中 `vec4 lightN` 约定一致）
 *
 * - `direction`：**局部空间**下从表面指向光源的向量（与原先手写 UBO 的 xyz 语义相同）；
 *   提交 GPU 时用实体 **世界矩阵的线性部分** 变换到世界空间（见 `pack_directional_lights_for_ubo`）。
 * - `intensity`：对应 shader 中 `lightN.w`，与 `dot(n, normalize(xyz))` 相乘。
 */
struct DirectionalLightComponent {
    glm::vec3 direction { 0.0f, 0.5f, -1.0f };
    float intensity { 1.0f };
};

} // namespace scene
} // namespace lumen
