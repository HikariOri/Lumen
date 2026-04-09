/**
 * @file command_pool.cpp
 */

#include "vulkan/command_pool.hpp"

#include "core/log/logger.hpp"

namespace vulkan {

std::expected<CommandPool, std::string>
CommandPool::create(const VkDevice device,
                    const std::uint32_t queue_family_index,
                    const VkCommandPoolCreateFlags flags) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("CommandPool::create: null device"));
    }
    VkCommandPoolCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = flags,
        .queueFamilyIndex = queue_family_index,
    };
    VkCommandPool pool { VK_NULL_HANDLE };
    if (vkCreateCommandPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("CommandPool::create: vkCreateCommandPool failed");
        return std::unexpected(
            std::string("CommandPool::create: vkCreateCommandPool failed"));
    }
    return CommandPool(device, pool);
}

CommandPool::CommandPool(const VkDevice device,
                         const VkCommandPool pool) noexcept
    : device_(device), pool_(pool) {}

CommandPool::~CommandPool() { destroy(); }

CommandPool::CommandPool(CommandPool &&other) noexcept
    : device_(other.device_), pool_(other.pool_) {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        pool_ = other.pool_;
        other.device_ = VK_NULL_HANDLE;
        other.pool_ = VK_NULL_HANDLE;
    }
    return *this;
}

void CommandPool::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, pool_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
}

std::expected<std::vector<VkCommandBuffer>, std::string>
CommandPool::allocate_primary(const std::uint32_t count) const {
    if (device_ == VK_NULL_HANDLE || pool_ == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("CommandPool::allocate_primary: invalid pool"));
    }
    if (count == 0U) {
        return std::vector<VkCommandBuffer> {};
    }
    VkCommandBufferAllocateInfo alloc {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = count,
    };
    std::vector<VkCommandBuffer> buffers(count);
    if (vkAllocateCommandBuffers(device_, &alloc, buffers.data()) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("CommandPool::allocate_primary: vkAllocateCommandBuffers failed");
        return std::unexpected(std::string(
            "CommandPool::allocate_primary: vkAllocateCommandBuffers failed"));
    }
    return buffers;
}

} // namespace vulkan
