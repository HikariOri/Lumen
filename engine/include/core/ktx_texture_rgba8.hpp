/**
 * @file ktx_texture_rgba8.hpp
 * @brief 使用 third_party KTX（libktx）将 KTX/KTX2 解码为 RGBA8 像素行（紧密打包）
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {
namespace core {

/**
 * @brief 从 KTX/KTX2 文件解码第 0 层 mip 为 RGBA8（每像素 4 字节，行紧密）
 */
bool decode_ktx_file_to_rgba8(const char *path, std::vector<std::uint8_t> &out_rgba,
                              std::uint32_t &out_width, std::uint32_t &out_height,
                              std::string *err_out = nullptr);

/**
 * @brief 从内存中的 KTX/KTX2 数据解码（与 glTF 内嵌 buffer 一致）
 */
bool decode_ktx_memory_to_rgba8(const std::uint8_t *bytes, std::size_t size,
                                std::vector<std::uint8_t> &out_rgba,
                                std::uint32_t &out_width, std::uint32_t &out_height,
                                std::string *err_out = nullptr);

} // namespace core
} // namespace lumen
