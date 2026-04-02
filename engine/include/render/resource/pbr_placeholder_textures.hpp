/**
 * @file pbr_placeholder_textures.hpp
 * @brief PBR 占位纹理集合（用于缺失材质贴图的回退）
 *
 * 当模型材质缺少某些 PBR 贴图时，使用 1×1 默认纹理保证：
 * - Shader 无需分支
 * - 渲染结果稳定
 * - 避免未绑定资源导致错误
 *
 * ---
 * @section pbr_placeholder_textures_maps 贴图定义
 *
 * 本类提供五种标准 PBR 贴图：
 *
 * - Albedo（sRGB）
 * - Normal
 * - MetallicRoughness
 * - AO（Ambient Occlusion）
 * - Emissive
 *
 * ---
 * @section pbr_placeholder_textures_defaults 默认值语义（重要）
 *
 * 每张贴图的默认值遵循物理合理性：
 *
 * | 贴图 | 默认值 | 含义 |
 * |------|--------|------|
 * | Albedo | (1,1,1) | 白色，不影响颜色 |
 * | Normal | (0.5,0.5,1) | 默认法线（+Z） |
 * | MetallicRoughness | (0,1) | 非金属 + 完全粗糙 |
 * | AO | 1 | 无遮蔽 |
 * | Emissive | (0,0,0) | 无自发光 |
 *
 * ---
 * @note
 * - 所有贴图均为 1×1
 * - 由 GPU 采样时自动扩展
 *
 * ---
 * @warning
 * - Albedo 必须为 sRGB 格式
 * - Normal / MR / AO / Emissive 必须为线性空间
 */

#pragma once

#include "render/vulkan.hpp"

#include "render/resource/texture.hpp"

namespace lumen {
namespace render {

class CommandPool;
class Context;

/**
 * @class PbrPlaceholderTextures
 * @brief PBR 默认占位纹理集合
 *
 * ---
 * @section usage 使用方式
 *
 * @code
 * PbrPlaceholderTextures placeholders;
 * placeholders.create(ctx, queue, cmdPool);
 *
 * material.albedo = placeholders.albedo();
 * @endcode
 *
 * ---
 * @section lifecycle 生命周期
 *
 * - create()：创建并上传 GPU
 * - is_complete()：检查是否全部成功
 *
 * ---
 * @note
 * 所有 Texture 由该类持有（RAII）
 */
class PbrPlaceholderTextures {
public:
    /**
     * @brief 创建所有占位纹理
     *
     * @param ctx Vulkan 上下文
     * @param transfer_queue 用于上传的队列
     * @param cmd_pool 命令池（用于 staging / copy）
     *
     * @retval true 全部创建成功
     * @retval false 任意失败
     *
     * ---
     * @note
     * 内部流程：
     * - 创建 1×1 CPU 数据
     * - 上传到 GPU（staging buffer）
     * - 创建 sampler + image view
     */
    bool create(const Context &ctx, vk::Queue transfer_queue,
                CommandPool &cmd_pool);

    /**
     * @brief 是否所有纹理创建成功
     */
    [[nodiscard]] bool is_complete() const {
        return albedo_.is_valid() && normal_.is_valid() &&
               metallic_roughness_.is_valid() && ao_.is_valid() &&
               emissive_.is_valid();
    }

    /**
     * @brief 获取 Albedo（sRGB）
     */
    [[nodiscard]] const Texture &albedo() const { return albedo_; }

    /**
     * @brief 获取 Normal（切线空间）
     *
     * 默认值：(0.5, 0.5, 1.0)
     */
    [[nodiscard]] const Texture &normal() const { return normal_; }

    /**
     * @brief 获取 Metallic-Roughness
     *
     * - R：Metallic（默认 0）
     * - G：Roughness（默认 1）
     */
    [[nodiscard]] const Texture &metallic_roughness() const {
        return metallic_roughness_;
    }

    /**
     * @brief 获取 AO（环境光遮蔽）
     *
     * 默认值：1（无遮挡）
     */
    [[nodiscard]] const Texture &ao() const { return ao_; }

    /**
     * @brief 获取 Emissive（自发光）
     *
     * 默认值：(0,0,0)
     */
    [[nodiscard]] const Texture &emissive() const { return emissive_; }

private:
    /// Albedo（sRGB）
    Texture albedo_;

    /// Normal（线性）
    Texture normal_;

    /// Metallic-Roughness（线性）
    Texture metallic_roughness_;

    /// Ambient Occlusion（线性）
    Texture ao_;

    /// Emissive（线性）
    Texture emissive_;
};

} // namespace render
} // namespace lumen
