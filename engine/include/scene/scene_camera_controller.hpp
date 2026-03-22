/**
 * @file scene_camera_controller.hpp
 * @brief 将鼠标 / 键盘输入应用到 `SceneOrbitCamera`（与平台无关，由应用传入增量与按键状态）
 */

#pragma once

#include "scene/scene_orbit_camera.hpp"

namespace lumen {
namespace scene {

/**
 * @brief 灵敏度与飞行速度（单位与历史 demo3d 一致）
 */
struct SceneCameraControllerSettings {
    float alt_orbit_sensitivity { 0.007f };
    float rmb_look_sensitivity { 0.007f };
    float pan_sensitivity { 0.004f };
    float alt_rmb_zoom_sensitivity { 0.01f };
    float fly_move_speed { 3.0f };
    float fly_move_speed_fast { 9.0f };
};

/**
 * @brief 右键「飞行」模式下的平移输入（世界水平 + 竖直）
 */
struct SceneCameraFlyInput {
    bool move_forward {};
    bool move_back {};
    bool move_left {};
    bool move_right {};
    bool move_up {};
    bool move_down {};
    bool fast_modifier {};
    float delta_seconds { 0.0f };
};

/**
 * @brief 场景相机控制器：不读取 SDL/ImGui，仅根据应用提供的 delta 修改相机
 */
class SceneCameraController {
public:
    void set_settings(SceneCameraControllerSettings s) { settings_ = s; }
    [[nodiscard]] SceneCameraControllerSettings settings() const {
        return settings_;
    }

    /// Alt + 左键拖拽：环绕枢轴
    void apply_alt_orbit(SceneOrbitCamera &cam, float mouse_delta_x,
                         float mouse_delta_y);

    /// Alt + 中键拖拽：沿相机平面平移枢轴
    void apply_alt_pan(SceneOrbitCamera &cam, float mouse_delta_x,
                       float mouse_delta_y);

    /// Alt + 右键拖拽：缩放半径
    void apply_alt_zoom_drag(SceneOrbitCamera &cam, float mouse_delta_y);

    /// 右键拖拽（非 Alt）：环视（改 yaw/pitch）
    void apply_rmb_look(SceneOrbitCamera &cam, float mouse_delta_x,
                        float mouse_delta_y);

    /// 右键飞行：在水平面与竖直方向平移枢轴（与 demo 中 WASD + E/Q 一致）
    void apply_fly_pan(SceneOrbitCamera &cam, const SceneCameraFlyInput &input);

private:
    SceneCameraControllerSettings settings_;
};

} // namespace scene
} // namespace lumen
