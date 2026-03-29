/**
 * @file gltf_material.hpp
 * @brief glTF / 加载管线用的 PBR 材质描述（非 EnTT 组件）
 */

#pragma once

#include <cstdint>
#include <string>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace lumen {
namespace core {

enum class GltfMaterialAlphaMode : std::uint8_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

struct GltfMaterialData {
    glm::vec4 base_color_factor { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic_factor { 1.0f };
    float roughness_factor { 1.0f };
    float ao_factor { 1.0f };
    float _pad0 {};
    glm::vec3 emissive_factor { 0.0f, 0.0f, 0.0f };
    float _pad1 {};

    GltfMaterialAlphaMode alpha_mode { GltfMaterialAlphaMode::Opaque };
    float alpha_cutoff { 0.5f };
    bool double_sided { false };
    bool spec_gloss_texture_in_mr_slot { false };
    std::uint8_t _pad2[1] {};

    std::string albedo_path;
    std::string normal_path;
    std::string metallic_roughness_path;
    std::string ao_path;
    std::string emissive_path;
};

} // namespace core
} // namespace lumen
