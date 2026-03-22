/**
 * @file pbr_resources.hpp
 * @brief Demo3D：程序化环境立方体贴图、BRDF 积分 LUT 生成（供 IBL 使用）
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace demo3d::pbr {

/// 生成 256×256 RGBA8 BRDF 积分 LUT（Epic Games split-sum 近似，R=A_scale，G=B_scale）
void generate_brdf_lut_rgba8(std::vector<std::uint8_t> &out_rgba,
                             std::uint32_t resolution = 256);

/**
 * @brief 为立方体贴图每面填充 RGBA8 像素（顺序 +X,-X,+Y,-Y,+Z,-Z）
 *
 * 天顶偏蓝、地平线灰、低仰角暖色 + 可控太阳方向的高光瓣。
 */
void fill_procedural_sky_faces(std::uint32_t face_size,
                               std::array<std::vector<std::uint8_t>, 6> &faces);

} // namespace demo3d::pbr
