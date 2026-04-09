/**
 * @file sync_objects.hpp
 * @brief 信号量 / 围栏 RAII，以及「每帧飞行」与「按交换链图像」呈现同步的常用组合。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class Semaphore final {
public:
    [[nodiscard]] static std::expected<Semaphore, std::string>
    create(VkDevice device);

    Semaphore() = default;
    ~Semaphore();

    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;
    Semaphore(Semaphore &&other) noexcept;
    Semaphore &operator=(Semaphore &&other) noexcept;

    [[nodiscard]] VkSemaphore get() const noexcept { return semaphore_; }
    [[nodiscard]] explicit operator VkSemaphore() const noexcept {
        return semaphore_;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return semaphore_ != VK_NULL_HANDLE;
    }

private:
    explicit Semaphore(VkDevice device, VkSemaphore sem) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkSemaphore semaphore_ { VK_NULL_HANDLE };
};

class GpuFence final {
public:
    [[nodiscard]] static std::expected<GpuFence, std::string>
    create(VkDevice device, VkFenceCreateFlags flags = 0);

    GpuFence() = default;
    ~GpuFence();

    GpuFence(const GpuFence &) = delete;
    GpuFence &operator=(const GpuFence &) = delete;
    GpuFence(GpuFence &&other) noexcept;
    GpuFence &operator=(GpuFence &&other) noexcept;

    [[nodiscard]] VkFence get() const noexcept { return fence_; }
    [[nodiscard]] explicit operator VkFence() const noexcept { return fence_; }
    [[nodiscard]] bool is_valid() const noexcept {
        return fence_ != VK_NULL_HANDLE;
    }

private:
    explicit GpuFence(VkDevice device, VkFence fence) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkFence fence_ { VK_NULL_HANDLE };
};

/**
 * @brief 单缓冲演示用：acquire / present / 提交围栏各一（非多帧交错）。
 */
struct PresentFrameSync final {
    Semaphore image_available;
    Semaphore render_finished;
    GpuFence inflight;

    [[nodiscard]] static std::expected<PresentFrameSync, std::string>
    create(VkDevice device);
};

/**
 * @brief 一帧飞行槽位：`vkAcquireNextImageKHR` 信号量 + 该帧 `vkQueueSubmit` 围栏。
 */
struct FrameInFlightSlot final {
    Semaphore image_available;
    GpuFence inflight_fence;
};

[[nodiscard]] std::expected<std::vector<FrameInFlightSlot>, std::string>
create_frame_in_flight_slots(VkDevice device, std::uint32_t frame_count);

/**
 * @brief 每条交换链图像一条 `render_finished`，避免 present 未完成时复用同一信号量。
 */
class PerImageSemaphores final {
public:
    PerImageSemaphores() = default;
    ~PerImageSemaphores();

    PerImageSemaphores(const PerImageSemaphores &) = delete;
    PerImageSemaphores &operator=(const PerImageSemaphores &) = delete;
    PerImageSemaphores(PerImageSemaphores &&other) noexcept;
    PerImageSemaphores &operator=(PerImageSemaphores &&other) noexcept;

    /**
     * @brief 将长度调整为 @p count；缩容或扩容前在设备空闲时调用（与
     *        `vkDeviceWaitIdle` 或应用侧同步策略一致）。
     */
    [[nodiscard]] std::expected<void, std::string>
    sync_count(VkDevice device, std::size_t count);

    [[nodiscard]] VkSemaphore get(std::size_t image_index) const;
    [[nodiscard]] std::size_t size() const noexcept { return semaphores_.size(); }

private:
    void destroy_all() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    std::vector<VkSemaphore> semaphores_;
};

} // namespace vulkan
