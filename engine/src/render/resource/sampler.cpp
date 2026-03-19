/**
 * @file sampler.cpp
 * @brief Sampler 实现
 */

#include "render/resource/sampler.hpp"
#include "render/context.hpp"

namespace lumen {
namespace render {

bool Sampler::create(const Context& ctx, const SamplerConfig& config) {
    device_ = ctx.device();

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx.physical_device(), &props);

    VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;
    samplerInfo.maxAnisotropy =
        std::min(config.maxAnisotropy, props.limits.maxSamplerAnisotropy);
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = config.maxLod;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    VkResult result = vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);
    return result == VK_SUCCESS;
}

void Sampler::destroy_() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
}

Sampler::~Sampler() { destroy_(); }

Sampler::Sampler(Sampler&& other) noexcept
    : device_ { other.device_ }
    , sampler_ { other.sampler_ } {
    other.device_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this == &other) return *this;
    destroy_();
    device_ = other.device_;
    sampler_ = other.sampler_;
    other.device_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    return *this;
}

} // namespace render
} // namespace lumen
