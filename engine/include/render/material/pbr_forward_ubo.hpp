/**
 * @file pbr_forward_ubo.hpp
 * @brief Forward PBR（`note/forward_shader.md`）：Frame / Material / Object / Light 的
 * std140 UBO CPU 镜像
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "scene/pbr_material.hpp"

namespace lumen::render {

inline constexpr int k_pbr_forward_max_lights { 64 };
inline constexpr int k_pbr_legacy_point_light_cap { 4 };

/// `Light.position.w`：与 `pbr_forward.frag` 中宏一致
inline constexpr float k_pbr_light_type_directional { 0.0F };
inline constexpr float k_pbr_light_type_point { 1.0F };
inline constexpr float k_pbr_light_type_spot { 2.0F };

/**
 * @brief std140，`FrameUBO`（set 0 binding 0）
 */
struct PbrFrameUbo {
    glm::mat4 view { 1.0F };
    glm::mat4 proj { 1.0F };
    glm::mat4 view_proj { 1.0F };
    glm::vec4 camera_pos_w { 0.0F, 0.0F, 0.0F, 1.0F };
    /// x: exposure，y: iblStrength，z: maxPrefilterMip，w: diffIrradianceMip
    glm::vec4 exposure_ibl_mips { 1.0F, 1.0F, 0.0F, 0.0F };
};

static_assert(sizeof(PbrFrameUbo) == 224U);

/**
 * @brief std140，`MaterialUBO`（set 1 binding 0）
 */
struct PbrMaterialUbo {
    glm::vec4 base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    glm::vec4 emissive { 0.0F, 0.0F, 0.0F, 1.0F };
    float metallic_factor { 1.0F };
    float roughness_factor { 1.0F };
    float occlusion_strength { 1.0F };
    std::uint32_t flags {};
    std::uint32_t alpha_mode {};
    float alpha_cutoff { 0.5F };
    float _std140_pad0 {};
    float _std140_pad1 {};
};

static_assert(sizeof(PbrMaterialUbo) == 64U);

/**
 * @brief std140，`ObjectUBO`（set 2 binding 0）
 */
struct PbrObjectUbo {
    glm::mat4 model { 1.0F };
    glm::mat4 normal_matrix { 1.0F };
};

static_assert(sizeof(PbrObjectUbo) == 128U);

struct PbrLightGpu {
    glm::vec4 position_type {};
    glm::vec4 direction {};
    glm::vec4 color_intensity {};
    glm::vec4 params {};
};

static_assert(sizeof(PbrLightGpu) == 64U);

/**
 * @brief std140，`LightUBO`（set 3 binding 0）
 */
struct PbrLightUbo {
    glm::ivec4 light_count_pad {};
    std::array<PbrLightGpu, k_pbr_forward_max_lights> lights {};
};

static_assert(sizeof(PbrLightUbo) == 16U + 64U * 64U);

struct PbrDefaultPointLightInit {
    glm::vec3 pos {};
    float range { 5.0F };
    glm::vec3 color { 1.0F };
    float intensity { 2.0F };
};

[[nodiscard]] inline const std::array<PbrDefaultPointLightInit,
                                      k_pbr_legacy_point_light_cap> &
pbr_default_point_lights_table() {
    static constexpr std::array<PbrDefaultPointLightInit,
                                k_pbr_legacy_point_light_cap>
        k_lights { {
            PbrDefaultPointLightInit { glm::vec3 { 1.5F, 2.0F, 1.2F }, 5.0F,
                                       glm::vec3 { 1.0F, 0.92F, 0.82F }, 3.0F },
            PbrDefaultPointLightInit { glm::vec3 { -2.0F, 1.2F, 0.8F }, 5.5F,
                                       glm::vec3 { 0.55F, 0.72F, 1.0F }, 2.4F },
            PbrDefaultPointLightInit { glm::vec3 { 0.0F, 0.45F, -2.2F }, 6.0F,
                                       glm::vec3 { 1.0F, 0.85F, 0.68F }, 2.0F },
            PbrDefaultPointLightInit { glm::vec3 { -0.9F, -0.4F, 1.7F }, 4.5F,
                                       glm::vec3 { 0.48F, 0.52F, 0.58F }, 1.9F },
        } };
    return k_lights;
}

inline void pack_pbr_frame_ubo(PbrFrameUbo &f, const glm::mat4 &view,
                               const glm::mat4 &proj, const glm::vec3 &eye,
                               float exposure, float ibl_strength,
                               float max_prefilter_mip,
                               float diff_irradiance_mip) {
    f.view = view;
    f.proj = proj;
    f.view_proj = proj * view;
    f.camera_pos_w = glm::vec4(eye, 1.0F);
    f.exposure_ibl_mips = glm::vec4(exposure, ibl_strength, max_prefilter_mip,
                                    diff_irradiance_mip);
}

inline void pack_pbr_material_ubo(PbrMaterialUbo &out,
                                  const lumen::scene::PBRMaterial &mat,
                                  float emissive_intensity) {
    out.base_color_factor = mat.base_color_factor;
    out.emissive = glm::vec4(mat.emissive_factor, emissive_intensity);
    out.metallic_factor = mat.metallic_factor;
    out.roughness_factor = mat.roughness_factor;
    out.occlusion_strength = mat.occlusion_strength;
    out.flags = 0U;
    out.alpha_mode = static_cast<std::uint32_t>(mat.alpha_mode);
    out.alpha_cutoff = 0.5F;
    out._std140_pad0 = 0.0F;
    out._std140_pad1 = 0.0F;
}

inline void fill_pbr_light_ubo_default_points(PbrLightUbo &lu, int active_count,
                                              float strength_scale) {
    const int n =
        std::clamp(active_count, 0, k_pbr_legacy_point_light_cap);
    lu.light_count_pad.x = n;
    lu.light_count_pad.y = 0;
    lu.light_count_pad.z = 0;
    lu.light_count_pad.w = 0;
    const auto &L = pbr_default_point_lights_table();
    for (int i = 0; i < k_pbr_forward_max_lights; ++i) {
        if (i < n) {
            const auto &s = L[static_cast<size_t>(i)];
            lu.lights[static_cast<size_t>(i)].position_type =
                glm::vec4(s.pos, k_pbr_light_type_point);
            lu.lights[static_cast<size_t>(i)].direction = glm::vec4(0.0F);
            lu.lights[static_cast<size_t>(i)].color_intensity = glm::vec4(
                s.color, s.intensity * strength_scale);
            lu.lights[static_cast<size_t>(i)].params =
                glm::vec4((std::max)(s.range, 0.05F), 0.0F, 0.0F, 0.0F);
        } else {
            lu.lights[static_cast<size_t>(i)] = {};
        }
    }
}

/** ObjectUBO 动态绑定的步长（需 ≥ minUniformBufferOffsetAlignment） */
inline std::size_t pbr_object_ubo_dynamic_stride(std::size_t min_align) {
    const std::size_t base = sizeof(PbrObjectUbo);
    if (min_align == 0U) {
        return base;
    }
    return (base + min_align - 1U) / min_align * min_align;
}

} // namespace lumen::render
