/**
 * @file scene_orbit_controller.hpp
 * @brief 编辑器式轨道 `SceneOrbitController`；`frame_orbit_on_drawable` 取景辅助
 *
 * 灵敏度量纲等与 demo3d / cube3d_lighting 一致：鼠标增量为像素（SDL 相对运动），
 * 角速度侧为「弧度 / 像素」缩放；`FlyInput` 为世界水平 + 竖直平移（右键 + WASD 等）。
 */

#pragma once

#include "scene/scene_camera_controller.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <entt/entt.hpp>
#include <optional>
#include <utility>

namespace lumen {
namespace platform {
class Input;
}
namespace scene {

class SceneOrbitController : public ISceneCameraController {
public:
    /// 视口灵敏度（`apply_alt_*` / `apply_rmb_look` / `apply_fly_pan`）
    struct Settings {
        float alt_orbit_sensitivity { 0.007f };
        float rmb_look_sensitivity { 0.007f };
        float pan_sensitivity { 0.004f };
        float alt_rmb_zoom_sensitivity { 0.01f };
        float fly_move_speed { 3.0f };
        float fly_move_speed_fast { 9.0f };
        /// 鼠标增量指数平滑时间常数（秒）；`0` 表示不平滑（与旧行为一致）
        float mouse_smooth_time_seconds { 0.022f };
        /// WASD 飞行目标速度趋近时间常数（秒）；`0` 表示立即到目标速度
        float fly_velocity_smooth_time_seconds { 0.07f };
        /// F 取景等 `begin_smooth_frame` 的枢轴 / 距离插值时间常数（秒）；`0` 表示立即到位
        float frame_smooth_time_seconds { 0.18f };
    };

    /// 半径与俯仰（弧度）钳制，与历史 demo 行为一致
    struct Limits {
        float min_radius { 0.8f };
        float max_radius { 20.0f };
        float min_pitch { 0.1f };
        float max_pitch { 1.4f };
    };

    /// 右键飞行：平移意图 + `delta_seconds`（由应用填）
    struct FlyInput {
        bool move_forward {};
        bool move_back {};
        bool move_left {};
        bool move_right {};
        bool move_up {};
        bool move_down {};
        bool fast_modifier {};
        float delta_seconds { 0.0f };
    };

    SceneOrbitController() = default;

    void set_settings(Settings s) { orbit_settings_ = s; }
    [[nodiscard]] Settings settings() const { return orbit_settings_; }

    void set_limits(Limits limits) { limits_ = limits; }
    [[nodiscard]] Limits limits() const { return limits_; }

    void set_pivot(glm::vec3 p) { pivot_ = p; }
    [[nodiscard]] glm::vec3 pivot() const { return pivot_; }

    void set_world_up(glm::vec3 up);
    [[nodiscard]] glm::vec3 world_up() const { return world_up_; }

    void set_yaw(float radians) { yaw_ = radians; }
    void set_pitch(float radians);
    void set_radius(float r);
    /// 与滚轮 / Alt 缩放等冲突时由内部取消平滑取景
    void cancel_smooth_frame();
    /// 每帧调用：将枢轴与半径指数趋近到 `begin_smooth_frame` 所设目标
    void tick_smooth_frame(float delta_seconds);
    /// 开始平滑取景（可重复调用以更新目标）
    void begin_smooth_frame(glm::vec3 target_pivot, float target_radius);
    [[nodiscard]] bool smooth_frame_active() const {
        return smooth_frame_active_;
    }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] float radius() const { return radius_; }

    [[nodiscard]] glm::vec3 orbit_direction() const;
    [[nodiscard]] glm::vec3 eye_position() const;

    void apply_scroll_zoom(float wheel_delta_y, float zoom_speed);
    void apply_radius_scale_drag(float mouse_delta_y, float sensitivity);

    /// ViewManipulate 等改 view 后，保持 pivot 反推 yaw / pitch / radius
    void sync_from_view(const glm::mat4 &view);

    void apply_to(SceneCamera &cam) const override;

    void apply_alt_orbit(float mouse_delta_x, float mouse_delta_y);
    void apply_alt_pan(float mouse_delta_x, float mouse_delta_y);
    void apply_alt_zoom_drag(float mouse_delta_y);
    void apply_rmb_look(float mouse_delta_x, float mouse_delta_y);
    void apply_fly_pan(const FlyInput &input);

    /**
     * @brief 每帧编辑视口导航（Alt+左/中/右键 轨道；视口内无 Alt 时右键飞行）
     *
     * 将原示例里对 `Input` + `viewport_mouse_state` + `imgui_wants_mouse` 的手写分支收拢到此处。
     *
     * @param input 本帧 `EventPump::input()`（建议在 `ImGuiLayer::begin_frame` 之后读取）
     * @param viewport_hovered 鼠标是否在场景视口矩形内
     * @param imgui_blocks_scene_mouse 通常为 `imgui_wants_mouse() && !viewport_hovered`，为 true 时不把鼠标用于飞行/环视
     * @param delta_seconds 飞行平移用的帧步长
     * @return 本帧是否应启用相对鼠标模式（右键飞行时为 true）
     */
    [[nodiscard]] bool apply_per_frame_editor_navigation(
        const platform::Input &input, bool viewport_hovered,
        bool imgui_blocks_scene_mouse, float delta_seconds);

private:
    void clamp_pitch_radius_();

    [[nodiscard]] static float exp_smooth_alpha_(float delta_seconds,
                                               float time_constant_seconds);

    Settings orbit_settings_ {};
    glm::vec3 pivot_ { 0.0f };
    float yaw_ { 0.0f };
    float pitch_ { 0.3f };
    float radius_ { 2.5f };
    glm::vec3 world_up_ { 0.0f, 1.0f, 0.0f };
    Limits limits_ {};

    float smooth_mouse_dx_ { 0.0f };
    float smooth_mouse_dy_ { 0.0f };
    glm::vec3 fly_velocity_world_ { 0.0f };

    bool smooth_frame_active_ { false };
    glm::vec3 smooth_frame_target_pivot_ { 0.0f };
    float smooth_frame_target_radius_ { 1.0f };
};

void frame_orbit_on_drawable(SceneOrbitController &orbit,
                             const entt::registry &reg,
                             entt::entity drawable,
                             const glm::vec3 &mesh_center_local,
                             const glm::vec3 &mesh_half_extents_local);

/// 计算 `frame_orbit_on_drawable` 会使用的枢轴与世界空间半径（不修改 `orbit`）
[[nodiscard]] std::optional<std::pair<glm::vec3, float>>
frame_orbit_targets_for_drawable(
    const SceneOrbitController &orbit, const entt::registry &reg,
    entt::entity drawable, const glm::vec3 &mesh_center_local,
    const glm::vec3 &mesh_half_extents_local);

} // namespace scene
} // namespace lumen
