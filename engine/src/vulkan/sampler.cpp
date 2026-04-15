#pragma once

#include "vulkan/sampler.hpp"

namespace vulkan {

VkSampler create_sampler(const VkDevice device, const SamplerDesc &desc) {
    VkSamplerCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = desc.magFilter,
        .minFilter = desc.minFilter,
        .mipmapMode = desc.mipmapMode,
        .addressModeU = desc.addressModeU,
        .addressModeV = desc.addressModeV,
        .addressModeW = desc.addressModeW,
        .mipLodBias = desc.mipLodBias,
        .anisotropyEnable = desc.anisotropyEnable,
        .maxAnisotropy = desc.maxAnisotropy,
        .minLod = desc.minLod,
        .maxLod = desc.maxLod,
    };
    VkSampler sampler { VK_NULL_HANDLE };
    if (vkCreateSampler(device, &createInfo, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}
} // namespace vulkan
