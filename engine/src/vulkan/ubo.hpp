/**
 * @file ubo.hpp
 * @brief 与 GLSL `uniform` 块对齐的常用 UBO 结构（`std140` / `glm::mat4`）。
 */

#pragma once

#include <glm/glm.hpp>

namespace vulkan {

/**
 * @brief 视图与投影矩阵（顶点侧典型用法：`gl_Position = proj * view *
 * vec4(local,1)`）。
 */
struct UboViewProj {
    glm::mat4 view { 1.F };
    glm::mat4 proj { 1.F };
};

} // namespace vulkan
