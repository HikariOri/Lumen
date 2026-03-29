/**
 * @file scene_camera.cpp
 */

#include "scene/scene_camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace lumen::scene {

void SceneCamera::set_projection_perspective(const float fov_y_degrees,
                                             const float z_near,
                                             const float z_far) {
    projection_ = SceneCameraProjection::Perspective;
    fov_y_degrees_ = fov_y_degrees;
    z_near_ = z_near;
    z_far_ = z_far;
}

void SceneCamera::set_projection_orthographic(const float half_height,
                                              const float z_near,
                                              const float z_far) {
    projection_ = SceneCameraProjection::Orthographic;
    ortho_half_height_ = half_height;
    z_near_ = z_near;
    z_far_ = z_far;
}

void SceneCamera::set_depth_range(const float z_near, const float z_far) {
    z_near_ = z_near;
    z_far_ = z_far;
}

void SceneCamera::set_look_at(glm::vec3 eye, glm::vec3 target,
                              glm::vec3 world_up) {
    eye_ = eye;
    target_ = target;
    if (glm::dot(world_up, world_up) < 1e-10f) {
        return;
    }
    world_up_ = glm::normalize(world_up);
}

glm::mat4 SceneCamera::view_matrix() const {
    return glm::lookAt(eye_, target_, world_up_);
}

glm::mat4 SceneCamera::projection_matrix(const float aspect) const {
    glm::mat4 p {};
    if (projection_ == SceneCameraProjection::Perspective) {
        p = glm::perspective(glm::radians(fov_y_degrees_), aspect, z_near_,
                             z_far_);
    } else {
        const float half_w = ortho_half_height_ * aspect;
        p = glm::ortho(-half_w, half_w, -ortho_half_height_, ortho_half_height_,
                       z_near_, z_far_);
    }
    p[1][1] *= -1.0F;
    return p;
}

} // namespace lumen::scene
