/**
 * @file scene_camera.hpp
 * @brief 场景相机：仅透视 / 正交投影与 lookAt 视图（与具体导航方式解耦）
 */

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace lumen {
namespace scene {

enum class SceneCameraProjection { Perspective, Orthographic };

/**
 * @brief 场景相机：存储投影参数与 `lookAt(eye, target, world_up)` 状态
 *
 * 投影矩阵使用 Vulkan NDC Y 翻转（`proj[1][1] *= -1`），与
 * `docs/reference/glm-vulkan.md` 一致。
 */
class SceneCamera {
public:
    SceneCamera() = default;

    [[nodiscard]] SceneCameraProjection projection() const {
        return projection_;
    }

    void set_projection_perspective(float fov_y_degrees, float z_near,
                                    float z_far);
    /// @param half_height 视锥竖直半高（世界单位）；水平范围由 `aspect` 推导
    void set_projection_orthographic(float half_height, float z_near,
                                     float z_far);

    void set_depth_range(float z_near, float z_far);

    void set_look_at(glm::vec3 eye, glm::vec3 target, glm::vec3 world_up);

    [[nodiscard]] glm::vec3 eye_position() const { return eye_; }
    [[nodiscard]] glm::vec3 target() const { return target_; }
    [[nodiscard]] glm::vec3 world_up() const { return world_up_; }

    [[nodiscard]] glm::mat4 view_matrix() const;
    /// @param aspect width / height
    [[nodiscard]] glm::mat4 projection_matrix(float aspect) const;

private:
    SceneCameraProjection projection_ { SceneCameraProjection::Perspective };
    float fov_y_degrees_ { 42.0F };
    float ortho_half_height_ { 5.0F };
    float z_near_ { 0.1F };
    float z_far_ { 100.0F };
    glm::vec3 eye_ { 0.0F, 0.0F, 2.5F };
    glm::vec3 target_ { 0.0F };
    glm::vec3 world_up_ { 0.0F, 1.0F, 0.0F };
};

} // namespace scene
} // namespace lumen
