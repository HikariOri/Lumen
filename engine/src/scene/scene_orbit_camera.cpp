/**
 * @file scene_orbit_camera.cpp
 */

#include "scene/scene_orbit_camera.hpp"

#include "scene/transform.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace lumen::scene {

void SceneOrbitCamera::set_world_up(glm::vec3 up) {
    if (glm::dot(up, up) < 1e-10f) {
        return;
    }
    world_up_ = glm::normalize(up);
}

void SceneOrbitCamera::set_pitch(float radians) {
    pitch_ = radians;
    clamp_pitch_radius_();
}

void SceneOrbitCamera::set_radius(float r) {
    radius_ = r;
    clamp_pitch_radius_();
}

void SceneOrbitCamera::set_depth_range(float z_near, float z_far) {
    z_near_ = z_near;
    z_far_ = z_far;
}

glm::vec3 SceneOrbitCamera::orbit_direction() const {
    return glm::vec3(std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
                     std::cos(yaw_) * std::cos(pitch_));
}

glm::vec3 SceneOrbitCamera::eye_position() const {
    return pivot_ + radius_ * orbit_direction();
}

glm::mat4 SceneOrbitCamera::view_matrix() const {
    const glm::vec3 eye = eye_position();
    return glm::lookAt(eye, pivot_, world_up_);
}

glm::mat4 SceneOrbitCamera::projection_matrix(float aspect) const {
    glm::mat4 p = glm::perspective(glm::radians(fov_y_degrees_), aspect, z_near_,
                                   z_far_);
    p[1][1] *= -1.0f;
    return p;
}

void SceneOrbitCamera::apply_scroll_zoom(float wheel_delta_y, float zoom_speed) {
    radius_ -= wheel_delta_y * zoom_speed;
    clamp_pitch_radius_();
}

void SceneOrbitCamera::apply_radius_scale_drag(float mouse_delta_y,
                                               float sensitivity) {
    radius_ *= (1.0f + mouse_delta_y * sensitivity);
    clamp_pitch_radius_();
}

void SceneOrbitCamera::sync_orbit_from_view(const glm::mat4 &view) {
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

void SceneOrbitCamera::clamp_pitch_radius_() {
    radius_ = glm::clamp(radius_, limits_.min_radius, limits_.max_radius);
    pitch_ = glm::clamp(pitch_, limits_.min_pitch, limits_.max_pitch);
}

void frame_orbit_on_drawable(SceneOrbitCamera &cam, const ::entt::registry &reg,
                             const ::entt::entity drawable,
                             const glm::vec3 &mesh_center_local,
                             const glm::vec3 &mesh_half_extents_local) {
    if (drawable == ::entt::null || !reg.valid(drawable)) {
        return;
    }
    const glm::mat4 mw = world_matrix(reg, drawable);
    cam.set_pivot(glm::vec3(mw * glm::vec4(mesh_center_local, 1.0f)));
    const float sx = glm::length(glm::vec3(mw[0]));
    const float sy = glm::length(glm::vec3(mw[1]));
    const float sz = glm::length(glm::vec3(mw[2]));
    const float col_scale = std::max({ sx, sy, sz });
    const auto lim = cam.limits();
    const float fit_r =
        glm::length(mesh_half_extents_local) * col_scale * 2.75f;
    const float r = glm::clamp(
        std::max(fit_r, lim.min_radius * 1.5f), lim.min_radius, lim.max_radius);
    cam.set_radius(r);
}

} // namespace lumen::scene
