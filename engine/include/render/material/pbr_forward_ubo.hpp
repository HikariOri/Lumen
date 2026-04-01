/**
 * @file pbr_forward_ubo.hpp
 * @brief Forward PBR（`note/forward_shader.md`）：Frame / Material / Object /
 * Light 的 std140 UBO CPU 镜像
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

#include "render/material/material.hpp"

namespace lumen::render {

inline constexpr int PBR_FORWARD_MAX_LIGHTS { 64 };
inline constexpr int PBR_LEGACY_POINT_LIGHT_CAP { 4 };

/// `Light.position.w`：与 `shaders/glsl/pbr_forward.frag` 中宏一致
inline constexpr float PBR_LIGHT_TYPE_DIRECTIONAL { 0.0F };
inline constexpr float PBR_LIGHT_TYPE_POINT { 1.0F };
inline constexpr float PBR_LIGHT_TYPE_SPOT { 2.0F };

/**
 * @brief std140，`FrameUBO`（set 0 binding 0）
 */
struct PbrFrameUbo {
    glm::mat4 view { 1.0F };
    glm::mat4 proj { 1.0F };
    glm::mat4 viewProj { 1.0F };
    glm::vec4 cameraPosW { 0.0F, 0.0F, 0.0F, 1.0F };
    /// x: exposure，y: iblStrength，z: maxPrefilterMip，w: diffIrradianceMip
    glm::vec4 exposureIblMips { 1.0F, 1.0F, 0.0F, 0.0F };
    /// 与 `shaders/glsl/pbr_forward.*` FrameUBO.debugMode 一致（std140 尾部四
    /// int）
    std::int32_t debugMode {};
    std::int32_t framePad0 {};
    std::int32_t framePad1 {};
    std::int32_t framePad2 {};
};

static_assert(sizeof(PbrFrameUbo) == 240U);

/**
 * @brief std140，`MaterialUBO`（set 1 binding 0）
 */
struct PbrMaterialUbo {
    glm::vec4 baseColorFactor { 1.0F, 1.0F, 1.0F, 1.0F };
    glm::vec4 emissive { 0.0F, 0.0F, 0.0F, 1.0F };
    float metallicFactor { 1.0F };
    float roughnessFactor { 1.0F };
    float occlusionStrength { 1.0F };
    std::uint32_t flags {};
    std::uint32_t alphaMode {};
    float alphaCutoff { 0.5F };
    float std140Pad0 {};
    float std140Pad1 {};
};

static_assert(sizeof(PbrMaterialUbo) == 64U);

/**
 * @brief std140，`ObjectUBO`（set 2 binding 0）
 */
struct PbrObjectUbo {
    glm::mat4 model { 1.0F };
    glm::mat4 normalMatrix { 1.0F };
};

static_assert(sizeof(PbrObjectUbo) == 128U);

struct PbrLightGpu {
    glm::vec4 positionType {};
    glm::vec4 direction {};
    glm::vec4 colorIntensity {};
    glm::vec4 params {};
};

static_assert(sizeof(PbrLightGpu) == 64U);

/**
 * @brief std140，`LightUBO`（set 3 binding 0）
 */
struct PbrLightUbo {
    glm::ivec4 lightCountPad {};
    std::array<PbrLightGpu, PBR_FORWARD_MAX_LIGHTS> lights {};
};

static_assert(sizeof(PbrLightUbo) == 16U + 64U * 64U);

struct PbrDefaultPointLightInit {
    glm::vec3 position {};
    float range { 5.0F };
    glm::vec3 color { 1.0F };
    float intensity { 2.0F };
};

[[nodiscard]] inline const std::array<PbrDefaultPointLightInit,
                                      PBR_LEGACY_POINT_LIGHT_CAP> &
