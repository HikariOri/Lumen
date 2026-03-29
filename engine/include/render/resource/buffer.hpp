/**
 * @file buffer.hpp
 * @brief VkBuffer 封装：顶点、索引、Uniform、Staging
 *
 * 封装 Vulkan Buffer + VMA 分配，提供统一的资源生命周期管理。
 *
 * 设计目标：
 * 1. 简化 VkBuffer + VkDeviceMemory（或 VMA Allocation）的管理
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
#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;
class CommandPool;

/**
 * @brief Buffer 使用类型
 *
 * 抽象 Vulkan 的 VkBufferUsageFlags
 */
enum class BufferUsage : uint8_t {
    Vertex,      ///< 顶点数据（VK_BUFFER_USAGE_VERTEX_BUFFER_BIT）
    Index,       ///< 索引数据（VK_BUFFER_USAGE_INDEX_BUFFER_BIT）
    Uniform,     ///< Uniform Buffer（常用于每帧更新）
    Storage,     ///< Storage Buffer（Compute / SSBO）
    Staging,     ///< Staging（CPU→GPU 中转）
    TransferSrc, ///< 拷贝源
    TransferDst, ///< 拷贝目标
};

/**
 * @brief Buffer 创建信息
 */
struct BufferCreateInfo {
    /// Buffer 大小（字节）
    size_t size { 0 };

    /// 使用类型（影响 usage flags）
    BufferUsage usage { BufferUsage::Vertex };

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
     * @brief 与 usage 枚举按位或的额外 Vulkan usage（如 TRANSFER_DST）
     */
    VkBufferUsageFlags usageFlagsExtra { 0 };

    /**
     * @brief 分配时保持 CPU 映射（仅 hostVisible 时有效）
     *
     * 适合每帧 Uniform 更新，避免反复 vmaMapMemory / vmaUnmapMemory。
     */
    bool persistentlyMapped { false };
};

/**
 * @class Buffer
 * @brief Vulkan Buffer 通用封装（RAII）
 *
 * 生命周期：
 * create() → 使用 → 析构自动释放
 *
 * 内部：
 * - VkBuffer
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

    /// 获取 VkBuffer
    [[nodiscard]] VkBuffer handle() const { return vkBuffer; }

    /// 获取大小
    [[nodiscard]] size_t size() const { return byteSize; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return vkBuffer != VK_NULL_HANDLE; }

protected:
    void destroy_();

    /**
     * @brief 创建 device-local buffer，经 Staging 拷贝并 submit（派生类共用）
     *
     * @note 内部一次 `submit_one_shot`；若要在同一 submit 中上传多个 buffer，
     *       应自行 `Buffer::create` + `StagingBuffer` + 录制 `vkCmdCopyBuffer`。
     */
    bool create_device_local_upload_impl(const Context &ctx, VkQueue transferQueue,
                                         CommandPool &cmdPool, BufferUsage usage,
                                         const void *srcBytes, size_t byteCount);

private:
    VkDevice vkDevice { VK_NULL_HANDLE };
    VmaAllocator vmaAllocator { nullptr };

    VkBuffer vkBuffer { VK_NULL_HANDLE };
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
    bool create_device_local_and_upload(const Context &ctx, VkQueue transferQueue,
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
    bool create_device_local_and_upload(const Context &ctx, VkQueue transferQueue,
                                        CommandPool &cmdPool, const void *srcBytes,
                                        size_t byteCount);

    /**
     * @brief 转换为 Vulkan 类型
     */
    [[nodiscard]] VkIndexType vk_index_type() const {
        return indexType == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16
                                              : VK_INDEX_TYPE_UINT32;
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
