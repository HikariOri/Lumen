/**
 * @file frame_sync.cpp
 * @brief FrameSync 实现
 */

#include "render/frame_sync.hpp"

#include "core/logger.hpp"

namespace lumen::render {

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

VkSemaphore FrameSync::image_available(uint32_t imageIndex) const {
    return imageIndex < imageAvailable_.size() ? imageAvailable_[imageIndex]
                                               : VK_NULL_HANDLE;
}

VkSemaphore FrameSync::render_finished(uint32_t imageIndex) const {
    return imageIndex < renderFinished_.size() ? renderFinished_[imageIndex]
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
