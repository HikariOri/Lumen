/**
 * @file ibl_cpu.hpp
 * @brief tinygltf_raw_load_test 专用：CPU 端生成 IBL 辅助数据（与 demo3d 无关）
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace tinygltf_test::ibl {

/// 生成 split-sum BRDF 积分 LUT（RGBA8：R=A_scale，G=B_scale，B/A=0，A=255）
void generate_brdf_lut_rgba8(std::vector<std::uint8_t> &out_rgba,
                             std::uint32_t resolution = 256);

/**
 * @brief 填充立方体六面 RGBA8（顺序 +X,-X,+Y,-Y,+Z,-Z，与引擎 `create_cubemap_from_rgba8_faces` 一致）
 */
void fill_procedural_environment_faces(
    std::uint32_t face_size,
    std::array<std::vector<std::uint8_t>, 6> &out_faces_rgba8);

/**
 * @brief 辐照度立方体 E(N)=∫ L(ω)(N·ω)+ dω，cosine 加权蒙特卡洛（线性 RGB 写入 RGBA8）
 */
void fill_irradiance_environment_faces(
    std::uint32_t face_size,
    std::array<std::vector<std::uint8_t>, 6> &out_faces_rgba8);

/**
 * @brief mip0 为锐环境；mip≥1 为 GGX 重要性采样的镜面预过滤（与 split-sum BRDF LUT 一致）
 */
void build_radiance_env_mipmap_chain_rgba8(
    std::uint32_t base_face_size,
    std::vector<std::array<std::vector<std::uint8_t>, 6>> &out_mips);

} // namespace tinygltf_test::ibl
