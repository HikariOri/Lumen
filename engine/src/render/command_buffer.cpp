/**
 * @file command_buffer.cpp
 * @brief CommandPool 与 FrameSync 实现
 *
 * 实现内容：
 * - CommandPool：命令池创建、分配、释放与重置
 * - FrameSync：Semaphore 与 Fence 的创建与生命周期管理
 *
 * 设计要点：
 * - CommandPool 管理 CommandBuffer 的分配与复用
 * - FrameSync 管理每帧 GPU/CPU 同步
 * - 两者共同构成渲染循环（frame loop）的基础
 */

#include "render/command_buffer.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"

namespace lumen::render {

/**
 * @brief 创建命令池
 *
 * @param ctx Vulkan 上下文
 * @param queueFamilyIndex 队列族索引（必须匹配提交队列）
 *
 * @note
 * - CommandPool 必须绑定到一个 queue family
 * - 该 pool 分配的 command buffer 只能提交到该 queue
 *
 * @note flags:
 * VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
 * - 允许单独 reset command buffer（而不是整个 pool）
 */
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

/**
 * @brief 分配 CommandBuffer
 *
 * @param count 数量
 * @param level PRIMARY / SECONDARY
 * @return 分配成功的 CommandBuffer 列表
 *
 * @note
 * - PRIMARY：可直接提交到 queue
 * - SECONDARY：必须在 primary buffer 中执行
 *
 * @warning
 * pool_ 必须有效，否则行为未定义
 */
std::vector<VkCommandBuffer> CommandPool::allocate(uint32_t count,
                                                   VkCommandBufferLevel level) {
    std::vector<VkCommandBuffer> buffers(count);

    VkCommandBufferAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    allocInfo.commandPool = pool_;
    allocInfo.level = level;
    allocInfo.commandBufferCount = count;

    if (vkAllocateCommandBuffers(device_, &allocInfo, buffers.data()) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("CommandBuffer 分配失败 count={}", count);
        return {};
    }

    return buffers;
}

/**
 * @brief 释放 CommandBuffer
 *
 * @param buffers 要释放的 buffer 列表
 *
 * @note
 * - 必须来自当前 CommandPool
 * - 可以批量释放
 */
void CommandPool::free(const std::vector<VkCommandBuffer> &buffers) {
    if (!buffers.empty()) {
        vkFreeCommandBuffers(device_, pool_,
                             static_cast<uint32_t>(buffers.size()),
                             buffers.data());
    }
}

/**
 * @brief 重置命令池
 *
 * @note
 * - 会使所有 command buffer 回到初始状态
 * - 必须保证 GPU 不再使用这些 buffer
 *
 * @warning
 * 如果 GPU 仍在执行这些 command buffer，
 * reset 会导致未定义行为（严重 bug）
 */
void CommandPool::reset() {
    if (pool_ != VK_NULL_HANDLE) {
        vkResetCommandPool(device_, pool_, 0);
    }
}

/**
 * @brief 销毁命令池
 */
void CommandPool::destroy_() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

CommandPool::~CommandPool() { destroy_(); }

/**
 * @brief 移动构造
 *
 * 转移 Vulkan 资源所有权
 */
CommandPool::CommandPool(CommandPool &&other) noexcept
    : device_ { other.device_ }, pool_ { other.pool_ } {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

/**
 * @brief 移动赋值
 */
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

// ========================== FrameSync ==========================

/**
 * @brief 创建 per-frame 同步对象（简化版本）
 *
 * @param device Vulkan device
 * @param framesInFlight 帧并发数
 *
 * @note
 * 简化为：
 * swapchainImageCount == framesInFlight
 */
bool FrameSync::create(VkDevice device, uint32_t framesInFlight) {
    return create(device, framesInFlight, framesInFlight);
}

/**
 * @brief 创建同步对象（完整版本）
 *
 * @param device Vulkan device
 * @param swapchainImageCount swapchain 图像数量
 * @param framesInFlight 帧并发数
 *
 * @note
 * - Semaphore：按 image 数量分配
 * - Fence：按 frame 数量分配
 *
 * @设计：
 * imageIndex → semaphore
 * frameIndex → fence
 */
bool FrameSync::create(VkDevice device, uint32_t swapchainImageCount,
                       uint32_t framesInFlight) {
    if (!imageAvailable_.empty() || !inFlightFences_.empty()) {
        destroy_();
    }

    device_ = device;

    imageAvailable_.resize(swapchainImageCount);
    renderFinished_.resize(swapchainImageCount);
    inFlightFences_.resize(framesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    // --- 创建 semaphore（per image）---
    for (uint32_t i { 0 }; i < swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                              &imageAvailable_[i]) != VK_SUCCESS) {
            destroy_();
            return false;
        }

        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                              &renderFinished_[i]) != VK_SUCCESS) {
            destroy_();
            return false;
        }
    }

    // --- 创建 fence（per frame）---
    VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    // 初始为 signaled，避免第一帧卡住
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i { 0 }; i < framesInFlight; ++i) {
        if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) !=
            VK_SUCCESS) {
            LUMEN_LOG_ERROR("FrameSync Fence 创建失败");
            destroy_();
            return false;
        }
    }

    LUMEN_LOG_DEBUG("FrameSync 创建成功 swapchainImages={} framesInFlight={}",
                    swapchainImageCount, framesInFlight);

    return true;
}

