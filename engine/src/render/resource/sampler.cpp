/**
 * @file sampler.cpp
 * @brief Sampler 实现（VkSampler 创建与生命周期管理）
 *
 * ============================
 * Vulkan Sampler 本质说明
 * ============================
 *
 * VkSampler 是 GPU 固定功能采样状态对象（Fixed-function state object）：
 *
 * 它不存储纹理数据，而是定义：
 *  - Texture sampling 如何进行（filter）
 *  - UV 超出范围如何处理（address mode）
 *  - mipmap LOD 如何选择（LOD selection）
 *  - 是否启用各向异性过滤（anisotropy）
 *
 * Sampler 在 Vulkan 中属于“Pipeline 外状态”，但会被 descriptor set 引用。
 *
 * ⚠ 性能关键点：
 *  - VkSampler 是“重资源”（driver-level state object）
 *  - 不建议频繁创建（应做 cache / dedup）
 */

#include "render/resource/sampler.hpp"
#include "render/context.hpp"

namespace lumen {
namespace render {

/**
 * @brief 创建 Vulkan Sampler
 *
 * ============================
 * 创建流程说明（GPU行为映射）
 * ============================
 *
 * SamplerConfig → VkSamplerCreateInfo 映射到 GPU 固定采样硬件单元
 *
 * GPU执行路径：
 *  fragment shader → descriptor → sampler state → texture fetch unit
 *
 * ============================
 * 关键字段说明（驱动层行为）
 * ============================
 */
bool Sampler::create(const Context &ctx, const SamplerConfig &config) {

    /**
     * Vulkan logical device（所有 Vulkan object 的归属）
     */
    device_ = ctx.device();

    /**
     * Physical device limits（GPU硬件能力）
     *
     * 这里主要用于：
     *  - clamp anisotropy 上限
     *  - 避免非法参数导致 vkCreateSampler failure
     */
    const auto &props = ctx.physical_device_properties();

    VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    /**
     * ============================
     * Filter Mode（过滤模式）
     * ============================
     *
     * magFilter：
     *   texel < pixel（放大）时使用
     *
     * minFilter：
     *   texel > pixel（缩小）时使用
     *
     * GPU行为：
     *   NEAREST = 1 texel lookup
     *   LINEAR  = 2x2 bilinear interpolation
     */
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;

    /**
     * ============================
     * Mipmap mode（LOD选择策略）
     * ============================
     *
     * GPU会根据屏幕导数计算LOD：
     *   LOD ≈ log2(textureSize / screenFootprint)
     *
     * NEAREST：
     *   - 选择单层 mip
     *   - sharp transition
     *
     * LINEAR：
     *   - 在 mip 层之间插值
     *   - 更平滑（PBR推荐）
     */
    samplerInfo.mipmapMode = config.mipmapMode;

    /**
     * ============================
     * Address Mode（UV超界行为）
     * ============================
     *
     * 决定 texture coordinate ∉ [0,1] 时行为：
     *
     * REPEAT：
     *   fract(UV) → tiling
     *
     * CLAMP_TO_EDGE：
     *   clamp(UV) → 边缘拉伸（避免 seam）
     *
     * MIRRORED_REPEAT：
     *   镜像重复（减少重复感）
     *
     * 常见用途：
     *   terrain / tiling texture → REPEAT
     *   UI / atlas → CLAMP_TO_EDGE
     */
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;

    /**
     * ============================
     * Anisotropy（各向异性过滤）
     * ============================
     *
     * 解决问题：
     *   斜视角纹理模糊（road / floor / wall）
     *
     * GPU行为：
     *   在高斜率方向增加采样 footprint
     *
     * 性能成本：
     *   ↑ 采样次数（随角度增加）
     *
     * 常见值：
     *   1   = disabled
     *   4   = low cost
     *   8   = balanced
     *   16  = high quality
     *
     * ⚠ 硬件限制：
     *   props.limits.maxSamplerAnisotropy
     *   通常 = 8 or 16
     */
    samplerInfo.maxAnisotropy =
        std::min(config.maxAnisotropy, props.limits.maxSamplerAnisotropy);

    /**
     * ⚠ Vulkan requirement：
     * anisotropyEnable 必须显式设置（通常在这里缺省逻辑）
     *
     * 如果 maxAnisotropy > 1：
     *   samplerInfo.anisotropyEnable = VK_TRUE
     * else:
     *   VK_FALSE
     */
    samplerInfo.anisotropyEnable = config.maxAnisotropy > 1;

    /**
     * ============================
     * LOD Clamp（mipmap范围限制）
     * ============================
     *
     * minLod：
     *   防止过度锐化 / 放大最高 mip
     *
     * maxLod：
     *   限制 mip 层使用（UI常用0~2）
     *
     * VK_LOD_CLAMP_NONE：
     *   使用全部 mip levels
     */
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = config.maxLod;

    /**
     * ============================
     * Border Color（边界颜色）
     * ============================
     *
     * 当 addressMode = CLAMP_TO_BORDER 时使用
     *
     * 这里设为 opaque black：
     *   - 常用于 shadow map
     *   - 避免 undefined border sampling
     */
    // TODO: 让用户传进来
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    /**
     * ============================
     * VkSampler 创建
     * ============================
     *
     * GPU driver 会：
     *  - 分配 sampler state object
     *  - 写入 descriptor cache friendly layout
     *
     * @note 注意：
     * VkSamplerCreate 是“驱动级操作”，可能有较高开销
     */
    VkResult result =
        vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);

    return result == VK_SUCCESS;
}

/**
 * @brief 销毁 Sampler
 *
 * Vulkan对象生命周期必须显式管理
 */
void Sampler::destroy_() {
    if (sampler_ != VK_NULL_HANDLE) {

        /**
         * GPU资源释放：
         * driver 会延迟回收（可能等待 GPU idle）
         */
        vkDestroySampler(device_, sampler_, nullptr);

        sampler_ = VK_NULL_HANDLE;
    }
}

/**
 * @brief 析构函数（RAII）
 */
Sampler::~Sampler() { destroy_(); }

/**
 * @brief move constructor
 *
 * 资源所有权转移：
 * 避免重复 vkDestroySampler
 */
Sampler::Sampler(Sampler &&other) noexcept
    : device_ { other.device_ }, sampler_ { other.sampler_ } {

    other.device_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

/**
 * @brief move assignment
 */
Sampler &Sampler::operator=(Sampler &&other) noexcept {

    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    sampler_ = other.sampler_;

    other.device_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace render
} // namespace lumen
