/**
 * @file scene_orbit_controller.cpp
 */

#include "scene/scene_orbit_controller.hpp"

#include "platform/input.hpp"
#include "scene/transform.hpp"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace lumen::scene {

float SceneOrbitController::exp_smooth_alpha_(const float delta_seconds,
                                               const float time_constant_seconds) {
    if (time_constant_seconds <= 0.0f || delta_seconds <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - std::exp(-delta_seconds / time_constant_seconds);
}

void SceneOrbitController::set_world_up(glm::vec3 up) {
    if (glm::dot(up, up) < 1e-10f) {
        return;
    }
    world_up_ = glm::normalize(up);
}

void SceneOrbitController::set_pitch(float radians) {
    pitch_ = radians;
    clamp_pitch_radius_();
}

void SceneOrbitController::set_radius(float r) {
    radius_ = r;
    clamp_pitch_radius_();
}

glm::vec3 SceneOrbitController::orbit_direction() const {
    return glm::vec3(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
                     std::cos(yaw_) * std::cos(pitch_));
}

glm::vec3 SceneOrbitController::eye_position() const {
    return pivot_ + radius_ * orbit_direction();
}

void SceneOrbitController::apply_scroll_zoom(float wheel_delta_y,
                                             float zoom_speed) {
    radius_ -= wheel_delta_y * zoom_speed;
    clamp_pitch_radius_();
}

void SceneOrbitController::apply_radius_scale_drag(float mouse_delta_y,
                                                   float sensitivity) {
    radius_ *= (1.0f + mouse_delta_y * sensitivity);
    clamp_pitch_radius_();
}

void SceneOrbitController::sync_from_view(const glm::mat4 &view) {
    const glm::mat4 inv = glm::inverse(view);
    const glm::vec3 eye(inv[3]);
    const glm::vec3 v = eye - pivot_;
    const float r = glm::length(v);
    if (r < 1e-5f) {
        return;
    }
    radius_ = glm::clamp(r, limits_.min_radius, limits_.max_radius);
    const glm::vec3 d = v / radius_;
    pitch_ = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
    pitch_ = glm::clamp(pitch_, limits_.min_pitch, limits_.max_pitch);
    yaw_ = std::atan2(d.x, d.z);
}

void SceneOrbitController::apply_to(SceneCamera &cam) const {
    cam.set_look_at(eye_position(), pivot_, world_up_);
}

void SceneOrbitController::clamp_pitch_radius_() {
    radius_ = glm::clamp(radius_, limits_.min_radius, limits_.max_radius);
    pitch_ = glm::clamp(pitch_, limits_.min_pitch, limits_.max_pitch);
}

void frame_orbit_on_drawable(SceneOrbitController &orbit,
                             const entt::registry &reg,
                             const entt::entity drawable,
                             const glm::vec3 &mesh_center_local,
                             const glm::vec3 &mesh_half_extents_local) {
    if (drawable == entt::null || !reg.valid(drawable)) {
        return;
    }
    const glm::mat4 mw = world_matrix(reg, drawable);
    orbit.set_pivot(glm::vec3(mw * glm::vec4(mesh_center_local, 1.0f)));
    const float sx = glm::length(glm::vec3(mw[0]));
    const float sy = glm::length(glm::vec3(mw[1]));
    const float sz = glm::length(glm::vec3(mw[2]));
    const float col_scale = std::max({ sx, sy, sz });
    const auto lim = orbit.limits();
    const float fit_r =
        glm::length(mesh_half_extents_local) * col_scale * 2.75f;
    const float r = glm::clamp(
        std::max(fit_r, lim.min_radius * 1.5f), lim.min_radius, lim.max_radius);
    orbit.set_radius(r);
}

void SceneOrbitController::apply_alt_orbit(const float mouse_delta_x,
                                           const float mouse_delta_y) {
    const float s = orbit_settings_.alt_orbit_sensitivity;
    set_yaw(yaw() - mouse_delta_x * s);
    set_pitch(pitch() + mouse_delta_y * s);
}

void SceneOrbitController::apply_alt_pan(const float mouse_delta_x,
                                         const float mouse_delta_y) {
    const glm::vec3 pivot = pivot_;
    const glm::vec3 orbit_off = radius_ * orbit_direction();
    const glm::vec3 cam_pos = pivot + orbit_off;
    glm::vec3 forward = glm::normalize(pivot - cam_pos);
    const glm::vec3 w_up = world_up_;
    glm::vec3 right = glm::normalize(glm::cross(forward, w_up));
    if (glm::length(right) < 1e-5f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float pan_scale = radius_ * orbit_settings_.pan_sensitivity;
    set_pivot(pivot + (-mouse_delta_x * pan_scale) * right +
              (mouse_delta_y * pan_scale) * up);
}

void SceneOrbitController::apply_alt_zoom_drag(const float mouse_delta_y) {
    apply_radius_scale_drag(mouse_delta_y,
                            orbit_settings_.alt_rmb_zoom_sensitivity);
}

void SceneOrbitController::apply_rmb_look(const float mouse_delta_x,
                                          const float mouse_delta_y) {
    const float s = orbit_settings_.rmb_look_sensitivity;
    set_yaw(yaw() - mouse_delta_x * s);
    set_pitch(pitch() + mouse_delta_y * s);
}

void SceneOrbitController::apply_fly_pan(const FlyInput &input) {
    const glm::vec3 pivot = pivot_;
    const glm::vec3 orbit_off = radius_ * orbit_direction();
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
    const float speed =
        input.fast_modifier ? orbit_settings_.fly_move_speed_fast
                            : orbit_settings_.fly_move_speed;
    glm::vec3 target_vel { 0.0f };
    if (input.move_forward) {
        target_vel += flat_fwd * speed;
    }
    if (input.move_back) {
        target_vel -= flat_fwd * speed;
    }
    if (input.move_left) {
        target_vel -= flat_right * speed;
    }
    if (input.move_right) {
        target_vel += flat_right * speed;
    }
    if (input.move_up) {
        target_vel += glm::vec3(0.0f, 1.0f, 0.0f) * speed;
    }
    if (input.move_down) {
        target_vel -= glm::vec3(0.0f, 1.0f, 0.0f) * speed;
    }

    const float dt = input.delta_seconds;
    const float v_tau = orbit_settings_.fly_velocity_smooth_time_seconds;
    if (v_tau <= 0.0f || dt <= 0.0f) {
        fly_velocity_world_ = target_vel;
        set_pivot(pivot + fly_velocity_world_ * dt);
        return;
    }
    const float a = exp_smooth_alpha_(dt, v_tau);
    fly_velocity_world_ += (target_vel - fly_velocity_world_) * a;
    set_pivot(pivot + fly_velocity_world_ * dt);
}

bool SceneOrbitController::apply_per_frame_editor_navigation(
    const platform::Input &input, const bool viewport_hovered,
    const bool imgui_blocks_scene_mouse, const float delta_seconds) {
    using platform::MouseButton;

    const float mdx = input.mouse_delta_x();
    const float mdy = input.mouse_delta_y();

    const float m_tau = orbit_settings_.mouse_smooth_time_seconds;
    float udx = mdx;
    float udy = mdy;
    if (m_tau > 0.0f && delta_seconds > 0.0f) {
        const float a = exp_smooth_alpha_(delta_seconds, m_tau);
        smooth_mouse_dx_ += (mdx - smooth_mouse_dx_) * a;
        smooth_mouse_dy_ += (mdy - smooth_mouse_dy_) * a;
        udx = smooth_mouse_dx_;
        udy = smooth_mouse_dy_;
    } else {
        smooth_mouse_dx_ = mdx;
        smooth_mouse_dy_ = mdy;
    }

    if (viewport_hovered && input.has_alt() &&
        input.is_mouse_button_down(MouseButton::Left)) {
        apply_alt_orbit(udx, udy);
    }
    if (viewport_hovered && input.has_alt() &&
        input.is_mouse_button_down(MouseButton::Middle)) {
        apply_alt_pan(udx, udy);
    }
    if (viewport_hovered && input.has_alt() &&
        input.is_mouse_button_down(MouseButton::Right)) {
        apply_alt_zoom_drag(udy);
    }

    const bool scene_fly =
        viewport_hovered &&
        input.is_mouse_button_down(MouseButton::Right) && !input.has_alt();

    if (!imgui_blocks_scene_mouse && scene_fly) {
        apply_rmb_look(udx, udy);
        FlyInput fly {};
        fly.move_forward = input.is_key_down(platform::Key::W);
        fly.move_back = input.is_key_down(platform::Key::S);
        fly.move_left = input.is_key_down(platform::Key::A);
        fly.move_right = input.is_key_down(platform::Key::D);
        fly.move_up = input.is_key_down(platform::Key::E);
        fly.move_down = input.is_key_down(platform::Key::Q);
        fly.fast_modifier = input.has_shift();
        fly.delta_seconds = delta_seconds;
        apply_fly_pan(fly);
    } else if (delta_seconds > 0.0f &&
               glm::dot(fly_velocity_world_, fly_velocity_world_) > 1e-12f) {
        set_pivot(pivot() + fly_velocity_world_ * delta_seconds);
        const float v_tau = orbit_settings_.fly_velocity_smooth_time_seconds;
        if (v_tau > 0.0f) {
            const float a = exp_smooth_alpha_(delta_seconds, v_tau);
            fly_velocity_world_ += (glm::vec3(0.0f) - fly_velocity_world_) * a;
        } else {
            fly_velocity_world_ = glm::vec3(0.0f);
        }
    } else {
        fly_velocity_world_ = glm::vec3(0.0f);
    }

    return scene_fly;
}

} // namespace lumen::scene
