/**
 * @file descriptor_pool.cpp
 */

#include "vulkan/descriptor_pool.hpp"

#include "core/log/logger.hpp"

namespace vulkan {

std::expected<DescriptorPool, std::string>
DescriptorPool::create(const VkDevice device, const std::uint32_t max_sets,
                       const std::vector<VkDescriptorPoolSize> &pool_sizes,
                       const VkDescriptorPoolCreateFlags flags) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("DescriptorPool::create: null device"));
    }
    if (max_sets == 0U) {
        return std::unexpected(
            std::string("DescriptorPool::create: max_sets must be > 0"));
    }
    VkDescriptorPoolCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = flags,
        .maxSets = max_sets,
        .poolSizeCount = static_cast<std::uint32_t>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    VkDescriptorPool pool { VK_NULL_HANDLE };
    if (vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("DescriptorPool::create: vkCreateDescriptorPool failed");
        return std::unexpected(
            std::string("DescriptorPool::create: vkCreateDescriptorPool failed"));
    }
    return DescriptorPool(device, pool);
}

DescriptorPool DescriptorPool::adopt(const VkDevice device,
                                       const VkDescriptorPool pool) noexcept {
    return DescriptorPool(device, pool);
}

DescriptorPool::DescriptorPool(const VkDevice device,
                               const VkDescriptorPool pool) noexcept
    : device_(device), pool_(pool) {}

DescriptorPool::~DescriptorPool() { destroy(); }

DescriptorPool::DescriptorPool(DescriptorPool &&other) noexcept
    : device_(other.device_), pool_(other.pool_) {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

DescriptorPool &DescriptorPool::operator=(DescriptorPool &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        pool_ = other.pool_;
        other.device_ = VK_NULL_HANDLE;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

void DescriptorPool::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
}

} // namespace vulkan
