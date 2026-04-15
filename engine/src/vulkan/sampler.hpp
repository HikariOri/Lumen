#pragma once

namespace vulkan {

struct SamplerDesc {
    VkFilter magFilter { VK_FILTER_LINEAR };
    VkFilter minFilter { VK_FILTER_LINEAR };
    VkSamplerMipmapMode mipmapMode { VK_SAMPLER_MIPMAP_MODE_LINEAR };
    VkSamplerAddressMode addressModeU { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeV { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeW { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    float minLod { 0.0F };
    float maxLod { 0.0F };
    float mipLodBias { 0.0F };
    VkBool32 anisotropyEnable { VK_FALSE };
    float maxAnisotropy { 1.0F };
};

[[nodiscard]] VkSampler create_sampler(VkDevice device,
                                       const SamplerDesc &desc = {});

} // namespace vulkan