/**
 * @brief 等待并重置 fence
 *
 * @param frameIndex 当前帧索引
 * @param timeoutNs 超时时间
 * @return true 表示 fence 已 signal
 *
 * @details
 * 执行流程：
 * - CPU 等待 GPU 完成该帧（vkWaitForFences）
 * - 成功后 reset fence（为下一帧复用）
 *
 * @note
 * Fence 用于 GPU → CPU 同步
 *
 * @warning
 * 必须在下一次 vkQueueSubmit 前 reset，否则提交会失败
 */
bool FrameSync::wait_fence(uint32_t frameIndex, uint64_t timeoutNs) {
    if (frameIndex >= inFlightFences_.size()) {
        return false;
    }

    VkResult result = vkWaitForFences(device_, 1, &inFlightFences_[frameIndex],
                                      VK_TRUE, timeoutNs);

    if (result == VK_TIMEOUT) {
        LUMEN_LOG_DEBUG("Fence 等待超时");
        return false;
    }

    if (result == VK_SUCCESS) {
        vkResetFences(device_, 1, &inFlightFences_[frameIndex]);
    }

    return result == VK_SUCCESS;
}

/**
 * @brief 获取 imageAvailable semaphore
 */
VkSemaphore FrameSync::image_available(uint32_t imageIndex) const {
    return imageIndex < imageAvailable_.size() ? imageAvailable_[imageIndex]
                                               : VK_NULL_HANDLE;
}

/**
 * @brief 获取 renderFinished semaphore
 */
VkSemaphore FrameSync::render_finished(uint32_t imageIndex) const {
    return imageIndex < renderFinished_.size() ? renderFinished_[imageIndex]
                                               : VK_NULL_HANDLE;
}

/**
 * @brief 获取当前帧 fence
 */
VkFence FrameSync::in_flight_fence(uint32_t frameIndex) const {
    return frameIndex < inFlightFences_.size() ? inFlightFences_[frameIndex]
                                               : VK_NULL_HANDLE;
}

/**
 * @brief 销毁同步对象
 *
 * @note
 * 必须在 device idle 或保证 GPU 不再使用这些对象后调用
 */
void FrameSync::destroy_() {
    for (auto s : imageAvailable_) {
        vkDestroySemaphore(device_, s, nullptr);
    }

    for (auto s : renderFinished_) {
        vkDestroySemaphore(device_, s, nullptr);
    }

    for (auto f : inFlightFences_) {
        vkDestroyFence(device_, f, nullptr);
    }

    imageAvailable_.clear();
    renderFinished_.clear();
    inFlightFences_.clear();
}

FrameSync::~FrameSync() { destroy_(); }

/**
 * @brief 移动构造
 */
FrameSync::FrameSync(FrameSync &&other) noexcept
    : device_ { other.device_ },
      imageAvailable_ { std::move(other.imageAvailable_) },
      renderFinished_ { std::move(other.renderFinished_) },
      inFlightFences_ { std::move(other.inFlightFences_) } {
    other.device_ = VK_NULL_HANDLE;
}

/**
 * @brief 移动赋值
 */
FrameSync &FrameSync::operator=(FrameSync &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    imageAvailable_ = std::move(other.imageAvailable_);
    renderFinished_ = std::move(other.renderFinished_);
    inFlightFences_ = std::move(other.inFlightFences_);

    other.device_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace lumen::render
