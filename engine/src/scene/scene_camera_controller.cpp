/**
 * @file scene_camera_controller.cpp
 */

#include "scene/scene_camera_controller.hpp"

#include <glm/geometric.hpp>

namespace lumen::scene {

void SceneCameraController::apply_alt_orbit(SceneOrbitCamera &cam,
                                            const float mouse_delta_x,
                                            const float mouse_delta_y) {
    const float s = settings_.alt_orbit_sensitivity;
    cam.set_yaw(cam.yaw() - mouse_delta_x * s);
    cam.set_pitch(cam.pitch() + mouse_delta_y * s);
}

void SceneCameraController::apply_alt_pan(SceneOrbitCamera &cam,
                                          const float mouse_delta_x,
                                          const float mouse_delta_y) {
    const glm::vec3 pivot = cam.pivot();
    const glm::vec3 orbit_off = cam.radius() * cam.orbit_direction();
    const glm::vec3 cam_pos = pivot + orbit_off;
    glm::vec3 forward = glm::normalize(pivot - cam_pos);
    const glm::vec3 world_up = cam.world_up();
    glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
    if (glm::length(right) < 1e-5f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float pan_scale = cam.radius() * settings_.pan_sensitivity;
    cam.set_pivot(pivot + (-mouse_delta_x * pan_scale) * right +
                  (mouse_delta_y * pan_scale) * up);
}

void SceneCameraController::apply_alt_zoom_drag(SceneOrbitCamera &cam,
                                                  const float mouse_delta_y) {
    cam.apply_radius_scale_drag(mouse_delta_y,
                                settings_.alt_rmb_zoom_sensitivity);
}

void SceneCameraController::apply_rmb_look(SceneOrbitCamera &cam,
                                           const float mouse_delta_x,
                                           const float mouse_delta_y) {
    const float s = settings_.rmb_look_sensitivity;
    cam.set_yaw(cam.yaw() - mouse_delta_x * s);
    cam.set_pitch(cam.pitch() + mouse_delta_y * s);
}

void SceneCameraController::apply_fly_pan(SceneOrbitCamera &cam,
                                          const SceneCameraFlyInput &input) {
    const glm::vec3 pivot = cam.pivot();
    const glm::vec3 orbit_off = cam.radius() * cam.orbit_direction();
    const glm::vec3 cam_pos = pivot + orbit_off;
    const glm::vec3 view_fwd = glm::normalize(pivot - cam_pos);
    glm::vec3 flat_fwd(view_fwd.x, 0.0f, view_fwd.z);
    if (glm::length(flat_fwd) > 1e-5f) {
        flat_fwd = glm::normalize(flat_fwd);
    } else {
        flat_fwd = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    const glm::vec3 flat_right =
        glm::normalize(glm::cross(flat_fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const float step =
        (input.fast_modifier ? settings_.fly_move_speed_fast
                             : settings_.fly_move_speed) *
        input.delta_seconds;
    glm::vec3 delta { 0.0f };
    if (input.move_forward) {
        delta += flat_fwd * step;
    }
    if (input.move_back) {
        delta -= flat_fwd * step;
    }
    if (input.move_left) {
        delta -= flat_right * step;
    }
    if (input.move_right) {
        delta += flat_right * step;
    }
    if (input.move_up) {
        delta += glm::vec3(0.0f, 1.0f, 0.0f) * step;
    }
    if (input.move_down) {
        delta -= glm::vec3(0.0f, 1.0f, 0.0f) * step;
    }
    cam.set_pivot(pivot + delta);
}

} // namespace lumen::scene
