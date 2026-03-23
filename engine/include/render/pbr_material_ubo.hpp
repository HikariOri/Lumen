/**
 * @file pbr_material_ubo.hpp
 * @brief Set=1 binding=0 材质 UBO，std140，与 demo PBR 片元着色器一致
 */

#pragma once

#include <cstddef>

#include <glm/vec4.hpp>

namespace lumen {
namespace render {

/// 与 GLSL `MaterialUBO` 块对齐（std140）
struct alignas(16) PbrMaterialUbo {
    glm::vec4 base_color_factor { 1.0f, 1.0f, 1.0f, 1.0f };
    /// x=metallic, y=roughness, z=ao 与贴图相乘
    glm::vec4 mr_ao_factors { 1.0f, 1.0f, 1.0f, 0.0f };
    glm::vec4 emissive_factor { 0.0f, 0.0f, 0.0f, 0.0f };
};

static_assert(sizeof(PbrMaterialUbo) == 48, "PbrMaterialUbo std140 size");

} // namespace render
} // namespace lumen
