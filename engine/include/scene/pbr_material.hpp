/**
 * @file pbr_material.hpp
 * @brief glTF 风格 PBR 材质 CPU 侧定义（纹理 × factor → GPU descriptor）
 */

#pragma once

#include <cstdint>

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace lumen::render {
class Texture;
}

namespace lumen::scene {

/// glTF `alphaMode` 近似
enum class MaterialAlphaMode : std::uint8_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

/**
 * @brief CPU 侧 PBR 材质（与 glTF 贴图槽位一致）
 *
 * 缺失贴图时由 `PbrMaterialDefaultTextures` 提供默认采样，避免 descriptor
 * 绑定无效资源。
 */
struct PBRMaterial {
    glm::vec4 base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    float metallic_factor { 1.0F };
    float roughness_factor { 1.0F };
    glm::vec3 emissive_factor { 0.0F, 0.0F, 0.0F };
    float occlusion_strength { 1.0F };

    const render::Texture *base_color_tex {};
    /// glTF metallicRoughness：G=roughness，B=metallic
    const render::Texture *metallic_roughness_tex {};
    const render::Texture *normal_tex {};
    const render::Texture *occlusion_tex {};
    const render::Texture *emissive_tex {};

    bool double_sided {};
    MaterialAlphaMode alpha_mode { MaterialAlphaMode::Opaque };
};

/**
 * @brief 紧凑 GPU 材质记录（供后续 bindless / SSBO 表等扩展）
 */
// struct MaterialGpuCompact {
//     glm::vec4 base_color_factor {};
//     float metallic_factor {};
//     float roughness_factor {};
//     std::int32_t base_color_tex_index {};
//     std::int32_t mr_tex_index {};
//     std::int32_t normal_tex_index {};
// };

} // namespace lumen::scene
