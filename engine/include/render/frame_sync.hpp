/**
 * @file frame_sync.hpp
 * @brief per-frame 同步对象：Semaphore + Fence（swapchain / 帧轮转）
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

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

    /**
     * @brief 创建 per‑frame 的同步对象
     * @param device Vulkan device
     * @param framesInFlight 并发帧数（如 2 或 3）
     * @return true 表示成功
     *
     * 本版本为每帧创建一个 fence 和两个 semaphore
     * - imageAvailable：等待下一帧图像可渲染
     * - renderFinished：完成渲染后 signal 给 present
     */
    bool create(VkDevice device, uint32_t framesInFlight);

    /**
     * @brief 创建 per‑image 的 semaphore + per‑frame fence
     * @param device Vulkan device
     * @param swapchainImageCount swapchain 图像个数
     * @param framesInFlight 并发帧数
     *
     * Swapchain 特殊调用，用 image 数量产生对应的
     * imageAvailable Semaphore（避免复用冲突）。
     */
    bool create(VkDevice device, uint32_t swapchainImageCount,
                uint32_t framesInFlight);

    /**
     * @brief 等待 fence 变为已信号（上一轮 GPU 已结束占用本 slot）
     * @param timeoutNs 超时，UINT64_MAX 表示无限等待
     * @return true 表示已等到 signal
     *
     * @note 不在此处 vkResetFences。若在随后路径中未执行
     * vkQueueSubmit（例如最小化后 swapchain
     * 不可呈现），围栏应保持已信号，否则下一帧同 frameIndex 会永久等超时。
     * 仅在即将提交队列前调用 reset_fence()。
     */
    bool wait_fence(uint32_t frameIndex, uint64_t timeoutNs = UINT64_MAX);

    /**
     * @brief 将 fence 复位为未信号，供下一次 vkQueueSubmit 使用
     * @details 须在 wait_fence 成功之后、且本条路径会调用 vkQueueSubmit
     * 时再调用。
     */
    bool reset_fence(uint32_t frameIndex);

    /**
     * @brief 在 vkQueueSubmit 失败等异常后恢复 fence（须先 ctx.wait_idle）
     *
     * reset_fence 已成功但 submit 未执行时，fence 未信号，下一帧 wait 会卡死。
     * 本函数销毁并以已信号样式重建该槽位 fence。
     */
    bool recreate_in_flight_fence_signaled(uint32_t frameIndex);

    /**
     * @brief 获取按 imageIndex 的 imageAvailable Semaphore
     */
    [[nodiscard]] VkSemaphore image_available(uint32_t imageIndex) const;

    /**
     * @brief 获取按 imageIndex 的 renderFinished Semaphore
     */
    [[nodiscard]] VkSemaphore render_finished(uint32_t imageIndex) const;

    /**
     * @brief 获取当前帧的 fence
     */
    [[nodiscard]] VkFence in_flight_fence(uint32_t frameIndex) const;

    [[nodiscard]] uint32_t frame_count() const {
        return static_cast<uint32_t>(inFlightFences_.size());
    }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };

    /// Semaphore for image acquire (swapchain)
    std::vector<VkSemaphore> imageAvailable_;

    /// Semaphore for signaling render finish
    std::vector<VkSemaphore> renderFinished_;

    /// Per‑frame fence for CPU sync
    std::vector<VkFence> inFlightFences_;
};

} // namespace render
} // namespace lumen
