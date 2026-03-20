/**
 * @file sampler.hpp
 * @brief VkSampler 封装：过滤、寻址模式、各向异性
 *
 * 用于纹理采样的 Sampler 配置。
 */

#pragma once

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/// Sampler 配置
struct SamplerConfig {
    VkFilter magFilter { VK_FILTER_LINEAR };
    VkFilter minFilter { VK_FILTER_LINEAR };
    VkSamplerMipmapMode mipmapMode { VK_SAMPLER_MIPMAP_MODE_LINEAR };
    VkSamplerAddressMode addressModeU { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeV { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    VkSamplerAddressMode addressModeW { VK_SAMPLER_ADDRESS_MODE_REPEAT };
    /// 各向异性过滤，0 表示关闭
    float maxAnisotropy { 16.0f };
    float minLod { 0.0f };
    float maxLod { VK_LOD_CLAMP_NONE };
};

/**
 * @class Sampler
 * @brief Vulkan Sampler 封装
 */
class Sampler {
public:
    Sampler() = default;
    Sampler(const Sampler &) = delete;
    Sampler(Sampler &&other) noexcept;
    Sampler &operator=(const Sampler &) = delete;
    Sampler &operator=(Sampler &&other) noexcept;
    ~Sampler();

    /**
     * @brief 创建 Sampler
     * @param ctx 已初始化的 Context
     * @param config 可选配置
     * @return 成功返回 true
     */
    bool create(const Context &ctx, const SamplerConfig &config = {});

    /// VkSampler 句柄
    [[nodiscard]] VkSampler handle() const { return sampler_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return sampler_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkSampler sampler_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
