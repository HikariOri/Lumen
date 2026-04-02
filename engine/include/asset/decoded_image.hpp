/**
 * @file asset/decoded_image.hpp
 * @brief CPU 侧 RGBA8 像素（与 `render::Image` / `Texture` 区分）
 *
 * @note `lumen::render::Image` 为 GPU 资源（VMA/VkImage）；`Texture` 为采样用组合对象。
 * 磁盘 PNG/JPG 应先「解码」为本结构的像素，再按需 `Texture::create_from_memory` 上传。
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumen::asset {

struct DecodedImage {
    std::vector<std::uint8_t> rgba {};
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
};

/// 与计划文档中的 `CpuRaster` 同义
using CpuRaster = DecodedImage;

/**
 * @brief 自 PNG/JPG/HDR 等解码为 RGBA8（`stb_image`，与 `Texture::create_from_file` 翻转约定一致）
 */
[[nodiscard]] bool decode_image_file_to_rgba8(const char *file_path,
                                              DecodedImage &out,
                                              std::string *error_out = nullptr);

/**
 * @brief 相对资源根路径 → 绝对路径后解码（`get_resource_path`）
 */
[[nodiscard]] bool decode_image_resource_rel_path_to_rgba8(
    std::string_view resource_rel_path, DecodedImage &out,
    std::string *error_out = nullptr);

} // namespace lumen::asset
