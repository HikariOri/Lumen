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
 * Sampler 在 Vulkan 中属于「Pipeline 外状态」，但会被 descriptor set 引用。
 *
 * ⚠ 性能关键点：
 *  - VkSampler 是「重资源」（driver-level state object）
 *  - 不建议频繁创建（应做 cache / dedup）
 */

#include "render/resource/sampler.hpp"
#include "render/context.hpp"

namespace lumen {
namespace render {

/**
 * @brief 创建 Vulkan Sampler
 *
 * SamplerConfig → vk::SamplerCreateInfo → GPU 固定采样硬件单元
 */
bool Sampler::create(const Context &ctx, const SamplerConfig &config) {

    device_ = ctx.device();

    const auto &props = ctx.physical_device_properties();

    vk::SamplerCreateInfo samplerInfo {};

    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;

    samplerInfo.maxAnisotropy =
        std::min(config.maxAnisotropy, props.limits.maxSamplerAnisotropy);

    samplerInfo.anisotropyEnable = config.maxAnisotropy > 1.0F ? vk::True : vk::False;

    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = config.maxLod;
    samplerInfo.borderColor = config.borderColor;

    const vk::Result result =
        device_.createSampler(&samplerInfo, nullptr, &sampler_);
    return result == vk::Result::eSuccess;
}

void Sampler::destroy_() {
    if (sampler_) {
        device_.destroySampler(sampler_, nullptr);
        sampler_ = nullptr;
    }
}

Sampler::~Sampler() { destroy_(); }

Sampler::Sampler(Sampler &&other) noexcept
    : device_ { other.device_ }, sampler_ { other.sampler_ } {

    other.device_ = nullptr;
    other.sampler_ = nullptr;
}

Sampler &Sampler::operator=(Sampler &&other) noexcept {

    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    sampler_ = other.sampler_;

    other.device_ = nullptr;
    other.sampler_ = nullptr;

    return *this;
}

} // namespace render
} // namespace lumen
