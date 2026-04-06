/**
 * @file command_buffer.cpp
 * @brief CommandPool 实现
 */

#include "render/command_buffer.hpp"

#include "core/log/logger.hpp"

#include "render/context.hpp"

#include <array>

namespace lumen::render {

bool CommandPool::create(const Context &ctx, uint32_t queueFamilyIndex) {
    device_ = ctx.device();

    vk::CommandPoolCreateInfo createInfo {};
    createInfo.queueFamilyIndex = queueFamilyIndex;
    createInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    const vk::Result result =
        device_.createCommandPool(&createInfo, nullptr, &pool_);

    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("CommandPool 创建成功, queueFamily={}",
                        queueFamilyIndex);
    } else {
        LUMEN_LOG_ERROR("CommandPool 创建失败, result={}",
                        static_cast<int>(result));
    }

    return result == vk::Result::eSuccess;
}

std::vector<vk::CommandBuffer>
CommandPool::allocate(uint32_t count, vk::CommandBufferLevel level) {
    std::vector<vk::CommandBuffer> raw(count);

    vk::CommandBufferAllocateInfo allocInfo {};
    allocInfo.commandPool = pool_;
    allocInfo.level = level;
    allocInfo.commandBufferCount = count;

    if (device_.allocateCommandBuffers(&allocInfo, raw.data()) !=
        vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("命令缓冲分配失败 count={}", count);
        return {};
    }

    return raw;
}

void CommandPool::free(const std::vector<vk::CommandBuffer> &buffers) {
    if (buffers.empty()) {
        return;
    }
    device_.freeCommandBuffers(pool_, buffers);
}

void CommandPool::free_one_(vk::CommandBuffer cb) {
    if (!cb) {
        return;
    }
    const std::array<vk::CommandBuffer, 1> one {{ cb }};
    device_.freeCommandBuffers(pool_, one);
}

void CommandPool::reset() {
    if (pool_) {
        device_.resetCommandPool(pool_, {});
    }
}

bool CommandPool::submit_one_shot(
    vk::Queue queue, const std::function<void(vk::CommandBuffer cmd)> &record) {
    if (!pool_ || !device_ || !queue) {
        LUMEN_LOG_ERROR("submit_one_shot: 无效的 pool / device / queue");
        return false;
    }

    std::vector<vk::CommandBuffer> buffers =
        allocate(1, vk::CommandBufferLevel::ePrimary);
    if (buffers.empty() || !buffers[0]) {
        return false;
    }

    vk::CommandBuffer cmd = buffers[0];

    vk::CommandBufferBeginInfo beginInfo {};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    if (cmd.begin(&beginInfo) != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("submit_one_shot: vk::CommandBuffer::begin 失败");
        free_one_(cmd);
        return false;
    }

    record(cmd);
    cmd.end();

    vk::Fence fence {};
    vk::FenceCreateInfo fenceInfo {};
    if (device_.createFence(&fenceInfo, nullptr, &fence) !=
        vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("submit_one_shot: createFence 失败");
        free_one_(cmd);
        return false;
    }

    vk::SubmitInfo submitInfo {};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    const vk::Result submitResult = queue.submit(1, &submitInfo, fence);
    if (submitResult != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("submit_one_shot: queue.submit 失败 result={}",
                        static_cast<int>(submitResult));
        device_.destroyFence(fence, nullptr);
        free_one_(cmd);
        return false;
    }

    const vk::Result waitResult =
        device_.waitForFences(1, &fence, vk::True, UINT64_MAX);
    device_.destroyFence(fence, nullptr);
    free_one_(cmd);

    if (waitResult != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("submit_one_shot: waitForFences 失败 result={}",
                        static_cast<int>(waitResult));
        return false;
    }

    return true;
}

void CommandPool::destroy_() {
    if (pool_) {
        device_.destroyCommandPool(pool_, nullptr);
        pool_ = nullptr;
    }
}

CommandPool::~CommandPool() { destroy_(); }

CommandPool::CommandPool(CommandPool &&other) noexcept
    : device_ { other.device_ }, pool_ { other.pool_ } {
    other.device_ = nullptr;
    other.pool_ = nullptr;
}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    pool_ = other.pool_;

    other.device_ = nullptr;
    other.pool_ = nullptr;

    return *this;
}

} // namespace lumen::render
