/**
 * @file scene_orbit_camera.hpp
 * @brief 编辑器式轨道相机：枢轴 + 球坐标 yaw/pitch/radius，输出视图与 Vulkan 透视投影
 */

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <entt/entt.hpp>

namespace lumen {
namespace scene {

/**
 * @brief 轨道半径与俯仰角钳制（弧度 pitch，与 demo 历史行为一致）
 */
struct SceneOrbitCameraLimits {
    float min_radius { 0.8f };
    float max_radius { 20.0f };
    float min_pitch { 0.1f };
    float max_pitch { 1.4f };
};

/**
 * @brief 场景轨道相机（类 Unity Scene 视口）
 *
 * 相机位置 = `pivot + radius * orbit_direction(yaw, pitch)`，`lookAt(pivot)`。
 * 投影使用竖直 FOV、Vulkan NDC Y 翻转（`proj[1][1] *= -1`），与 `docs/reference/glm-vulkan.md` 一致。
 */
class SceneOrbitCamera {
public:
    SceneOrbitCamera() = default;

    void set_limits(SceneOrbitCameraLimits limits) { limits_ = limits; }
    [[nodiscard]] SceneOrbitCameraLimits limits() const { return limits_; }

    void set_pivot(glm::vec3 p) { pivot_ = p; }
    [[nodiscard]] glm::vec3 pivot() const { return pivot_; }

    void set_world_up(glm::vec3 up);
    [[nodiscard]] glm::vec3 world_up() const { return world_up_; }

    void set_yaw(float radians) { yaw_ = radians; }
    void set_pitch(float radians);
    void set_radius(float r);
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] float radius() const { return radius_; }

    void set_fov_y_degrees(float deg) { fov_y_degrees_ = deg; }
    void set_depth_range(float z_near, float z_far);

    [[nodiscard]] glm::vec3 orbit_direction() const;
    [[nodiscard]] glm::vec3 eye_position() const;
    [[nodiscard]] glm::mat4 view_matrix() const;
    /// @param aspect width / height
    [[nodiscard]] glm::mat4 projection_matrix(float aspect) const;

    /**
     * @brief 滚轮缩放：radius -= wheel_delta_y * zoom_speed，再钳制
     */
    void apply_scroll_zoom(float wheel_delta_y, float zoom_speed);

    /**
     * @brief Alt+右键拖拽：radius *= (1 + mouse_delta_y * sensitivity)
     */
    void apply_radius_scale_drag(float mouse_delta_y, float sensitivity);

    /**
     * @brief ViewManipulate 等修改 view 之后，按当前 pivot 反推 yaw / pitch / radius
     */
    void sync_orbit_from_view(const glm::mat4 &view);

private:
    void clamp_pitch_radius_();

    glm::vec3 pivot_ { 0.0f };
    float yaw_ { 0.0f };
    float pitch_ { 0.3f };
    float radius_ { 2.5f };
    glm::vec3 world_up_ { 0.0f, 1.0f, 0.0f };
    float fov_y_degrees_ { 42.0f };
    float z_near_ { 0.1f };
    float z_far_ { 100.0f };
    SceneOrbitCameraLimits limits_ {};
};

/**
 * @brief 将枢轴对准实体上网格局部 AABB 中心的世界坐标，并按包围盒尺度调整半径
 * @param mesh_center_local 模型空间包围盒中心
 * @param mesh_half_extents_local 模型空间半尺寸（轴对齐）
 */
void frame_orbit_on_drawable(SceneOrbitCamera &cam, const ::entt::registry &reg,
                             ::entt::entity drawable,
                             const glm::vec3 &mesh_center_local,
                             const glm::vec3 &mesh_half_extents_local);

} // namespace scene
} // namespace lumen
