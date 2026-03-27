/**
 * @file cubemap_file_loader.hpp
 * @brief 环境立方体贴图加载模块
 *
 * 提供两种常见 HDR/环境贴图加载方式：
 *
 * 1. 六面贴图（face files）
 *    - 典型 PBR skybox / IBL cubemap
 *    - 需要目录中包含 px/nx/py/ny/pz/nz 六张图
 *
 * 2. HDR 等距柱状图（equirectangular）
 *    - 常见 .hdr 环境贴图格式
 *    - CPU 端展开采样生成 cubemap
 *
 * 最终结果统一写入 Vulkan Texture（通常用于 IBL / skybox / reflection）
 */

#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class CommandPool;
class Context;
class Texture;
struct SamplerConfig;

/**
 * @brief 从六张面贴图加载 Cubemap
 *
 * 目录结构示例：
 * ```
 * dir/
 *   px.png   (+X)
 *   nx.png   (-X)
 *   py.png   (+Y)
 *   ny.png   (-Y)
 *   pz.png   (+Z)
 *   nz.png   (-Z)
 * ```
 *
 * 要求：
 * - 六张图必须全部存在
 * - 分辨率必须一致
 * - 支持 png / jpg（大小写不敏感）
 *
 * 处理流程：
 * 1. CPU 加载六张图
 * 2. 创建 staging buffer
 * 3. 上传到 GPU cubemap image
 * 4. 创建 image view + sampler
 *
 * @param ctx Vulkan 上下文（device / allocator 等）
 * @param dir 贴图目录路径（可带或不带尾部 /）
 * @param transfer_queue 用于 staging copy 的队列
 * @param cmd_pool 用于提交上传命令的 command pool
 * @param sampler_cfg cubemap sampler 参数（过滤 / wrap 等）
 * @param out_tex 输出纹理对象（成功时覆盖写入）
 * @param out_error 失败时错误信息（可为空）
 *
 * @return true 成功加载并上传 GPU
 * @return false 任一阶段失败
 */
bool load_cubemap_from_face_files(const Context &ctx, const std::string &dir,
                                  VkQueue transfer_queue, CommandPool &cmd_pool,
                                  const SamplerConfig &sampler_cfg,
                                  Texture &out_tex,
                                  std::string *out_error = nullptr);

/**
 * @brief 从 HDR 等距柱状图生成 Cubemap
 *
 * 输入格式：
 * - .hdr（Radiance HDR）
 * - equirectangular projection（长宽比通常 2:1）
 *
 * 处理流程：
 * 1. CPU 加载 HDR（float4）
 * 2. 根据球面采样公式展开为 6 个面
 * 3. 生成 cubemap face 数据
 * 4. 上传 GPU + 生成 mipmap（可选）
 *
 * face_size：
 * - 0：自动推断（通常 width / 4）
 * - 推荐范围：128 ~ 1024
 *
 * 注意：
 * - CPU 计算成本较高（O(N² × 6)）
 * - 建议离线预处理或缓存
 *
 * @param ctx Vulkan 上下文
 * @param hdr_path HDR 文件路径
 * @param transfer_queue 上传队列
 * @param cmd_pool 命令池
 * @param sampler_cfg sampler 配置
 * @param out_tex 输出 cubemap
 * @param face_size 每个面的分辨率
 * @param out_error 错误信息
 *
 * @return true 成功
 * @return false 失败
 */
bool load_cubemap_from_hdr_equirectangular_file(
    const Context &ctx, const std::string &hdr_path, VkQueue transfer_queue,
    CommandPool &cmd_pool, const SamplerConfig &sampler_cfg, Texture &out_tex,
    std::uint32_t face_size = 0, std::string *out_error = nullptr);

} // namespace render
} // namespace lumen
