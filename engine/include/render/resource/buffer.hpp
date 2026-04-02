/**
 * @file buffer.hpp
 * @brief Vulkan Buffer 封装：顶点、索引、Uniform、Staging
 *
 * 封装 Vulkan Buffer + VMA 分配，提供统一的资源生命周期管理。
 *
 * 设计目标：
 * 1. 简化 Buffer + VkDeviceMemory（或 VMA Allocation）的管理
 * 2. 提供统一的 CPU/GPU 数据上传接口
 * 3. 支持不同用途的 Buffer（Vertex / Index / Uniform / Staging）
 *
 * 参考：
 * - Vulkan 中 Buffer 与 Memory 是分离的，需要手动绑定
 * - hostVisible 内存可映射，device local 内存性能更高
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <vk_mem_alloc.h>
#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Context;
class CommandPool;

/**
 * @brief Buffer 创建信息
 */
struct BufferCreateInfo {
    /// Buffer 大小（字节）
    size_t size { 0 };

    /// Vulkan usage（可用 `|` 组合，如 `eVertexBuffer | eTransferDst`）
    vk::BufferUsageFlags usage { vk::BufferUsageFlagBits::eVertexBuffer };

    /**
     * @brief 是否 Host 可见
     *
     * true：
     *   - 内存可 map（CPU 可写）
     *   - 适合 Uniform / Staging
     *
     * false：
     *   - 通常为 device local（GPU 更快）
     *   - 需通过 staging buffer 上传数据
     */
    bool hostVisible { false };

    /**
     * @brief 分配时保持 CPU 映射（仅 hostVisible 时有效）
     *
     * 适合每帧 Uniform 更新，避免反复 vmaMapMemory / vmaUnmapMemory。
     */
    bool persistentlyMapped { false };

    /**
     * @brief 与 hostVisible 配合：GPU 写入后 CPU 可读（如 `vkCmdCopyImageToBuffer` 回读）
     *
     * 为真时使用 `VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT`，否则为
     * `HOST_ACCESS_SEQUENTIAL_WRITE`。
     */
    bool hostRandomAccess { false };
};

/**
 * @class Buffer
 * @brief Vulkan Buffer 通用封装（RAII）
 *
 * 生命周期：
 * create() → 使用 → 析构自动释放
 *
 * 内部：
 * - `vk::Buffer`
 * - VmaAllocation（自动管理内存）
 *
 * 注意：
 * - 不可拷贝（避免重复释放 GPU 资源）
 * - 支持移动语义
 */
class Buffer {
public:
    Buffer() = default;

    /// 禁止拷贝（避免重复释放 GPU 资源）
    Buffer(const Buffer &) = delete;

    /// 支持移动
    Buffer(Buffer &&rhs) noexcept;
    Buffer &operator=(const Buffer &) = delete;
    Buffer &operator=(Buffer &&rhs) noexcept;

    /// 析构自动释放 GPU 资源
    virtual ~Buffer();

    /**
     * @brief 创建 Buffer 并分配内存
     *
     * 若已存在有效 buffer，会先 destroy 再创建。
     */
    bool create(const Context &ctx, const BufferCreateInfo &createInfo);

    /**
     * @brief 上传数据（仅 hostVisible）
     *
     * 持久映射时直接写入映射基址；否则 map → memcpy → unmap。
     * 非 HOST_COHERENT 内存在 memcpy 后会 vmaFlushAllocation。
     */
    void upload(const void *srcBytes, size_t byteCount, size_t byteOffset = 0);

    /**
     * @brief 映射内存
     *
     * 持久映射时返回创建时得到的指针，否则 vmaMapMemory。
     */
    void *map();

    /**
     * @brief 解除映射
     *
     * 持久映射时为 no-op（由 vmaDestroyBuffer 释放）。
     */
    void unmap();

    /**
     * @brief GPU 写入 host-visible 内存后，使 CPU 读可见（`vkCmdCopyImageToBuffer` 等之后）
     */
    void invalidate_mapped_range(size_t byte_offset, size_t byte_count);

