/**
 * @file command_buffer.hpp
 * @brief CommandPool 与 CommandBuffer、帧同步对象
 *
 * Vulkan 中所有 GPU 操作都需通过命令缓冲(CommandBuffer)提交，
 * 这些缓冲记录渲染、计算、内存拷贝等操作。在执行这些命令之前，
 * 需要通过命令池(CommandPool)分配，同时结合同步原语（Semaphore、Fence）
 * 来控制 GPU 与 CPU、帧间的执行顺序。
 *
 * 术语背景：
 * - CommandPool：Vulkan 管理命令缓冲的内存与分配器，
 *   必须先创建才能 allocate command buffers。
 * - CommandBuffer：用来 *记录 GPU 要执行的命令*。
 * - Semaphore：用于 GPU → GPU 的同步（queue 之间或同一个 queue 中
 *   不同命令提交顺序依赖）。
 * - Fence：用于 GPU → CPU 的同步（CPU 等待 GPU 完成某一帧）。
 *
 * 多帧并发（frames in flight）：
 * Vulkan 应用通常允许 *多帧同时在 GPU 上执行*，避免 CPU 被 GPU 阻塞。
 * 实现方式：
 * 每个 frame 拥有独立的 command buffer、semaphore 和 fence。
 * CPU 在下一帧开始之前，等待对应的 fence 结束。
 *
 * @todo [架构] 封装 CommandBuffer，将代码中的 vkCommandBuffer 适当替换为
 * CommandBuffer
 * @todo [架构] 拆分 CommandBuffer 和 FrameSync 到不同的文件
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/**
 * @class CommandPool
 * @brief Vulkan 命令池
 *
 * CommandPool 用于分配 CommandBuffer。在 Vulkan 里命令缓冲来自池，
 * 池的生命周期应与命令缓冲一致。命令池通常按 **队列族索引** 创建，
 * 因为不同队列族（graphics/compute/transfer）无法复用同一个池。
 *
 * 使用流程：
 * 1. 创建 VkCommandPool
 * 2. 从 pool allocate N 个 VkCommandBuffer
 * 3. 使用 vkBeginCommandBuffer / vkEndCommandBuffer 记录命令
 * 4. vkQueueSubmit 提交执行
 *
 * 注意：
 * - vkResetCommandPool 可以重置所有分配的 buffer
 * - CommandPool 必须在释放所有 buffer 之后销毁
 * - CommandPool 对线程访问不是线程安全的
 */
class CommandPool {
public:
    CommandPool() = default;
    CommandPool(const CommandPool &) = delete;
    CommandPool(CommandPool &&other) noexcept;
    CommandPool &operator=(const CommandPool &) = delete;
    CommandPool &operator=(CommandPool &&other) noexcept;
    ~CommandPool();

    /**
     * @brief 创建命令池
     * @param ctx 已初始化 Vulkan Context
     * @param queueFamilyIndex 要绑定的队列族索引（如 gfx 队列）
     * @return true 表示成功
     *
     * 队列族决定了从该 pool 分配的 command buffers 能提交到哪个 queue。
     */
    bool create(const Context &ctx, uint32_t queueFamilyIndex);

    /**
     * @brief 分配 CommandBuffer
     * @param count 数量
     * @param level 主/次级 CommandBuffer
     * @return 成功返回 VkCommandBuffer 列表
     *
     * VK_COMMAND_BUFFER_LEVEL_PRIMARY 可直接 vkQueueSubmit；
     * VK_COMMAND_BUFFER_LEVEL_SECONDARY 只能被 primary 调用。
     *
     * @todo level 应该封装为 CommandBufferLevel
     */
    std::vector<VkCommandBuffer>
    allocate(uint32_t count,
             VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /**
     * @brief 释放 CommandBuffer
     *
     * 释放由此命令池分配的 command buffer。注意可以批量释放。
     */
    void free(const std::vector<VkCommandBuffer> &buffers);

    /**
     * @brief 重置命令池中所有分配（等价于 vkResetCommandPool）
     *
     * 在开始下一帧录制 command buffers 之前，常 reset 以重用。
     */
    void reset();

    [[nodiscard]] VkCommandPool handle() const { return pool_; }
    [[nodiscard]] bool is_valid() const { return pool_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkCommandPool pool_ { VK_NULL_HANDLE };
};

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
 * 所以需要有多个 fence & semaphore
 * 组合来轮转。:contentReference[oaicite:2]{index=2}
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
     * @brief 等待 fence 并重置
     * @param frameIndex 当前帧索引
     * @param timeoutNs 超时，UINT64_MAX 表示无限等待
     * @return true 表示 fence 已 signal 并重置成功
     *
     * CPU 端等待 GPU 完成该帧，避免覆盖 GPU 正在使用的资源。
     */
    bool wait_fence(uint32_t frameIndex, uint64_t timeoutNs = UINT64_MAX);

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
