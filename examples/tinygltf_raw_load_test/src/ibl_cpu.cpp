/**
 * @file ibl_cpu.cpp
 * @brief CPU 端 BRDF LUT 与简单程序化环境贴图（独立实现，不引用其他示例）
 */

#include "ibl_cpu.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace tinygltf_test::ibl {
namespace {

constexpr float k_pi = 3.14159265358979323846F;

float radical_inverse_vdc(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

glm::vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    return glm::vec2(static_cast<float>(i) / static_cast<float>(n),
                     radical_inverse_vdc(i));
}

float geometry_schlick_ggx(float ndotv, float roughness) {
    const float r = roughness + 1.F;
    const float k = (r * r) / 8.F;
    return ndotv / std::max(ndotv * (1.F - k) + k, 1e-7F);
}

float geometry_smith(const glm::vec3 &N, const glm::vec3 &V, const glm::vec3 &L,
                     float roughness) {
    const float ndotv = glm::clamp(glm::dot(N, V), 0.F, 1.F);
    const float ndotl = glm::clamp(glm::dot(N, L), 0.F, 1.F);
    return geometry_schlick_ggx(ndotv, roughness) *
           geometry_schlick_ggx(ndotl, roughness);
}

glm::vec3 importance_sample_ggx(const glm::vec2 &xi, const glm::vec3 &N,
                                float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.F * k_pi * xi.x;
    const float cos_theta =
        std::sqrt(std::max(0.F, (1.F - xi.y) / (1.F + (a * a - 1.F) * xi.y)));
    const float sin_theta = std::sqrt(std::max(0.F, 1.F - cos_theta * cos_theta));
    glm::vec3 Ht;
    Ht.x = std::cos(phi) * sin_theta;
    Ht.y = std::sin(phi) * sin_theta;
    Ht.z = cos_theta;

    const glm::vec3 up =
        std::abs(N.z) < 0.999F ? glm::vec3(0.F, 0.F, 1.F) : glm::vec3(1.F, 0.F, 0.F);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    const glm::vec3 bitangent = glm::cross(N, tangent);
    return glm::normalize(tangent * Ht.x + bitangent * Ht.y + N * Ht.z);
}

glm::vec2 integrate_brdf(float ndotv, float roughness) {
    glm::vec3 V {};
    V.x = std::sqrt(std::max(0.F, 1.F - ndotv * ndotv));
    V.y = 0.F;
    V.z = ndotv;

    const glm::vec3 N(0.F, 0.F, 1.F);
    float A = 0.F;
    float B = 0.F;
    constexpr std::uint32_t k_samples = 1024u;

    for (std::uint32_t i = 0; i < k_samples; ++i) {
        const glm::vec2 xi = hammersley(i, k_samples);
        const glm::vec3 H = importance_sample_ggx(xi, N, roughness);
        const glm::vec3 L = glm::normalize(2.F * glm::dot(V, H) * H - V);
        const float ndotl = glm::clamp(L.z, 0.F, 1.F);
        const float ndoth = glm::clamp(H.z, 0.F, 1.F);
        const float vdoth = glm::clamp(glm::dot(V, H), 0.F, 1.F);
        if (ndotl > 0.F) {
            const float G = geometry_smith(N, V, L, roughness);
            const float G_vis =
                (G * vdoth) / std::max(ndoth * ndotv, 1e-7F);
            const float Fc = std::pow(1.F - vdoth, 5.F);
            A += (1.F - Fc) * G_vis;
            B += Fc * G_vis;
        }
    }
    return glm::vec2(A, B) / static_cast<float>(k_samples);
}

/// 像素 (px,py) ∈ [0,face) 映射到该面的外向方向（单位向量），face 0..5 = +X,-X,+Y,-Y,+Z,-Z
glm::vec3 cubemap_direction(std::uint32_t face, std::uint32_t px, std::uint32_t py,
                            std::uint32_t face_size) {
    const float u =
        (static_cast<float>(px) + 0.5F) / static_cast<float>(face_size) * 2.F - 1.F;
    const float v =
        (static_cast<float>(py) + 0.5F) / static_cast<float>(face_size) * 2.F - 1.F;
    glm::vec3 d {};
    switch (face) {
    case 0:
        d = glm::vec3(1.F, -v, -u);
        break;
    case 1:
        d = glm::vec3(-1.F, -v, u);
        break;
    case 2:
        d = glm::vec3(u, 1.F, v);
        break;
    case 3:
        d = glm::vec3(u, -1.F, -v);
        break;
    case 4:
        d = glm::vec3(u, -v, 1.F);
        break;
    default:
        d = glm::vec3(-u, -v, -1.F);
        break;
    }
    return glm::normalize(d);
}

glm::vec3 sample_environment(const glm::vec3 &dir) {
    const glm::vec3 d = glm::normalize(dir);
    const float up = glm::clamp(d.y, -1.F, 1.F);
    // 偏亮的天空与地平线，便于 IBL 漫反射/镜面有足够能量（RGBA8 仍 clamp 到 1）
    const glm::vec3 zenith(0.22F, 0.42F, 0.72F);
    const glm::vec3 horizon(0.62F, 0.66F, 0.72F);
    const glm::vec3 ground(0.12F, 0.11F, 0.10F);
    glm::vec3 c {};
    if (up >= 0.F) {
        const float t = std::pow(up, 0.40F);
        c = glm::mix(horizon, zenith, t);
    } else {
        const float t = glm::smoothstep(-1.F, 0.F, up);
        c = glm::mix(ground, horizon, t);
    }
    // 半球环境底光，避免背光面过黑
    c += glm::vec3(0.08F, 0.085F, 0.10F);
    const glm::vec3 sun_dir = glm::normalize(glm::vec3(0.25F, 0.88F, 0.18F));
    const float sun_dot = glm::max(glm::dot(d, sun_dir), 0.F);
    c += glm::vec3(1.F, 0.94F, 0.82F) * std::pow(sun_dot, 96.F) * 0.75F;
    return glm::clamp(c, 0.F, 1.F);
}

std::uint32_t cubemap_mip_count(const std::uint32_t base_face_size) {
    std::uint32_t m = 1u;
    std::uint32_t d = std::max(1u, base_face_size);
    while (d > 1u) {
        d >>= 1u;
        ++m;
    }
    return m;
}

glm::vec3 cosine_sample_hemisphere(const glm::vec3 &N, const glm::vec2 &xi) {
    const float r = std::sqrt(xi.x);
    const float phi = 2.F * k_pi * xi.y;
    const glm::vec3 hl(r * std::cos(phi), r * std::sin(phi),
                       std::sqrt(std::max(0.F, 1.F - xi.x)));
    const glm::vec3 up =
        std::abs(N.y) < 0.999F ? glm::vec3(0.F, 1.F, 0.F) : glm::vec3(1.F, 0.F, 0.F);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    const glm::vec3 bitangent = glm::cross(N, tangent);
    return glm::normalize(tangent * hl.x + bitangent * hl.y + N * hl.z);
}

glm::vec3 irradiance_at(const glm::vec3 &N, const std::uint32_t samples) {
    glm::vec3 acc(0.F);
    for (std::uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const glm::vec3 wi = cosine_sample_hemisphere(N, xi);
        acc += sample_environment(wi);
    }
    return acc * (k_pi / static_cast<float>(samples));
}

glm::vec3 specular_prefilter_at(const glm::vec3 &R, const float roughness,
                                const std::uint32_t samples) {
    const glm::vec3 N = glm::normalize(R);
    const glm::vec3 V = N;
    glm::vec3 acc(0.F);
    float wsum = 0.F;
    for (std::uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const glm::vec3 H = importance_sample_ggx(xi, N, roughness);
        const glm::vec3 L = glm::normalize(2.F * glm::dot(V, H) * H - V);
        const float ndotl = glm::max(glm::dot(N, L), 0.F);
        if (ndotl > 0.F) {
            acc += sample_environment(L) * ndotl;
            wsum += ndotl;
        }
    }
    if (wsum > 1e-5F) {
        return acc / wsum;
    }
    return sample_environment(N);
}

} // namespace

