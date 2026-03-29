/**
 * @file command_buffer.cpp
 * @brief CommandPool 实现
 */

#include "render/command_buffer.hpp"

#include "core/logger.hpp"

#include <utility>
#include "render/context.hpp"

namespace lumen::render {

CommandBuffer::CommandBuffer(CommandBuffer &&other) noexcept
    : vk_(other.vk_) {
    other.vk_ = VK_NULL_HANDLE;
}

CommandBuffer &CommandBuffer::operator=(CommandBuffer &&other) noexcept {
    if (this != &other) {
        vk_ = other.vk_;
        other.vk_ = VK_NULL_HANDLE;
    }
    return *this;
}

VkCommandBufferLevel command_buffer_level_to_vk(CommandBufferLevel level) {
    return level == CommandBufferLevel::Primary
               ? VK_COMMAND_BUFFER_LEVEL_PRIMARY
               : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
}

bool CommandPool::create(const Context &ctx, uint32_t queueFamilyIndex) {
    device_ = ctx.device();

    VkCommandPoolCreateInfo createInfo {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    createInfo.queueFamilyIndex = queueFamilyIndex;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result =
        vkCreateCommandPool(device_, &createInfo, nullptr, &pool_);

    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("CommandPool 创建成功, queueFamily={}",
                        queueFamilyIndex);
    } else {
        LUMEN_LOG_ERROR("CommandPool 创建失败, result={}",
                        static_cast<int>(result));
    }

    return result == VK_SUCCESS;
}

std::vector<CommandBuffer> CommandPool::allocate(uint32_t count,
                                                 CommandBufferLevel level) {
    std::vector<VkCommandBuffer> raw(count);

    VkCommandBufferAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    allocInfo.commandPool = pool_;
    allocInfo.level = command_buffer_level_to_vk(level);
    allocInfo.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device_, &allocInfo, raw.data()) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("CommandBuffer 分配失败 count={}", count);
        return {};
    }

    std::vector<CommandBuffer> out;
    out.reserve(count);
    for (VkCommandBuffer vk : raw) {
        out.emplace_back(CommandBuffer(vk));
    }
    return out;
}

void CommandPool::free(const std::vector<CommandBuffer> &buffers) {
    if (buffers.empty()) {
        return;
    }
    std::vector<VkCommandBuffer> raw;
    raw.reserve(buffers.size());
    for (const CommandBuffer &b : buffers) {
        raw.push_back(b.handle());
    }
    vkFreeCommandBuffers(device_, pool_, static_cast<uint32_t>(raw.size()),
                         raw.data());
}

void CommandPool::free_moved(CommandBuffer &&cb) {
    if (!cb.is_valid()) {
        return;
    }
    std::vector<CommandBuffer> v;
    v.push_back(std::move(cb));
    free(v);
}

void CommandPool::reset() {
    if (pool_ != VK_NULL_HANDLE) {
        vkResetCommandPool(device_, pool_, 0);
    }
}

bool CommandPool::submit_one_shot(
    VkQueue queue,
    const std::function<void(const CommandBuffer &cmd)> &record) {
    if (pool_ == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE ||
        queue == VK_NULL_HANDLE) {
        LUMEN_LOG_ERROR("submit_one_shot: 无效的 pool / device / queue");
        return false;
    }

    std::vector<CommandBuffer> buffers = allocate(1, CommandBufferLevel::Primary);
    if (buffers.empty() || !buffers[0].is_valid()) {
        return false;
    }

    CommandBuffer cmd = std::move(buffers[0]);

    // ONE_TIME_SUBMIT：本 buffer 仅提交一次后即 free，驱动可据此优化；与帧内复用 buffer 的用法区分
    const VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkCommandBuffer vkCmd = cmd.handle();
    if (vkBeginCommandBuffer(vkCmd, &beginInfo) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("submit_one_shot: vkBeginCommandBuffer 失败");
        free_moved(std::move(cmd));
        return false;
    }

    record(cmd);

    if (vkEndCommandBuffer(vkCmd) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("submit_one_shot: vkEndCommandBuffer 失败");
        free_moved(std::move(cmd));
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device_, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("submit_one_shot: vkCreateFence 失败");
        free_moved(std::move(cmd));
        return false;
    }

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkCmd;

    const VkResult submitResult =
        vkQueueSubmit(queue, 1, &submitInfo, fence);
    if (submitResult != VK_SUCCESS) {
        LUMEN_LOG_ERROR("submit_one_shot: vkQueueSubmit 失败 result={}",
                        static_cast<int>(submitResult));
        vkDestroyFence(device_, fence, nullptr);
        free_moved(std::move(cmd));
        return false;
    }

    const VkResult waitResult =
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    free_moved(std::move(cmd));

    if (waitResult != VK_SUCCESS) {
        LUMEN_LOG_ERROR("submit_one_shot: vkWaitForFences 失败 result={}",
                        static_cast<int>(waitResult));
        return false;
    }

    return true;
}

void CommandPool::destroy_() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

CommandPool::~CommandPool() { destroy_(); }

CommandPool::CommandPool(CommandPool &&other) noexcept
    : device_ { other.device_ }, pool_ { other.pool_ } {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    pool_ = other.pool_;

    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace lumen::render
