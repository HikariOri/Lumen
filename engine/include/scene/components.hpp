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
#include <glm/vec4.hpp>

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
 * @brief PBR 材质参数与贴图路径（金属–粗糙度工作流）
 *
 * 路径为空时使用引擎占位纹理；运行时应在 GPU 侧解析为 Texture 并写 Descriptor。
 */
struct MaterialComponent {
    glm::vec4 base_color_factor { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic_factor { 1.0f };
    float roughness_factor { 1.0f };
    float ao_factor { 1.0f };
    float _pad0 {};
    glm::vec3 emissive_factor { 0.0f, 0.0f, 0.0f };
    float _pad1 {};

    std::string albedo_path;
    std::string normal_path;
    /// glTF：B=金属，G=粗糙
    std::string metallic_roughness_path;
    std::string ao_path;
    std::string emissive_path;
};

/**
 * @brief 光源类型（与 GPU `GPULight.position.w` 编码一致：0/1/2）
 */
enum class LightType : std::uint8_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

/**
 * @brief 统一光源组件（对应设计文档中的 `LightComponent` + 各子类型字段）
 *
 * 按 `type` 解释字段：
 * - **方向光**：`local_direction`（局部空间，**表面 → 光源**）、`color`、`intensity`；`Transform` 仅旋转影响方向。
 * - **点光**：`Transform` 平移为世界位置，`range` 为影响距离；`color`、`intensity`。
 * - **聚光**：`Transform` 为世界位置，`local_direction` 为锥轴（光发射方向，局部空间），`range`、`inner_radians` /
 *   `outer_radians`（相对锥轴的半角，弧度，内 ≤ 外）；`color`、`intensity`。
 *
 * 点光衰减系数暂由 shader 固定；若需 per-light 衰减，可后续扩展本结构。
 */
struct LightComponent {
    LightType type { LightType::Directional };
    std::uint8_t _pad[3] {};
    glm::vec3 color { 1.0f, 1.0f, 1.0f };
    float intensity { 1.0f };
    glm::vec3 local_direction { 0.0f, 0.5f, -1.0f };
    float range { 10.0f };
    float inner_radians { 0.35f };
    float outer_radians { 0.61f };
};

} // namespace scene
} // namespace lumen