void generate_brdf_lut_rgba8(std::vector<std::uint8_t> &out_rgba,
                             const std::uint32_t resolution) {
    out_rgba.resize(static_cast<size_t>(resolution) * static_cast<size_t>(resolution) *
                    4u);
    for (std::uint32_t y = 0; y < resolution; ++y) {
        const float roughness =
            std::max(static_cast<float>(y) / static_cast<float>(resolution), 1e-4F);
        for (std::uint32_t x = 0; x < resolution; ++x) {
            const float ndotv =
                std::max(static_cast<float>(x) / static_cast<float>(resolution),
                         1e-4F);
            const glm::vec2 ab = integrate_brdf(ndotv, roughness);
            const size_t o =
                (static_cast<size_t>(y) * static_cast<size_t>(resolution) +
                 static_cast<size_t>(x)) *
                4u;
            out_rgba[o + 0] = static_cast<std::uint8_t>(
                glm::clamp(ab.x, 0.F, 1.F) * 255.F + 0.5F);
            out_rgba[o + 1] = static_cast<std::uint8_t>(
                glm::clamp(ab.y, 0.F, 1.F) * 255.F + 0.5F);
            out_rgba[o + 2] = 0;
            out_rgba[o + 3] = 255;
        }
    }
}

void fill_procedural_environment_faces(
    const std::uint32_t face_size,
    std::array<std::vector<std::uint8_t>, 6> &out_faces_rgba8) {
    const size_t bytes =
        static_cast<size_t>(face_size) * static_cast<size_t>(face_size) * 4u;
    for (auto &face : out_faces_rgba8) {
        face.resize(bytes);
    }
    for (std::uint32_t face = 0; face < 6u; ++face) {
        for (std::uint32_t py = 0; py < face_size; ++py) {
            for (std::uint32_t px = 0; px < face_size; ++px) {
                const glm::vec3 dir =
                    cubemap_direction(face, px, py, face_size);
                const glm::vec3 rgb = sample_environment(dir);
                const size_t i =
                    (static_cast<size_t>(py) * static_cast<size_t>(face_size) +
                     static_cast<size_t>(px)) *
                    4u;
                out_faces_rgba8[face][i + 0] = static_cast<std::uint8_t>(
                    rgb.r * 255.F + 0.5F);
                out_faces_rgba8[face][i + 1] = static_cast<std::uint8_t>(
                    rgb.g * 255.F + 0.5F);
                out_faces_rgba8[face][i + 2] = static_cast<std::uint8_t>(
                    rgb.b * 255.F + 0.5F);
                out_faces_rgba8[face][i + 3] = 255;
            }
        }
    }
}

