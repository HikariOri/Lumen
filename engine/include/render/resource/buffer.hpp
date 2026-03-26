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

#include <concepts>
#include <cstddef>
#include <cstdint>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

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
 * - 不可拷贝（GPU 资源）
 * - 支持移动语义
 */
class Buffer {
public:
    Buffer() = default;

    /// 禁止拷贝（避免重复释放 GPU 资源）
    Buffer(const Buffer &) = delete;

    /// 支持移动
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(const Buffer &) = delete;
    Buffer &operator=(Buffer &&other) noexcept;

    /// 析构自动释放 GPU 资源
    virtual ~Buffer();

    /**
     * @brief 创建 Buffer 并分配内存
     *
     * 内部流程：
     * 1. 填充 VkBufferCreateInfo（size + usage）
     * 2. 使用 VMA 分配内存
     * 3. 绑定 Buffer 与 Allocation
     *
     * @param ctx Vulkan 上下文
     * @param info 创建参数
     * @return 是否成功
     */
    bool create(const Context &ctx, const BufferCreateInfo &info);

    /**
     * @brief 上传数据（仅 hostVisible）
     *
     * 等价：
     *   map → memcpy → unmap
     *
     * 注意：
     * - 非 hostVisible Buffer 不能直接调用
     * - device local Buffer 需使用 staging
     *
     * @param data 源数据
     * @param size 数据大小
     * @param offset 偏移
     */
    void upload(const void *data, size_t size, size_t offset = 0);

    /**
     * @brief 映射内存
     *
     * @return CPU 可访问指针
     *
     * 注意：
     * - 仅 hostVisible 有效
     * - 非 coherent 内存需要 flush
     */
    void *map();

    /**
     * @brief 解除映射
     */
    void unmap();

    /// 获取 VkBuffer
    [[nodiscard]] VkBuffer handle() const { return buffer_; }

    /// 获取大小
    [[nodiscard]] size_t size() const { return size_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return buffer_ != VK_NULL_HANDLE; }

private:
    /// 销毁资源
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VmaAllocator vma_allocator_ { nullptr };

    VkBuffer buffer_ { VK_NULL_HANDLE };
    VmaAllocation allocation_ { nullptr };

    size_t size_ { 0 };
};

/**
 * @brief 顶点 Buffer
 *
 * 默认 hostVisible = true（方便 CPU 上传）
 *
 * 实际工程优化：
 * - 大型模型建议 device local + staging
 */
class VertexBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t size, bool hostVisible = true);
};

/**
 * @brief 索引 Buffer
 */
class IndexBuffer : public Buffer {
public:
    /// 索引类型
    enum class IndexType { Uint16, Uint32 };

    bool create(const Context &ctx, size_t size, bool hostVisible = true);

    /**
     * @brief 转换为 Vulkan 类型
     */
    [[nodiscard]] VkIndexType vk_index_type() const {
        return indexType_ == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16
                                               : VK_INDEX_TYPE_UINT32;
    }

    [[nodiscard]] IndexType index_type() const { return indexType_; }

    /// 设置索引类型
    void set_index_type(IndexType t) { indexType_ = t; }

private:
    IndexType indexType_ { IndexType::Uint32 };
};

/**
 * @brief Uniform Buffer
 *
 * 特点：
 * - 高频更新（每帧）
 * - 必须 hostVisible
 *
 * 推荐：
 * - 使用 ring buffer / dynamic offset
 */
class UniformBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t size, bool hostVisible = true);

    /**
     * @brief 更新数据（底层 memcpy）
     */
    void update(const void *data, size_t size, size_t offset = 0);

    /**
     * @brief 类型安全更新
     *
     * 限制：
     * - T 必须 trivially copyable
     */
    template <typename T>
    void update(const T &data, size_t offset = 0) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "T must be trivially copyable");
        update(&data, sizeof(T), offset);
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
 *
 * 参考：
 * staging buffer 用于拷贝到 GPU 本地内存 :contentReference[oaicite:2]{index=2}
 */
class StagingBuffer : public Buffer {
public:
    bool create(const Context &ctx, size_t size);
};

} // namespace render
} // namespace lumen
