/**
 * @file buffer.hpp
 * @brief VkBuffer 封装：顶点、索引、Uniform、Staging
 *
 * 提供 Buffer 创建、内存绑定与 RAII 管理。
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace lumen {
    namespace render {

        class Context;

        /// Buffer 使用类型
        enum class BufferUsage {
            Vertex,
            Index,
            Uniform,
            Storage,
            Staging,
            TransferSrc,
            TransferDst,
        };

        /// Buffer 创建信息
        struct BufferCreateInfo {
            /// 大小（字节）
            size_t size { 0 };
            BufferUsage usage { BufferUsage::Vertex };
            /// 是否 Host 可见（用于 CPU 上传）
            bool hostVisible { false };
        };

        /**
         * @class Buffer
         * @brief Vulkan Buffer 封装
         *
         * RAII 管理 Buffer 与 DeviceMemory。
         */
        class Buffer {
        public:
            Buffer() = default;
            Buffer(const Buffer &) = delete;
            Buffer(Buffer &&other) noexcept;
            Buffer &operator=(const Buffer &) = delete;
            Buffer &operator=(Buffer &&other) noexcept;
            ~Buffer();

            /**
             * @brief 创建 Buffer 并分配内存
             * @param ctx 已初始化的 Context
             * @param info 创建信息
             * @return 成功返回 true
             */
            bool create(const Context &ctx, const BufferCreateInfo &info);

            /**
             * @brief 上传数据到 Buffer（需 hostVisible）
             * @param data 源数据指针
             * @param size 字节数
             * @param offset Buffer 内偏移
             */
            void upload(const void *data, size_t size, size_t offset = 0);

            /**
             * @brief 映射内存指针（hostVisible 时有效）
             * @return 映射的指针，失败返回 nullptr
             */
            void *map();

            /**
             * @brief 解除映射
             */
            void unmap();

            /// VkBuffer 句柄
            [[nodiscard]] VkBuffer handle() const { return buffer_; }

            /// 分配的大小（字节）
            [[nodiscard]] size_t size() const { return size_; }

            /// 是否有效
            [[nodiscard]] bool is_valid() const {
                return buffer_ != VK_NULL_HANDLE;
            }

        private:
            void destroy_();

            VkDevice device_ { VK_NULL_HANDLE };
            VkBuffer buffer_ { VK_NULL_HANDLE };
            VkDeviceMemory memory_ { VK_NULL_HANDLE };
            size_t size_ { 0 };
        };

    } // namespace render
} // namespace lumen