void fill_irradiance_environment_faces(
    const std::uint32_t face_size,
    std::array<std::vector<std::uint8_t>, 6> &out_faces_rgba8) {
    constexpr std::uint32_t k_samples = 256u;
    const size_t bytes =
        static_cast<size_t>(face_size) * static_cast<size_t>(face_size) * 4u;
    for (auto &face : out_faces_rgba8) {
        face.resize(bytes);
    }
    for (std::uint32_t face = 0; face < 6u; ++face) {
        for (std::uint32_t py = 0; py < face_size; ++py) {
            for (std::uint32_t px = 0; px < face_size; ++px) {
                const glm::vec3 dir =
                    cubemap_direction(face, px, py, face_size);
                const glm::vec3 rgb =
                    glm::clamp(irradiance_at(dir, k_samples), 0.F, 1.F);
                const size_t i =
                    (static_cast<size_t>(py) * static_cast<size_t>(face_size) +
                     static_cast<size_t>(px)) *
                    4u;
                out_faces_rgba8[face][i + 0] = static_cast<std::uint8_t>(
                    rgb.r * 255.F + 0.5F);
                out_faces_rgba8[face][i + 1] = static_cast<std::uint8_t>(
                    rgb.g * 255.F + 0.5F);
                out_faces_rgba8[face][i + 2] = static_cast<std::uint8_t>(
                    rgb.b * 255.F + 0.5F);
                out_faces_rgba8[face][i + 3] = 255;
            }
        }
    }
}

void build_radiance_env_mipmap_chain_rgba8(
    const std::uint32_t base_face_size,
    std::vector<std::array<std::vector<std::uint8_t>, 6>> &out_mips) {
    const std::uint32_t M = cubemap_mip_count(base_face_size);
    out_mips.resize(M);
    for (std::uint32_t m = 0; m < M; ++m) {
        const std::uint32_t sz = std::max(1u, base_face_size >> m);
        for (auto &face : out_mips[m]) {
            face.resize(static_cast<size_t>(sz) * static_cast<size_t>(sz) * 4u);
        }
    }
    {
        std::array<std::vector<std::uint8_t>, 6> mip0;
        fill_procedural_environment_faces(base_face_size, mip0);
        out_mips[0] = std::move(mip0);
    }
    for (std::uint32_t m = 1; m < M; ++m) {
        const std::uint32_t sz = std::max(1u, base_face_size >> m);
        const float roughness =
            static_cast<float>(m) / static_cast<float>(M - 1u);
        const std::uint32_t samples = (m <= 2u) ? 128u : 64u;
        for (std::uint32_t face = 0; face < 6u; ++face) {
            for (std::uint32_t py = 0; py < sz; ++py) {
                for (std::uint32_t px = 0; px < sz; ++px) {
                    const glm::vec3 R = cubemap_direction(face, px, py, sz);
                    const glm::vec3 rgb = glm::clamp(
                        specular_prefilter_at(R, roughness, samples), 0.F, 1.F);
                    const size_t i =
                        (static_cast<size_t>(py) * static_cast<size_t>(sz) +
                         static_cast<size_t>(px)) *
                        4u;
                    out_mips[m][face][i + 0] = static_cast<std::uint8_t>(
                        rgb.r * 255.F + 0.5F);
                    out_mips[m][face][i + 1] = static_cast<std::uint8_t>(
                        rgb.g * 255.F + 0.5F);
                    out_mips[m][face][i + 2] = static_cast<std::uint8_t>(
                        rgb.b * 255.F + 0.5F);
                    out_mips[m][face][i + 3] = 255;
                }
            }
        }
    }
}

} // namespace tinygltf_test::ibl
