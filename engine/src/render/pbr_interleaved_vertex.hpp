/**
 * @file render/pbr_interleaved_vertex.hpp
 * @brief PBR 前向管线交错顶点（与 `pbr_forward` 着色器布局一致）
 */

#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace lumen::render {

struct PbrInterleavedVertex {
    glm::vec3 position {};
    glm::vec3 normal { 0.0F, 1.0F, 0.0F };
    glm::vec2 uv {};
    glm::vec4 tangent { 1.0F, 0.0F, 0.0F, 1.0F };
};

inline void compute_pbr_mesh_tangents(std::vector<PbrInterleavedVertex> &verts,
                                      const std::vector<std::uint32_t> &indices) {
    std::vector<glm::vec3> accum_tan_u(verts.size(), glm::vec3(0.0F));
    std::vector<glm::vec3> accum_tan_v(verts.size(), glm::vec3(0.0F));
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::uint32_t i0 = indices[i];
        const std::uint32_t i1 = indices[i + 1];
        const std::uint32_t i2 = indices[i + 2];
        const PbrInterleavedVertex &v0 = verts[i0];
        const PbrInterleavedVertex &v1 = verts[i1];
        const PbrInterleavedVertex &v2 = verts[i2];
        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 duv1 = v1.uv - v0.uv;
        const glm::vec2 duv2 = v2.uv - v0.uv;
        const float denom = duv1.x * duv2.y - duv2.x * duv1.y + 1e-8F;
        const float f = 1.0F / denom;
        const glm::vec3 t = f * (edge1 * duv2.y - edge2 * duv1.y);
        const glm::vec3 b = f * (edge2 * duv1.x - edge1 * duv2.x);
        accum_tan_u[i0] += t;
        accum_tan_u[i1] += t;
        accum_tan_u[i2] += t;
        accum_tan_v[i0] += b;
        accum_tan_v[i1] += b;
        accum_tan_v[i2] += b;
    }
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const glm::vec3 &n = verts[i].normal;
        glm::vec3 t = accum_tan_u[i];
        t = glm::normalize(t - n * glm::dot(n, t));
        const float w =
            glm::dot(glm::cross(n, t), glm::normalize(accum_tan_v[i])) < 0.0F
                ? -1.0F
                : 1.0F;
        verts[i].tangent = glm::vec4(t, w);
    }
}

} // namespace lumen::render
