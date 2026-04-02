/**
 * @file frame_sync.cpp
 * @brief FrameSync 实现
 */

#include "render/frame_sync.hpp"

#include "core/logger.hpp"

namespace lumen::render {

bool FrameSync::create(vk::Device device, uint32_t framesInFlight) {
    return create(device, framesInFlight, framesInFlight);
}

bool FrameSync::create(vk::Device device, uint32_t swapchainImageCount,
                       uint32_t framesInFlight) {
    if (!imageAvailable_.empty() || !inFlightFences_.empty()) {
        destroy_();
    }

    device_ = device;

    imageAvailable_.resize(swapchainImageCount);
    renderFinished_.resize(swapchainImageCount);
    inFlightFences_.resize(framesInFlight);

    vk::SemaphoreCreateInfo semaphoreInfo {};

    for (uint32_t i { 0 }; i < swapchainImageCount; ++i) {
        if (device_.createSemaphore(&semaphoreInfo, nullptr,
                                    &imageAvailable_[i]) !=
            vk::Result::eSuccess) {
            destroy_();
            return false;
        }

        if (device_.createSemaphore(&semaphoreInfo, nullptr,
                                    &renderFinished_[i]) !=
            vk::Result::eSuccess) {
            destroy_();
            return false;
        }
    }

    vk::FenceCreateInfo fenceInfo {};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (uint32_t i { 0 }; i < framesInFlight; ++i) {
        if (device_.createFence(&fenceInfo, nullptr, &inFlightFences_[i]) !=
            vk::Result::eSuccess) {
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

    const vk::Fence fence = inFlightFences_[frameIndex];
    const vk::Result result =
        device_.waitForFences(1, &fence, vk::True, timeoutNs);

    return result == vk::Result::eSuccess;
}

bool FrameSync::reset_fence(uint32_t frameIndex) {
    if (frameIndex >= inFlightFences_.size()) {
        return false;
    }
    const vk::Fence fence = inFlightFences_[frameIndex];
    return device_.resetFences(1, &fence) == vk::Result::eSuccess;
}

bool FrameSync::recreate_in_flight_fence_signaled(uint32_t frameIndex) {
    if (frameIndex >= inFlightFences_.size() || !device_) {
        return false;
    }
    device_.destroyFence(inFlightFences_[frameIndex], nullptr);
    vk::FenceCreateInfo fenceInfo {};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
    return device_.createFence(&fenceInfo, nullptr,
                               &inFlightFences_[frameIndex]) ==
           vk::Result::eSuccess;
}

vk::Semaphore FrameSync::image_available(uint32_t imageIndex) const {
    return imageIndex < imageAvailable_.size() ? imageAvailable_[imageIndex]
                                               : vk::Semaphore {};
}

vk::Semaphore FrameSync::render_finished(uint32_t imageIndex) const {
    return imageIndex < renderFinished_.size() ? renderFinished_[imageIndex]
                                               : vk::Semaphore {};
}

vk::Fence FrameSync::in_flight_fence(uint32_t frameIndex) const {
    return frameIndex < inFlightFences_.size() ? inFlightFences_[frameIndex]
                                               : vk::Fence {};
}

void FrameSync::destroy_() {
    if (!device_) {
        imageAvailable_.clear();
        renderFinished_.clear();
        inFlightFences_.clear();
        return;
    }
    for (auto s : imageAvailable_) {
        if (s) {
            device_.destroySemaphore(s, nullptr);
        }
    }

    for (auto s : renderFinished_) {
        if (s) {
            device_.destroySemaphore(s, nullptr);
        }
    }

    for (auto f : inFlightFences_) {
        if (f) {
            device_.destroyFence(f, nullptr);
        }
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
    other.device_ = nullptr;
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

    other.device_ = nullptr;

    return *this;
}

} // namespace lumen::render
