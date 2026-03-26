/**
 * @file ktx_texture_rgba8.hpp
 * @brief 使用 libktx 将 KTX/KTX2 纹理解码为 RGBA8 像素数据
 *
 * 本模块提供从 KTX/KTX2 文件或内存中读取纹理，
 * 并统一解码为 CPU 可直接访问的 RGBA8（8-bit * 4 通道）格式。
 *
 * 设计目标：
 * - 屏蔽 KTX1 / KTX2 / Basis / 压缩格式差异
 * - 输出统一格式（RGBA8，紧密排列）
 * - 便于后续上传 Vulkan（staging buffer → image）
 *
 * 说明：
 * - KTX 是 Khronos 定义的 GPU 纹理容器格式，可包含 mipmap、cubemap 等
 * - KTX2 可能包含压缩格式（如 Basis / ASTC），需要解码或转码
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {
namespace core {

/**
 * @brief 从 KTX/KTX2 文件解码为 RGBA8（仅 base mip level）
 *
 * @param path 输入文件路径（.ktx / .ktx2）
 * @param[out] out_rgba 输出像素数据（RGBA8，4 字节/像素，行紧密排列）
 * @param[out] out_width 输出宽度（像素）
 * @param[out] out_height 输出高度（像素）
 * @param[out] err_out 错误信息（可选）
 *
 * @return 成功返回 true，失败返回 false
 *
 * @details
 * - 只解码第 0 层 mip（base level）
 * - 输出格式固定为 RGBA8：
 *   - R, G, B, A 各 8-bit
 *   - 总计 4 bytes / pixel
 * - 行紧密排列（row-major, 无 padding）
 *
 * 内部可能执行：
 * - KTX2 → 转码（Basis → RGBA）
 * - 压缩格式解压（如 ASTC / ETC → RGBA8）
 *
 * @note
 * - 不返回 GPU 原生压缩格式（如 BC/ASTC），而是统一展开为 RGBA8
 * - 适用于 CPU 处理 / Vulkan staging 上传
 */
bool decode_ktx_file_to_rgba8(const char *path,
                              std::vector<std::uint8_t> &out_rgba,
                              std::uint32_t &out_width,
                              std::uint32_t &out_height,
                              std::string *err_out = nullptr);

/**
 * @brief 从内存中的 KTX/KTX2 数据解码为 RGBA8
 *
 * @param bytes KTX 数据指针（通常来自 glTF buffer / 网络 / 内存映射）
 * @param size 数据大小（字节）
 * @param[out] out_rgba 输出 RGBA8 像素数据（紧密排列）
 * @param[out] out_width 输出宽度
 * @param[out] out_height 输出高度
 * @param[out] err_out 错误信息（可选）
 *
 * @return 成功返回 true，失败返回 false
 *
 * @details
 * 用于处理：
 * - glTF 内嵌纹理（KHR_texture_basisu）
 * - 内存加载资源系统
 *
 * 数据流程：
 * ```
 * KTX bytes
 *   ↓
 * libktx 解析 container
 *   ↓
 * （必要时）转码 / 解压
 *   ↓
 * RGBA8 输出
 * ```
 *
 * @note
 * - 同样只输出 base mip level
 * - 输出数据可直接 memcpy 到 staging buffer
 */
bool decode_ktx_memory_to_rgba8(const std::uint8_t *bytes, std::size_t size,
                                std::vector<std::uint8_t> &out_rgba,
                                std::uint32_t &out_width,
                                std::uint32_t &out_height,
                                std::string *err_out = nullptr);

} // namespace core
} // namespace lumen
