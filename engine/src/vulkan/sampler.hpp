#pragma once

namespace vulkan {

struct SamplerDesc {
    VkFilter mag_filter { VK_FILTER_LINEAR };
    VkFilter min_filter { VK_FILTER_LINEAR };
    VkSamplerMipmapMode mipmap_mode { VK_SAMPLER_MIPMAP_MODE_LINEAR };
    VkSamplerAddressMode address_mode_u { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode address_mode_v { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode address_mode_w { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    float min_lod { 0.0F };
    float max_lod { 0.0F };
    float mip_lod_bias { 0.0F };
    VkBool32 anisotropy_enable { VK_FALSE };
    float max_anisotropy { 1.0F };
};

[[nodiscard]] VkSampler create_sampler(VkDevice device,
                                       const SamplerDesc &desc = {});

} // namespace vulkan
