/**
 * @file material.hpp
 * @brief 单套 `VkDescriptorSet` 的分配与 `vkUpdateDescriptorSets` 封装（UBO /
 * 组合采样器）。
 */

#pragma once

#include <expected>
#include <string>

#include <vulkan/vulkan.h>

namespace vulkan {

class Buffer;

class Material final {
public:
    /**
     * @param layout 对应管线 `VkPipelineLayout` 中 `firstSet` 所指的那套 layout（常为
     *        set 0）。
     */
    [[nodiscard]] static std::expected<Material, std::string>
    create(VkDevice device, VkDescriptorPool pool,
           VkDescriptorSetLayout layout);

    Material() = default;
    ~Material() = default;

    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;
    Material(Material &&) = default;
    Material &operator=(Material &&) = default;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkDescriptorSet descriptor_set() const noexcept {
        return set_;
    }
    [[nodiscard]] bool is_valid() const noexcept { return set_ != VK_NULL_HANDLE; }

    void write_uniform_buffer(std::uint32_t binding, const Buffer &buffer,
                              VkDeviceSize offset = 0,
                              VkDeviceSize range = VK_WHOLE_SIZE);

    void write_combined_image_sampler(
        std::uint32_t binding, VkImageView image_view, VkSampler sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

private:
    explicit Material(VkDevice device, VkDescriptorSet set) noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkDescriptorSet set_ { VK_NULL_HANDLE };
};

} // namespace vulkan
