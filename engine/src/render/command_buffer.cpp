/**
 * @file command_buffer.cpp
 * @brief CommandPool 与 FrameSync 实现
 */

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "core/logger.hpp"

namespace lumen::render {

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
        }
        return result == VK_SUCCESS;
    }

    std::vector<VkCommandBuffer>
    CommandPool::allocate(uint32_t count, VkCommandBufferLevel level) {
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

    void CommandPool::free(const std::vector<VkCommandBuffer> &buffers) {
        if (!buffers.empty()) {
            vkFreeCommandBuffers(device_, pool_,
                                 static_cast<uint32_t>(buffers.size()),
                                 buffers.data());
        }
    }

    void CommandPool::reset() {
        if (pool_ != VK_NULL_HANDLE) {
            vkResetCommandPool(device_, pool_, 0);
        }
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
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        pool_ = other.pool_;
        other.device_ = VK_NULL_HANDLE;
        other.pool_ = VK_NULL_HANDLE;
        return *this;
    }

    // --- FrameSync ---

    bool FrameSync::create(VkDevice device, uint32_t framesInFlight) {
        return create(device, framesInFlight, framesInFlight);
    }

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

        VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i { 0 }; i < framesInFlight; ++i) {
            if (vkCreateFence(device_, &fenceInfo, nullptr,
                              &inFlightFences_[i]) != VK_SUCCESS) {
                LUMEN_LOG_ERROR("FrameSync Fence 创建失败");
                destroy_();
                return false;
            }
        }
        LUMEN_LOG_DEBUG("FrameSync 创建成功 swapchainImages={} framesInFlight={}",
                        swapchainImageCount, framesInFlight);
        return true;
    }

    bool FrameSync::wait_fence(uint32_t frameIndex, uint64_t timeoutNs) {
        if (frameIndex >= inFlightFences_.size()) {
            return false;
        }
        VkResult result = vkWaitForFences(
            device_, 1, &inFlightFences_[frameIndex], VK_TRUE, timeoutNs);
        if (result == VK_TIMEOUT) {
            return false;
        }
        if (result == VK_SUCCESS) {
            vkResetFences(device_, 1, &inFlightFences_[frameIndex]);
        }
        return result == VK_SUCCESS;
    }

    VkSemaphore FrameSync::image_available(uint32_t imageIndex) const {
        return imageIndex < imageAvailable_.size()
                   ? imageAvailable_[imageIndex]
                   : VK_NULL_HANDLE;
    }

    VkSemaphore FrameSync::render_finished(uint32_t imageIndex) const {
        return imageIndex < renderFinished_.size()
                   ? renderFinished_[imageIndex]
                   : VK_NULL_HANDLE;
    }

    VkFence FrameSync::in_flight_fence(uint32_t frameIndex) const {
        return frameIndex < inFlightFences_.size() ? inFlightFences_[frameIndex]
                                                   : VK_NULL_HANDLE;
    }

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

    FrameSync::FrameSync(FrameSync &&other) noexcept
        : device_ { other.device_ },
          imageAvailable_ { std::move(other.imageAvailable_) },
          renderFinished_ { std::move(other.renderFinished_) },
          inFlightFences_ { std::move(other.inFlightFences_) } {
        other.device_ = VK_NULL_HANDLE;
    }

    FrameSync &FrameSync::operator=(FrameSync &&other) noexcept {
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        imageAvailable_ = std::move(other.imageAvailable_);
        renderFinished_ = std::move(other.renderFinished_);
        inFlightFences_ = std::move(other.inFlightFences_);
        other.device_ = VK_NULL_HANDLE;
        return *this;
    }

} // namespace lumen::render
