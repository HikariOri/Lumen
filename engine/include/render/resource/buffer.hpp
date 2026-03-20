/**
 * @file buffer.hpp
 * @brief VkBuffer 封装：顶点、索引、Uniform、Staging
 *
 * 提供 Buffer 创建、内存绑定与 RAII 管理。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
 * 通用类型，也可使用专用类型：VertexBuffer、IndexBuffer、UniformBuffer、StagingBuffer。
 */
class Buffer {
public:
    Buffer() = default;
    Buffer(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(const Buffer &) = delete;
    Buffer &operator=(Buffer &&other) noexcept;
    virtual ~Buffer();

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
    [[nodiscard]] bool is_valid() const { return buffer_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkBuffer buffer_ { VK_NULL_HANDLE };
    VkDeviceMemory memory_ { VK_NULL_HANDLE };
    size_t size_ { 0 };
};

/// 顶点 Buffer，默认 hostVisible 便于 CPU 上传
class VertexBuffer : public Buffer {
public:
    /**
     * @brief 创建顶点 Buffer
     * @param ctx 已初始化的 Context
     * @param size 字节大小
     * @param hostVisible 是否 CPU 可写，默认 true
     */
    bool create(const Context &ctx, size_t size, bool hostVisible = true);
};

/// 索引 Buffer，默认 hostVisible，支持指定索引类型
class IndexBuffer : public Buffer {
public:
    /// 索引元素类型
    enum class IndexType { Uint16, Uint32 };

    /**
     * @brief 创建索引 Buffer
     * @param ctx 已初始化的 Context
     * @param size 字节大小
     * @param hostVisible 是否 CPU 可写，默认 true
     */
    bool create(const Context &ctx, size_t size, bool hostVisible = true);

    /// 对应的 VkIndexType（用于 vkCmdBindIndexBuffer）
    [[nodiscard]] VkIndexType vk_index_type() const {
        return indexType_ == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16
                                               : VK_INDEX_TYPE_UINT32;
    }

    [[nodiscard]] IndexType index_type() const { return indexType_; }
    void set_index_type(IndexType t) { indexType_ = t; }

private:
    IndexType indexType_ { IndexType::Uint32 };
};

/// Uniform Buffer，通常 hostVisible 用于每帧更新
class UniformBuffer : public Buffer {
public:
    /**
     * @brief 创建 Uniform Buffer
     * @param ctx 已初始化的 Context
     * @param size 字节大小
     * @param hostVisible 是否 CPU 可写，默认 true
     */
    bool create(const Context &ctx, size_t size, bool hostVisible = true);

    /**
     * @brief 更新数据（每帧调用，需 hostVisible）
     * @param data 源数据
     * @param size 字节数
     * @param offset Buffer 内偏移
     */
    void update(const void *data, size_t size, size_t offset = 0);

    /**
     * @brief 更新数据（类型安全，常用于单个 struct）
     * @param data 源数据
     * @param offset Buffer 内偏移
     */
    template <typename T>
    void update(const T &data, size_t offset = 0) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "T must be trivially copyable");
        update(&data, sizeof(T), offset);
    }
};

/// Staging Buffer，用于 CPU->GPU 传输，必为 hostVisible
class StagingBuffer : public Buffer {
public:
    /**
     * @brief 创建 Staging Buffer
     * @param ctx 已初始化的 Context
     * @param size 字节大小
     */
    bool create(const Context &ctx, size_t size);
};

} // namespace render
} // namespace lumen
