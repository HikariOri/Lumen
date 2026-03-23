/**
 * @file material_texture_mask.hpp
 * @brief PBR 材质：贴图槽启用位（与 `cube.frag` 中 `shaderParams.z` 一致）
 */

#pragma once

#include <cstdint>

#include "scene/components.hpp"

namespace lumen {
namespace render {

/// 与片元着色器 `floatBitsToUint(matUbo.shaderParams.z)` 的位一致
constexpr std::uint32_t kMatTexBitAlbedo = 1u << 0;
constexpr std::uint32_t kMatTexBitNormal = 1u << 1;
constexpr std::uint32_t kMatTexBitMetallicRoughness = 1u << 2;
constexpr std::uint32_t kMatTexBitOcclusion = 1u << 3;
constexpr std::uint32_t kMatTexBitEmissive = 1u << 4;
/// 与 `cube.frag` 一致：MR 槽绑定的是 KHR spec/gloss 贴图，粗糙度由 A 通道推导
constexpr std::uint32_t kMatTexBitMrIsSpecularGlossinessMap = 1u << 5;

/// 路径非空则视为使用该槽位贴图；否则片元用标量因子（设计文档「标量/贴图二选一」）
inline std::uint32_t material_texture_mask_from_component(
    const lumen::scene::MaterialComponent &m) {
    std::uint32_t mask = 0;
    if (!m.albedo_path.empty()) {
        mask |= kMatTexBitAlbedo;
    }
    if (!m.normal_path.empty()) {
        mask |= kMatTexBitNormal;
    }
    if (!m.metallic_roughness_path.empty()) {
        mask |= kMatTexBitMetallicRoughness;
        if (m.spec_gloss_texture_in_mr_slot) {
            mask |= kMatTexBitMrIsSpecularGlossinessMap;
        }
    }
    if (!m.ao_path.empty()) {
        mask |= kMatTexBitOcclusion;
    }
    if (!m.emissive_path.empty()) {
        mask |= kMatTexBitEmissive;
    }
    return mask;
}

} // namespace render
} // namespace lumen
