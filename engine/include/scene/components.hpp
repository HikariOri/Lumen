/**
 * @file components.hpp
 * @brief EnTT 场景组件：对象 ID、标签、变换、父子关系、光源与天光
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cmath>
#include <entt/entt.hpp>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/id.hpp"

namespace lumen {
namespace scene {

/**
 * @brief 实体身份：`Scene::create_entity` 时 `core::generate_random_id()`
 */
struct IDComponent {
    lumen::core::ID id { lumen::core::INVALID_ID };
};

/// 编辑器与层级显示用标签
struct TagComponent {
    std::string tag;

    TagComponent() = default;
    TagComponent(const TagComponent &other) = default;
    explicit TagComponent(std::string t) : tag(std::move(t)) {}

    operator std::string &() { return tag; }
    operator const std::string &() const { return tag; }
};

/**
 * @brief 层级关系：子实体持有父的 `core::ID`（`Scene::set_parent` 写入）
 *
 * `children` 预留；`Scene::children_of` 按 `parent` 与父实体 `IDComponent`
 * 匹配。
 */
struct RelationshipComponent {
    lumen::core::ID parent { lumen::core::INVALID_ID };
    std::vector<lumen::core::ID> children {};

    RelationshipComponent() = default;
    RelationshipComponent(const RelationshipComponent &other) = default;
    RelationshipComponent &
    operator=(const RelationshipComponent &other) = default;
};

/**
 * @brief 局部 TRS；与 `RelationshipComponent` 链式后由 `world_matrix`
 * 得世界矩阵
 */
struct TransformComponent {
    glm::vec3 translation { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale { 1.0f, 1.0f, 1.0f };

private:
    // 须通过 set_rotation / set_rotation_euler 同步，避免只改其一
    glm::vec3 rotationEuler { 0.0f, 0.0f, 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };

public:
    TransformComponent() = default;
    TransformComponent(const TransformComponent &other) = default;
    TransformComponent &operator=(const TransformComponent &other) = default;

    explicit TransformComponent(const glm::vec3 &translation_vec)
        : translation(translation_vec) {}

    [[nodiscard]] glm::mat4 get_transform() const {
        return glm::translate(glm::mat4(1.0f), translation) *
               glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }

    void set_transform(const glm::mat4 &transform) {
        glm::vec3 skew;
        glm::vec4 persp;
        glm::decompose(transform, scale, rotation, translation, skew, persp);
        rotationEuler = glm::eulerAngles(rotation);
    }

    [[nodiscard]] glm::vec3 get_rotation_euler() const { return rotationEuler; }

    void set_rotation_euler(const glm::vec3 &euler) {
        rotationEuler = euler;
        rotation = glm::quat(rotationEuler);
    }

    [[nodiscard]] glm::quat get_rotation() const { return rotation; }

    void set_rotation(const glm::quat &quat) {
        const glm::vec3 original_euler = rotationEuler;
        rotation = quat;
        rotationEuler = glm::eulerAngles(rotation);

        if ((std::fabs(rotationEuler.x - original_euler.x) ==
             glm::pi<float>()) &&
            (std::fabs(rotationEuler.z - original_euler.z) ==
             glm::pi<float>())) {
            rotationEuler.x = original_euler.x;
            rotationEuler.y = glm::pi<float>() - rotationEuler.y;
            rotationEuler.z = original_euler.z;
        }
    }
};

/**
 * @brief 光源类型枚举（逻辑分类；与 `pack_lights_for_ubo` 写入 GPU 的
 * `position.w` 0/1/2 编码独立）
 */
enum class LightType : std::uint8_t {
    None = 0,        ///< 占位或未指定
    Directional = 1, ///< 定向光
    Point = 2,       ///< 点光源
    Spot = 3,        ///< 聚光灯
};

/**
 * @brief 定向光（无限远）
 *
 * 世界空间方向由 `TransformComponent` 与 `world_matrix`
 * 推导；`pack_lights_for_ubo` 将约定默认局部「表面 → 光源」向量经旋转得到（见
 * `light.cpp`）。
 */
struct DirectionalLightComponent {
    glm::vec3 radiance { 1.0f, 1.0f, 1.0f }; ///< 线性空间颜色
    float intensity { 1.0f };                ///< 与 `radiance` 相乘的能量缩放
    bool castShadows { true };
    bool softShadows { true };
    float lightSize { 0.5f };    ///< PCSS 等软阴影：光源面尺寸
    float shadowAmount { 1.0f }; ///< 阴影贡献权重（1 为全强度）
};

/**
 * @brief 点光源
 *
 * 世界位置由实体 `TransformComponent`（及父链）决定；`radius` / `minRadius`
 * 供衰减与打包 UBO 使用。
 */
struct PointLightComponent {
    glm::vec3 radiance { 1.0f, 1.0f, 1.0f };
    float intensity { 1.0f };
    float lightSize { 0.5f }; ///< 面光源近似尺寸（阴影滤波等）
    float minRadius { 1.0f }; ///< 内球半径，缓解过近时的奇异衰减
    float radius { 10.0f };   ///< 有效影响距离（打包进 `GPULight.params.x`）
    bool castShadows { true };
    bool softShadows { true };
    float falloff { 1.0f }; ///< 距离衰减形状系数（着色器侧需配合使用）
};

/**
 * @brief 聚光灯
 *
 * 位置与锥轴由 `Transform` 与世界矩阵决定；实现上锥轴默认为局部 **-Z**，见
 * `pack_lights_for_ubo`。
 */
struct SpotLightComponent {
    glm::vec3 radiance { 1.0f, 1.0f, 1.0f };
    float intensity { 1.0f };
    float range { 10.0f }; ///< 沿锥轴最大距离
    float angle { 60.0f }; ///< 完整锥角（度），外半角为 `angle * 0.5`
    float angleAttenuation {
        5.0f
    }; ///< 内外锥过渡锐度（越大越硬边，映射见 `light.cpp`）
    bool castShadows { false };
    bool softShadows { false };
    float falloff { 1.0f }; ///< 距离衰减系数（着色器侧需配合使用）
};

/**
 * @brief 天光 / 环境光（IBL 或程序化天空）
 *
 * 与全局 `SceneEnvironment`（`scene_environment.hpp`）可并存：此处为 **组件级**
 * 覆盖或编辑器序列化预留。
 */
struct SkyLightComponent {
    /// 环境资源路径（立方体贴图目录、HDR 等；空则回退全局或默认）
    std::string sceneEnvironment;
    float intensity { 1.0f }; ///< 环境采样强度倍增
    float lod { 0.0f };       ///< 环境贴图 mip 偏置或 LOD 选择
    bool dynamicSky {
        false
    }; ///< true 时使用 `turbidityAzimuthInclination` 驱动程序化天空
    /// `dynamicSky == true` 时：x 浑浊度，y/z
    /// 太阳方位与仰角（弧度或度由宿主约定）
    glm::vec3 turbidityAzimuthInclination { 2.0f, 0.0f, 0.0f };
};

} // namespace scene
} // namespace lumen
