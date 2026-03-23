/**
 * @file pbr_material_ubo.hpp
 * @brief Set=1 binding=0 材质 UBO，std140，与 demo PBR 片元着色器一致
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <cstdint>

#include <glm/vec4.hpp>

namespace lumen {
namespace render {

/// 与 GLSL `MaterialUBO` 块对齐（std140，四列 vec4）
struct alignas(16) PbrMaterialUbo {
    glm::vec4 base_color_factor { 1.0f, 1.0f, 1.0f, 1.0f };
    /// x=metallic, y=roughness, z=ao；与贴图按槽位相乘（无贴图时仅用标量）
    glm::vec4 mr_ao_factors { 1.0f, 1.0f, 1.0f, 0.0f };
    /// xyz=自发光；w 保留
    glm::vec4 emissive_factor { 0.0f, 0.0f, 0.0f, 0.0f };
    /// x=alpha 模式（0 Opaque / 1 Mask / 2 Blend）；y=alpha cutoff；
    /// z=贴图槽位掩码（`material_texture_mask.hpp`），以 float 位拷贝写入；
    /// w 保留
    glm::vec4 shader_params { 0.0f, 0.5f, 0.0f, 0.0f };
};

static_assert(sizeof(PbrMaterialUbo) == 64, "PbrMaterialUbo std140 size");

inline float uint_bits_to_float(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

} // namespace render
} // namespace lumen
