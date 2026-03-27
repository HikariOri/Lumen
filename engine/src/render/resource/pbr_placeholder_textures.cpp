/**
 * @file pbr_placeholder_textures.cpp
 * @brief PBR 占位纹理实现：1×1 数据构造与 GPU 上传
 *
 * ---
 * @section impl_overview 实现概述
 *
 * 本文件负责：
 * - 构造 1×1 CPU 像素数据
 * - 调用 Texture::create_from_memory 上传到 GPU
 * - 创建 sampler + image view
 *
 * ---
 * @section impl_pbr_semantics PBR 默认值（重要）
 *
 * 所有默认值遵循 glTF 规范与物理意义：
 *
 * - Albedo：白色 (1,1,1)
 * - Normal：(0.5,0.5,1) → +Z 法线
 * - MetallicRoughness：
 *     G = Roughness = 1（完全粗糙）
 *     B = Metallic  = 0（非金属）
 * - AO：1（无遮挡）
 * - Emissive：0（无自发光）
 *
 * ---
 * @section impl_color_space 颜色空间
 *
 * - Albedo：sRGB（自动 gamma decode）
 * - 其他：Linear
 *
 * ---
 * @section impl_upload 上传流程
 *
 * 每张纹理：
 * 1. CPU 侧 1×1 RGBA
 * 2. staging buffer
 * 3. vkCmdCopyBufferToImage
 * 4. layout 转换为 SHADER_READ_ONLY
 *
 * ---
 * @note
 * - 所有纹理均无 mipmap（1×1 无意义）
 * - sampler 使用线性过滤 + repeat
 */

#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/sampler.hpp"

namespace lumen::render {

/**
 * @brief 创建 PBR 占位纹理
 *
 * @param ctx Vulkan 上下文
 * @param transfer_queue 上传队列
 * @param cmd_pool 命令池
 *
 * @retval true 所有纹理创建成功
 * @retval false 任意失败
 */
bool PbrPlaceholderTextures::create(const Context &ctx, VkQueue transfer_queue,
                                    CommandPool &cmd_pool) {

    // -------------------------------------------------------------------------
    // 1. CPU 默认像素（RGBA8）
    // -------------------------------------------------------------------------

    /// Albedo：白色（不影响颜色）
    constexpr std::uint8_t white_rgba[] = { 255, 255, 255, 255 };

    /// Normal：(0.5, 0.5, 1.0) → 切线空间默认法线 (+Z)
    constexpr std::uint8_t flat_normal_rgba[] = { 128, 128, 255, 255 };

    /**
     * MetallicRoughness（glTF 约定）：
     *
     * R: Occlusion（未使用）
     * G: Roughness
     * B: Metallic
     *
     * 默认：
     * - Roughness = 1 → 255
     * - Metallic  = 0 → 0
     */
    constexpr std::uint8_t default_mr_rgba[] = { 255, 255, 0, 255 };

    /// AO：1（无遮挡）
    constexpr std::uint8_t white_ao_rgba[] = { 255, 255, 255, 255 };

    /// Emissive：0（无发光）
    constexpr std::uint8_t black_emissive_rgba[] = { 0, 0, 0, 255 };

    // -------------------------------------------------------------------------
    // 2. Sampler 配置
    // -------------------------------------------------------------------------

    /**
     * 线性过滤 + Repeat：
     * - 1×1 纹理其实无所谓，但统一行为
     */
    SamplerConfig linear_repeat {};
    linear_repeat.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    linear_repeat.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    // -------------------------------------------------------------------------
    // 3. 创建 GPU 纹理
    // -------------------------------------------------------------------------

    /**
     * @note
     * create_from_memory 内部通常执行：
     * - 创建 VkImage
     * - staging buffer 上传
     * - layout 转换
     * - 创建 sampler / view
     */

    /// Albedo（sRGB）
    const bool a = albedo_.create_from_memory(
        ctx, white_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_SRGB, linear_repeat, false);

    /// Normal（Linear）
    const bool n = normal_.create_from_memory(
        ctx, flat_normal_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, linear_repeat, false);

    /// Metallic-Roughness（Linear）
    const bool mr = metallic_roughness_.create_from_memory(
        ctx, default_mr_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, linear_repeat, false);

    /// AO（Linear）
    const bool ao = ao_.create_from_memory(
        ctx, white_ao_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, linear_repeat, false);

    /// Emissive（Linear）
    const bool e = emissive_.create_from_memory(
        ctx, black_emissive_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, linear_repeat, false);

    // -------------------------------------------------------------------------
    // 4. 汇总结果
    // -------------------------------------------------------------------------

    return a && n && mr && ao && e;
}

} // namespace lumen::render
