/**
 * @file vertex_attribute_format.hpp
 * @brief 顶点输入属性格式：标量、向量、矩阵合一枚举
 *
 * - 标量 / 向量：对应单个 `VkVertexInputAttributeDescription`。
 * - 矩阵：按列主序拆成多列，占用连续 `location`（`mat4` = 4 个 `vec4` 属性），
 *   `offset` 为矩阵在顶点结构体中的起始偏移，列间距为 `sizeof(列向量)`（与 glm
 *   列主序布局一致）。
 */

#pragma once

#include <cstdint>

namespace lumen {
namespace render {

enum class VertexAttributeFormat : std::uint16_t {
    // --- 64-bit float ---
    F64,
    F64Vec2,
    F64Vec3,
    F64Vec4,
    F64Mat2,
    F64Mat3,
    F64Mat4,

    // --- 32-bit float ---
    F32,
    F32Vec2,
    F32Vec3,
    F32Vec4,
    F32Mat2,
    F32Mat3,
    F32Mat4,

    // --- 16-bit float ---
    F16,
    F16Vec2,
    F16Vec3,
    F16Vec4,
    F16Mat2,
    F16Mat3,
    F16Mat4,

    // --- 32-bit signed / unsigned integer ---
    I32,
    I32Vec2,
    I32Vec3,
    I32Vec4,
    I32Mat2,
    I32Mat3,
    I32Mat4,
    U32,
    U32Vec2,
    U32Vec3,
    U32Vec4,
    U32Mat2,
    U32Mat3,
    U32Mat4,

    // --- 16-bit signed / unsigned integer ---
    I16,
    I16Vec2,
    I16Vec3,
    I16Vec4,
    I16Mat2,
    I16Mat3,
    I16Mat4,
    U16,
    U16Vec2,
    U16Vec3,
    U16Vec4,
    U16Mat2,
    U16Mat3,
    U16Mat4,

    // --- 8-bit signed / unsigned integer ---
    I8,
    I8Vec2,
    I8Vec3,
    I8Vec4,
    I8Mat2,
    I8Mat3,
    I8Mat4,
    U8,
    U8Vec2,
    U8Vec3,
    U8Vec4,
    U8Mat2,
    U8Mat3,
    U8Mat4,

    // --- 8-bit / 16-bit UNORM / SNORM（无矩阵：顶点侧极少用矩阵打包）---
    Unorm8,
    Unorm8Vec2,
    Unorm8Vec3,
    Unorm8Vec4,
    Snorm8,
    Snorm8Vec2,
    Snorm8Vec3,
    Snorm8Vec4,
    Unorm16,
    Unorm16Vec2,
    Unorm16Vec3,
    Unorm16Vec4,
    Snorm16,
    Snorm16Vec2,
    Snorm16Vec3,
    Snorm16Vec4,

    // --- 8-bit sRGB（顶点颜色等）---
    Srgb8,
    Srgb8Vec2,
    Srgb8Vec3,
    Srgb8Vec4,
};

/**
 * @brief 该属性在管线中占用的连续 location 数量（矩阵为多列）
 */
[[nodiscard]] constexpr std::uint32_t
vertex_attribute_format_location_count(VertexAttributeFormat fmt) noexcept {
    switch (fmt) {
    case VertexAttributeFormat::F64Mat2:
    case VertexAttributeFormat::F32Mat2:
    case VertexAttributeFormat::F16Mat2:
    case VertexAttributeFormat::I32Mat2:
    case VertexAttributeFormat::U32Mat2:
    case VertexAttributeFormat::I16Mat2:
    case VertexAttributeFormat::U16Mat2:
    case VertexAttributeFormat::I8Mat2:
    case VertexAttributeFormat::U8Mat2:
        return 2;
    case VertexAttributeFormat::F64Mat3:
    case VertexAttributeFormat::F32Mat3:
    case VertexAttributeFormat::F16Mat3:
    case VertexAttributeFormat::I32Mat3:
    case VertexAttributeFormat::U32Mat3:
    case VertexAttributeFormat::I16Mat3:
    case VertexAttributeFormat::U16Mat3:
    case VertexAttributeFormat::I8Mat3:
    case VertexAttributeFormat::U8Mat3:
        return 3;
    case VertexAttributeFormat::F64Mat4:
    case VertexAttributeFormat::F32Mat4:
    case VertexAttributeFormat::F16Mat4:
    case VertexAttributeFormat::I32Mat4:
    case VertexAttributeFormat::U32Mat4:
    case VertexAttributeFormat::I16Mat4:
    case VertexAttributeFormat::U16Mat4:
    case VertexAttributeFormat::I8Mat4:
    case VertexAttributeFormat::U8Mat4:
        return 4;
    default:
        return 1;
    }
}

} // namespace render
} // namespace lumen
