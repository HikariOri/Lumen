/**
 * @file command_buffer.hpp
 * @brief CommandPool 与 CommandBuffer、帧同步对象
 *
 * 管理 CommandPool、CommandBuffer 的分配与录制，
 * 以及 per-frame 的 Semaphore、Fence。
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
         * @brief 命令池
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
             * @param ctx Context
             * @param queueFamilyIndex 队列族索引
             * @return 成功返回 true
             */
            bool create(const Context &ctx, uint32_t queueFamilyIndex);

            /**
             * @brief 分配 CommandBuffer
             * @param count 数量
             * @param level 主要或次要
             */
            std::vector<VkCommandBuffer> allocate(
                uint32_t count,
                VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

            /**
             * @brief 释放 CommandBuffer
             */
            void free(const std::vector<VkCommandBuffer> &buffers);

            void reset();

            [[nodiscard]] VkCommandPool handle() const { return pool_; }
            [[nodiscard]] bool is_valid() const {
                return pool_ != VK_NULL_HANDLE;
            }

        private:
            void destroy_();

            VkDevice device_ { VK_NULL_HANDLE };
            VkCommandPool pool_ { VK_NULL_HANDLE };
        };

        /**
         * @class FrameSync
         * @brief 每帧同步对象：Semaphore、Fence
         *
         * 支持多帧并发（frames in flight）。
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
             * @brief 创建 per-frame 的 Semaphore 与 Fence
             * @param device VkDevice
             * @param framesInFlight 并发帧数（通常 2 或 3）
             * @return 成功返回 true
             */
            bool create(VkDevice device, uint32_t framesInFlight);

            /**
             * @brief 创建 per-image 的 Semaphore 与 per-frame 的 Fence
             * 用于 Swapchain：每个 image 独立的 semaphore 避免复用冲突
             * @param device VkDevice
             * @param swapchainImageCount swapchain 图像数量
             * @param framesInFlight 并发帧数（fence 数量）
             */
            bool create(VkDevice device, uint32_t swapchainImageCount,
                       uint32_t framesInFlight);

            /**
             * @brief 等待 Fence 并重置
             * @param frameIndex 帧索引
             * @param timeoutNs 超时纳秒，UINT64_MAX 表示无限等待
             * @return true 表示 fence 已 signal 并已 reset；false 表示超时（未 reset，调用方可重试）
             */
            bool wait_fence(uint32_t frameIndex,
                           uint64_t timeoutNs = UINT64_MAX);

            /**
             * @brief 获取 image 的 imageAvailable Semaphore（按 imageIndex 索引）
             */
            [[nodiscard]] VkSemaphore
            image_available(uint32_t imageIndex) const;

            /**
             * @brief 获取 image 的 renderFinished Semaphore（按 imageIndex 索引）
             */
            [[nodiscard]] VkSemaphore
            render_finished(uint32_t imageIndex) const;

            /**
             * @brief 获取当前帧的 Fence（按 frameIndex 索引）
             */
            [[nodiscard]] VkFence in_flight_fence(uint32_t frameIndex) const;

            [[nodiscard]] uint32_t frame_count() const {
                return static_cast<uint32_t>(inFlightFences_.size());
            }

        private:
            void destroy_();

            VkDevice device_ { VK_NULL_HANDLE };
            std::vector<VkSemaphore> imageAvailable_;
            std::vector<VkSemaphore> renderFinished_;
            std::vector<VkFence> inFlightFences_;
        };

    } // namespace render
} // namespace lumen
