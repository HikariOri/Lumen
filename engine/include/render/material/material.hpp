/**
 * @file material.hpp
 * @brief CPU 侧材质（glTF 风格因子与贴图槽；纹理 × factor → GPU descriptor）
 */

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace lumen::render {

class Texture;

/// glTF `alphaMode` 近似
enum class MaterialAlphaMode : std::uint8_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

/**
 * @brief CPU 侧材质（与 glTF 贴图槽位一致）
 *
 * 缺失贴图时由 `PbrMaterialDefaultTextures` 提供默认采样，避免 descriptor
 * 绑定无效资源。
 */
struct Material {
    glm::vec4 baseColorFactor { 1.0F, 1.0F, 1.0F, 1.0F };
    float metallicFactor { 1.0F };
    float roughnessFactor { 1.0F };
    glm::vec3 emissiveFactor { 0.0F, 0.0F, 0.0F };
    float occlusionStrength { 1.0F };

    const Texture *baseColorTex {};
    /// glTF metallicRoughness：G=roughness，B=metallic
    const Texture *metallicRoughnessTex {};
    const Texture *normalTex {};
    const Texture *occlusionTex {};
    const Texture *emissiveTex {};

    bool doubleSided {};
    MaterialAlphaMode alphaMode { MaterialAlphaMode::Opaque };
};

/**
 * @brief 紧凑 GPU 材质记录（供后续 bindless / SSBO 表等扩展）
 */
// struct MaterialGpuCompact {
//     glm::vec4 baseColorFactor {};
//     float metallicFactor {};
//     float roughnessFactor {};
//     std::int32_t baseColorTexIndex {};
//     std::int32_t mrTexIndex {};
//     std::int32_t normalTexIndex {};
// };

} // namespace lumen::render
