/**
 * @file descriptor_set_layout.hpp
 * @brief `VkDescriptorSetLayout` RAII（与反射生成的 layout 无关时的手工绑定表）。
 */

#pragma once

#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class DescriptorSetLayout final {
public:
    [[nodiscard]] static std::expected<DescriptorSetLayout, std::string>
    create(VkDevice device,
           const std::vector<VkDescriptorSetLayoutBinding> &bindings);

    DescriptorSetLayout() = default;
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout(DescriptorSetLayout &&other) noexcept;
    DescriptorSetLayout &operator=(DescriptorSetLayout &&other) noexcept;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept {
        return layout_;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return layout_ != VK_NULL_HANDLE;
    }

private:
    DescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkDescriptorSetLayout layout_ { VK_NULL_HANDLE };
};

} // namespace vulkan