    /// 获取 `vk::Buffer`
    [[nodiscard]] vk::Buffer handle() const { return buffer_; }

    /// 获取大小
    [[nodiscard]] size_t size() const { return byteSize; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return static_cast<bool>(buffer_); }

protected:
    void destroy_();

    /**
     * @brief 创建 device-local buffer，经 Staging 拷贝并 submit（派生类共用）
     *
     * @note 内部一次 `submit_one_shot`；若要在同一 submit 中上传多个 buffer，
     *       应自行 `Buffer::create` + `StagingBuffer` + 录制 `vkCmdCopyBuffer`。
     */
    bool create_device_local_upload_impl(const Context &ctx, vk::Queue transferQueue,
                                         CommandPool &cmdPool,
                                         vk::BufferUsageFlags usage,
                                         const void *srcBytes, size_t byteCount);

private:
    vk::Device device_ {};
    VmaAllocator vmaAllocator { nullptr };

    vk::Buffer buffer_ {};
    VmaAllocation vmaAllocation { nullptr };

    size_t byteSize { 0 };
    void *persistentMappedBase { nullptr };
};

/**
 * @brief 顶点 Buffer
 *
 * 默认 hostVisible = true（示例/快速路径）。
 * 静态网格优先使用 create_device_local_and_upload。
 */
class VertexBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t byteCount, bool hostVisible = true);

    /**
     * @brief device-local 顶点缓冲 + Staging 上传（单次队列提交）
     *
     * 等价于：`create(device-local + TRANSFER_DST)` → staging → `vkCmdCopyBuffer`。
     */
    bool create_device_local_and_upload(const Context &ctx, vk::Queue transferQueue,
                                        CommandPool &cmdPool, const void *srcBytes,
                                        size_t byteCount);
};

/**
 * @brief 索引 Buffer
 */
class IndexBuffer : public Buffer {
public:
    /// 索引类型
    enum class IndexType { Uint16, Uint32 };

    bool create(const Context &ctx, size_t byteCount, bool hostVisible = true);

    /**
     * @brief device-local 索引缓冲 + Staging 上传（单次队列提交）
     */
    bool create_device_local_and_upload(const Context &ctx, vk::Queue transferQueue,
                                        CommandPool &cmdPool, const void *srcBytes,
                                        size_t byteCount);

    /**
     * @brief 索引元素类型（`vkCmdBindIndexBuffer`）
     */
    [[nodiscard]] vk::IndexType vk_index_type() const {
        return indexType == IndexType::Uint16 ? vk::IndexType::eUint16
                                              : vk::IndexType::eUint32;
    }

    [[nodiscard]] IndexType index_type() const { return indexType; }

    /// 设置索引类型
    void set_index_type(IndexType newType) { indexType = newType; }

private:
    IndexType indexType { IndexType::Uint32 };
};

/**
 * @brief Uniform Buffer
 *
 * 特点：
 * - 高频更新（每帧）
 * - 通常 hostVisible
 *
 * 推荐：
 * - 每帧更新用 create_persistent + update（少 map/unmap）
 * - 或使用 ring buffer / dynamic offset
 */
class UniformBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t byteCount, bool hostVisible = true);

    /**
     * @brief 持久映射的 Uniform（适合每帧 update）
     */
    bool create_persistent(const Context &ctx, size_t byteCount);

    void update(const void *srcBytes, size_t byteCount, size_t byteOffset = 0);

    template <typename T>
    void update(const T &value, size_t byteOffset = 0) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "T must be trivially copyable");
        update(&value, sizeof(T), byteOffset);
    }
};

/**
 * @brief Staging Buffer
 *
 * 用途：
 * - CPU → GPU 数据传输
 *
 * 特点：
 * - 必为 hostVisible
 * - usage = TRANSFER_SRC
 *
 * 典型流程：
 * staging → copy → device local buffer
 */
class StagingBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t byteCount);
};

} // namespace render
} // namespace lumen