pbr_default_point_lights_table() {
    static constexpr std::array<PbrDefaultPointLightInit,
                                PBR_LEGACY_POINT_LIGHT_CAP>
        k_lights { {
            PbrDefaultPointLightInit {
                .position = glm::vec3 { 1.5F, 2.0F, 1.2F },
                .range = 5.0F,
                .color = glm::vec3 { 1.0F, 0.92F, 0.82F },
                .intensity = 3.0F },
            PbrDefaultPointLightInit {
                .position = glm::vec3 { -2.0F, 1.2F, 0.8F },
                .range = 5.5F,
                .color = glm::vec3 { 0.55F, 0.72F, 1.0F },
                .intensity = 2.4F },
            PbrDefaultPointLightInit {
                .position = glm::vec3 { 0.0F, 0.45F, -2.2F },
                .range = 6.0F,
                .color = glm::vec3 { 1.0F, 0.85F, 0.68F },
                .intensity = 2.0F },
            PbrDefaultPointLightInit {
                .position = glm::vec3 { -0.9F, -0.4F, 1.7F },
                .range = 4.5F,
                .color = glm::vec3 { 0.48F, 0.52F, 0.58F },
                .intensity = 1.9F },
        } };
    return k_lights;
}

inline void pack_pbr_frame_ubo(PbrFrameUbo &frameUbo, const glm::mat4 &view,
                               const glm::mat4 &proj, const glm::vec3 &eye,
                               float exposure, float iblStrength,
                               float maxPrefilterMip, float diffIrradianceMip,
                               std::int32_t debugMode = 0) {
    frameUbo.view = view;
    frameUbo.proj = proj;
    frameUbo.viewProj = proj * view;
    frameUbo.cameraPosW = glm::vec4(eye, 1.0F);
    frameUbo.exposureIblMips =
        glm::vec4(exposure, iblStrength, maxPrefilterMip, diffIrradianceMip);
    frameUbo.debugMode = debugMode;
    frameUbo.framePad0 = 0;
    frameUbo.framePad1 = 0;
    frameUbo.framePad2 = 0;
}

inline void pack_pbr_material_ubo(PbrMaterialUbo &materialUbo,
                                  const Material &mat,
                                  float emissiveIntensity) {
    materialUbo.baseColorFactor = mat.baseColorFactor;
    materialUbo.emissive = glm::vec4(mat.emissiveFactor, emissiveIntensity);
    materialUbo.metallicFactor = mat.metallicFactor;
    materialUbo.roughnessFactor = mat.roughnessFactor;
    materialUbo.occlusionStrength = mat.occlusionStrength;
    materialUbo.flags = 0U;
    materialUbo.alphaMode = static_cast<std::uint32_t>(mat.alphaMode);
    materialUbo.alphaCutoff = 0.5F;
    materialUbo.std140Pad0 = 0.0F;
    materialUbo.std140Pad1 = 0.0F;
}

inline void fill_pbr_light_ubo_default_points(PbrLightUbo &lightUbo,
                                              int activeCount,
                                              float strengthScale) {
    const int n = std::clamp(activeCount, 0, PBR_LEGACY_POINT_LIGHT_CAP);
    lightUbo.lightCountPad.x = n;
    lightUbo.lightCountPad.y = 0;
    lightUbo.lightCountPad.z = 0;
    lightUbo.lightCountPad.w = 0;
    const auto &L = pbr_default_point_lights_table();
    for (int i = 0; i < PBR_FORWARD_MAX_LIGHTS; ++i) {
        if (i < n) {
            const auto &s = L[static_cast<size_t>(i)];
            lightUbo.lights[static_cast<size_t>(i)].positionType =
                glm::vec4(s.position, PBR_LIGHT_TYPE_POINT);
            lightUbo.lights[static_cast<size_t>(i)].direction = glm::vec4(0.0F);
            lightUbo.lights[static_cast<size_t>(i)].colorIntensity =
                glm::vec4(s.color, s.intensity * strengthScale);
            lightUbo.lights[static_cast<size_t>(i)].params =
                glm::vec4((std::max)(s.range, 0.05F), 0.0F, 0.0F, 0.0F);
        } else {
            lightUbo.lights[static_cast<size_t>(i)] = {};
        }
    }
}

/** ObjectUBO 动态绑定的步长（需 ≥ minUniformBufferOffsetAlignment） */
inline std::size_t pbr_object_ubo_dynamic_stride(std::size_t minAlign) {
    const std::size_t base = sizeof(PbrObjectUbo);
    if (minAlign == 0U) {
        return base;
    }
    return (base + minAlign - 1U) / minAlign * minAlign;
}

} // namespace lumen::render
