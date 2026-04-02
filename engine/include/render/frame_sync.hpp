/**
 * @file frame_sync.hpp
 * @brief per-frame 同步对象：Semaphore + Fence（swapchain / 帧轮转）
 */

#pragma once

#include <cstdint>
#include <vector>

#include "render/vulkan.hpp"

namespace lumen {
namespace render {

/**
 * @class FrameSync
 * @brief per‑frame 同步对象：Semaphore + Fence
 *
 * 同步的核心目的是：
 * - Semaphore：用于 GPU 到 GPU（比如等待 imageAvailable
 *   再执行 draw，再 signal renderFinished 给 presentation）。
 * - Fence：用于 GPU 到 CPU（CPU 等待 GPU 完成一帧执行）。
 *
 * 多帧并发：
 * Vulkan 应用通常在一帧 GPU 执行时就开始 CPU 录制下一帧，
 * 所以需要有多个 fence & semaphore 组合来轮转。
 */
class FrameSync {
public:
    FrameSync() = default;
    FrameSync(const FrameSync &) = delete;
    FrameSync(FrameSync &&other) noexcept;
    FrameSync &operator=(const FrameSync &) = delete;
    FrameSync &operator=(FrameSync &&other) noexcept;
    ~FrameSync();

    bool create(vk::Device device, uint32_t framesInFlight);

    bool create(vk::Device device, uint32_t swapchainImageCount,
                uint32_t framesInFlight);

    bool wait_fence(uint32_t frameIndex, uint64_t timeoutNs = UINT64_MAX);

    bool reset_fence(uint32_t frameIndex);

    bool recreate_in_flight_fence_signaled(uint32_t frameIndex);

    [[nodiscard]] vk::Semaphore image_available(uint32_t imageIndex) const;

    [[nodiscard]] vk::Semaphore render_finished(uint32_t imageIndex) const;

    [[nodiscard]] vk::Fence in_flight_fence(uint32_t frameIndex) const;

    [[nodiscard]] uint32_t frame_count() const {
        return static_cast<uint32_t>(inFlightFences_.size());
    }

private:
    void destroy_();

    vk::Device device_ {};

    std::vector<vk::Semaphore> imageAvailable_;
    std::vector<vk::Semaphore> renderFinished_;
    std::vector<vk::Fence> inFlightFences_;
};

} // namespace render
} // namespace lumen
