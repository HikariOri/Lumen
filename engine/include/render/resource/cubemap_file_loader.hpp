/**
 * @file cubemap_file_loader.hpp
 * @brief 从磁盘加载环境立方体贴图：六面 LDR 图或单张 HDR 等距柱状图
 */

#pragma once

#include <string>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class CommandPool;
class Context;
class Texture;
struct SamplerConfig;

/**
 * @brief 从目录加载六面图：px, nx, py, ny, pz, nz（扩展名 .png 或 .jpg，大小写不敏感）
 * @param dir 目录绝对或相对路径（末尾可有或无分隔符）
 * @param out_tex 输出纹理（成功时覆盖；失败时保持原状）
 * @param out_error 可选，失败时人类可读说明
 * @return 六面均存在且尺寸一致则 true
 */
bool load_cubemap_from_face_files(const Context &ctx, const std::string &dir,
                                  VkQueue transfer_queue, CommandPool &cmd_pool,
                                  const SamplerConfig &sampler_cfg, Texture &out_tex,
                                  std::string *out_error = nullptr);

/**
 * @brief 从单张 Radiance `.hdr` 等距柱状图 CPU 采样为 6 面立方体（RGBA32F + Mipmap）
 * @param face_size 每边像素；0 表示按长图宽度自动估算（约 width/4，夹在 128～1024）
 */
bool load_cubemap_from_hdr_equirectangular_file(
    const Context &ctx, const std::string &hdr_path, VkQueue transfer_queue,
    CommandPool &cmd_pool, const SamplerConfig &sampler_cfg, Texture &out_tex,
    std::uint32_t face_size = 0, std::string *out_error = nullptr);

} // namespace render
} // namespace lumen
