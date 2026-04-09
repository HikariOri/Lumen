/**
 * @file descriptor_set_layout.cpp
 */

#include "vulkan/descriptor_set_layout.hpp"

#include "core/log/logger.hpp"

namespace vulkan {

std::expected<DescriptorSetLayout, std::string>
DescriptorSetLayout::create(
    const VkDevice device,
    const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("DescriptorSetLayout::create: null device"));
    }
    VkDescriptorSetLayoutCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    VkDescriptorSetLayout layout { VK_NULL_HANDLE };
    if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("DescriptorSetLayout::create: vkCreateDescriptorSetLayout failed");
        return std::unexpected(
            std::string("DescriptorSetLayout::create: vkCreateDescriptorSetLayout "
                        "failed"));
    }
    return DescriptorSetLayout(device, layout);
}

DescriptorSetLayout::DescriptorSetLayout(
    const VkDevice device, const VkDescriptorSetLayout layout) noexcept
    : device_(device), layout_(layout) {}

DescriptorSetLayout::~DescriptorSetLayout() { destroy(); }

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout &&other) noexcept
    : device_(other.device_), layout_(other.layout_) {
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
}

DescriptorSetLayout &DescriptorSetLayout::operator=(
    DescriptorSetLayout &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        layout_ = other.layout_;
        other.device_ = VK_NULL_HANDLE;
        other.layout_ = VK_NULL_HANDLE;
    }
    return *this;
}

void DescriptorSetLayout::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

} // namespace vulkan
