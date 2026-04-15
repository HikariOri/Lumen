#pragma once

#include "vulkan/sampler.hpp"

namespace vulkan {

VkSampler create_sampler(const VkDevice device, const SamplerDesc &desc) {
    VkSamplerCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = desc.mag_filter,
        .minFilter = desc.min_filter,
        .mipmapMode = desc.mipmap_mode,
        .addressModeU = desc.address_mode_u,
        .addressModeV = desc.address_mode_v,
        .addressModeW = desc.address_mode_w,
        .mipLodBias = desc.mip_lod_bias,
        .anisotropyEnable = desc.anisotropy_enable,
        .maxAnisotropy = desc.max_anisotropy,
        .minLod = desc.min_lod,
        .maxLod = desc.max_lod,
    };
    VkSampler sampler { VK_NULL_HANDLE };
    if (vkCreateSampler(device, &createInfo, nullptr, &sampler) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return sampler;
}
} // namespace vulkan
