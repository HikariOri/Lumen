/**
 * @file pbr_resources.cpp
 * @brief Demo3D：IBL 辅助数据生成
 */

#include "pbr_resources.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace demo3d::pbr {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float radical_inverse_vdc(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    return glm::vec2(static_cast<float>(i) / static_cast<float>(n),
                     radical_inverse_vdc(i));
}

glm::vec3 importance_sample_ggx(glm::vec2 xi, glm::vec3 n, float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.0f * kPi * xi.x;
    const float cos_theta =
        std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    glm::vec3 h { std::cos(phi) * sin_theta, std::sin(phi) * sin_theta,
                  cos_theta };
    const glm::vec3 up =
        std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                               : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    const glm::vec3 bitangent = glm::cross(n, tangent);
    return glm::normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

float geometry_schlick_ggx(float n_dot_v, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return n_dot_v / (n_dot_v * (1.0f - k) + k + 1e-7f);
}

float geometry_smith(float n_dot_v, float n_dot_l, float roughness) {
    return geometry_schlick_ggx(n_dot_v, roughness) *
           geometry_schlick_ggx(n_dot_l, roughness);
}

glm::vec2 integrate_brdf(float n_dot_v, float roughness) {
    const float ndv = std::max(n_dot_v, 1e-4f);
    glm::vec3 v { std::sqrt(std::max(0.0f, 1.0f - ndv * ndv)), 0.0f, ndv };

    float a = 0.0f;
    float b = 0.0f;
    const glm::vec3 n { 0.0f, 0.0f, 1.0f };
    constexpr std::uint32_t kSampleCount = 1024u;
    for (std::uint32_t i = 0; i < kSampleCount; ++i) {
        const glm::vec2 xi = hammersley(i, kSampleCount);
        const glm::vec3 h = importance_sample_ggx(xi, n, roughness);
        const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, h) * h - v);

        const float n_dot_l = std::max(l.z, 0.0f);
        const float n_dot_h = std::max(h.z, 0.0f);
        const float v_dot_h = std::max(glm::dot(v, h), 0.0f);
        if (n_dot_l > 0.0f) {
            const float g = geometry_smith(n_dot_v, n_dot_l, roughness);
            const float g_vis = (g * v_dot_h) / (n_dot_h * n_dot_v + 1e-7f);
            const float fc = std::pow(1.0f - v_dot_h, 5.0f);
            a += (1.0f - fc) * g_vis;
            b += fc * g_vis;
        }
    }
    const float inv = 1.0f / static_cast<float>(kSampleCount);
    return glm::vec2(a, b) * inv;
}

/// 面索引 0..5 对应 Vulkan 立方体贴图层顺序：+X,-X,+Y,-Y,+Z,-Z
glm::vec3 face_uv_to_dir(std::uint32_t face, float u, float v) {
    const float uc = u * 2.0f - 1.0f;
    const float vc = v * 2.0f - 1.0f;
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -vc, -uc));
    case 1: return glm::normalize(glm::vec3(-1.0f, -vc, uc));
    case 2: return glm::normalize(glm::vec3(uc, 1.0f, vc));
    case 3: return glm::normalize(glm::vec3(uc, -1.0f, -vc));
    case 4: return glm::normalize(glm::vec3(uc, -vc, 1.0f));
    case 5: return glm::normalize(glm::vec3(-uc, -vc, -1.0f));
    default: return glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

glm::vec3 sky_radiance(const glm::vec3 &dir, const glm::vec3 &sun_dir) {
    const float h = std::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 zenith { 0.25f, 0.45f, 0.95f };
    glm::vec3 horizon { 0.65f, 0.7f, 0.78f };
    glm::vec3 ground { 0.08f, 0.07f, 0.06f };
    glm::vec3 base = glm::mix(horizon, zenith, std::pow(h, 0.55f));
    if (dir.y < 0.0f) {
        base = glm::mix(ground, horizon, std::clamp(1.0f + dir.y * 2.0f, 0.0f, 1.0f));
    }
    const float sun_dot = std::max(glm::dot(dir, sun_dir), 0.0f);
    const float sun_disk = std::pow(sun_dot, 256.0f) * 6.0f;
    const float corona = std::pow(sun_dot, 8.0f) * 0.35f;
    glm::vec3 sun_col { 1.0f, 0.95f, 0.85f };
    return base + sun_col * (sun_disk + corona);
}

} // namespace

void generate_brdf_lut_rgba8(std::vector<std::uint8_t> &out_rgba,
                             std::uint32_t resolution) {
    out_rgba.resize(static_cast<size_t>(resolution) * resolution * 4u);
    for (std::uint32_t y = 0; y < resolution; ++y) {
        const float roughness =
            std::clamp(static_cast<float>(y) / static_cast<float>(resolution - 1),
                       0.0f, 1.0f);
        for (std::uint32_t x = 0; x < resolution; ++x) {
            const float n_dot_v =
                std::clamp(static_cast<float>(x) / static_cast<float>(resolution - 1),
                           0.0f, 1.0f);
            const glm::vec2 ab = integrate_brdf(n_dot_v, roughness);
            const size_t i =
                (static_cast<size_t>(y) * resolution + static_cast<size_t>(x)) * 4u;
            out_rgba[i + 0] = static_cast<std::uint8_t>(
                std::clamp(ab.x, 0.0f, 1.0f) * 255.0f);
            out_rgba[i + 1] = static_cast<std::uint8_t>(
                std::clamp(ab.y, 0.0f, 1.0f) * 255.0f);
            out_rgba[i + 2] = 0;
            out_rgba[i + 3] = 255;
        }
    }
}

void fill_procedural_sky_faces(std::uint32_t face_size,
                               std::array<std::vector<std::uint8_t>, 6> &faces) {
    const glm::vec3 sun_dir = glm::normalize(glm::vec3(0.35f, 0.85f, 0.25f));
    const size_t face_bytes =
        static_cast<size_t>(face_size) * face_size * 4u;
    for (auto &f : faces) {
        f.resize(face_bytes);
    }
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t py = 0; py < face_size; ++py) {
            for (std::uint32_t px = 0; px < face_size; ++px) {
                const float u =
                    (static_cast<float>(px) + 0.5f) / static_cast<float>(face_size);
                const float v =
                    (static_cast<float>(py) + 0.5f) / static_cast<float>(face_size);
                const glm::vec3 d = face_uv_to_dir(face, u, v);
                glm::vec3 c = sky_radiance(d, sun_dir);
                c = glm::pow(c, glm::vec3(1.0f / 2.2f));
                const size_t o =
                    (static_cast<size_t>(py) * face_size + px) * 4u;
                faces[face][o + 0] =
                    static_cast<std::uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
                faces[face][o + 1] =
                    static_cast<std::uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
                faces[face][o + 2] =
                    static_cast<std::uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
                faces[face][o + 3] = 255;
            }
        }
    }
}

} // namespace demo3d::pbr
