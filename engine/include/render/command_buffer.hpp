/**
 * @file command_buffer.hpp
 * @brief CommandPool、命令缓冲类型别名与帧同步聚合入口
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
 * @note `FrameSync` 定义见 `frame_sync.hpp`，此处包含以便沿用
 * `#include "render/command_buffer.hpp"` 即可拿到帧同步类型。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

#include "render/frame_sync.hpp"

namespace lumen {
namespace render {

class Context;
class CommandPool;

/**
 * @brief 命令缓冲句柄（非 RAII：不 `vkFree`；须 `CommandPool::free` 或池 `reset`）
 *
 * 有效 `VkCommandBuffer` **仅**能由 `CommandPool::allocate` 产生；无公开构造函数从
 * `VkCommandBuffer` 包装。默认构造 / 移动后为空句柄。
 */
class CommandBuffer {
public:
    CommandBuffer() noexcept = default;
    CommandBuffer(const CommandBuffer &) = delete;
    CommandBuffer &operator=(const CommandBuffer &) = delete;
    CommandBuffer(CommandBuffer &&other) noexcept;
    CommandBuffer &operator=(CommandBuffer &&other) noexcept;
    ~CommandBuffer() = default;

    [[nodiscard]] VkCommandBuffer handle() const noexcept { return vk_; }
    [[nodiscard]] bool is_valid() const noexcept {
        return vk_ != VK_NULL_HANDLE;
    }

    /// 传入 Vulkan C API（与 `handle()` 相同）
    [[nodiscard]] operator VkCommandBuffer() const noexcept { return vk_; }

private:
    friend class CommandPool;
    explicit CommandBuffer(VkCommandBuffer vk) noexcept : vk_(vk) {}

    VkCommandBuffer vk_ { VK_NULL_HANDLE };
};

/**
 * @brief 与 `VkCommandBufferLevel` 对应的引擎侧枚举
 */
enum class CommandBufferLevel {
    Primary,
    Secondary,
};

[[nodiscard]] VkCommandBufferLevel
command_buffer_level_to_vk(CommandBufferLevel level);

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
     * @return 成功返回句柄列表
     *
     * Primary 可直接 vkQueueSubmit；Secondary 只能被 primary 调用。
     */
    std::vector<CommandBuffer>
    allocate(uint32_t count,
             CommandBufferLevel level = CommandBufferLevel::Primary);

    /**
     * @brief 释放 CommandBuffer
     *
     * 释放由此命令池分配的 command buffer。注意可以批量释放。
     */
    void free(const std::vector<CommandBuffer> &buffers);

    /**
     * @brief 重置命令池中所有分配（等价于 vkResetCommandPool）
     *
     * 在开始下一帧录制 command buffers 之前，常 reset 以重用。
     */
    void reset();

    /**
     * @brief 单次提交：分配 1 个 primary buffer，`ONE_TIME_SUBMIT_BIT` 录制、提交并等待完成
     *
     * 适用于纹理上传、一次性 blit 等与帧循环无关的命令。调用方须保证
     * `queue` 与创建本池时使用的队列族一致。
     *
     * @param queue 提交目标队列（通常为 `Context::graphics_queue()`）
     * @param record 录制闭包（在 `vkBeginCommandBuffer` 与 `vkEndCommandBuffer` 之间调用）
     * @return 任一步失败返回 false（buffer 仍会释放）
     */
    bool submit_one_shot(
        VkQueue queue,
        const std::function<void(const CommandBuffer &cmd)> &record);

    [[nodiscard]] VkCommandPool handle() const { return pool_; }
    [[nodiscard]] bool is_valid() const { return pool_ != VK_NULL_HANDLE; }

private:
    void destroy_();
    /// 释放单个 buffer（`std::initializer_list` 会拷贝，故不用 `free({cb})`）
    void free_moved(CommandBuffer &&cb);

    VkDevice device_ { VK_NULL_HANDLE };
    VkCommandPool pool_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
