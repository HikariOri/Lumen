/**
 * @file buffer.cpp
 * @brief Buffer 实现
 *
 * 实现 VkBuffer + VMA 分配封装。
 *
 * 关键点：
 * - 使用 VMA 自动管理内存（避免手写 vkAllocateMemory）
 * - 支持 hostVisible / device local 两类内存
 * - 提供 map / upload 接口用于 CPU 写入
 */

#include "render/resource/buffer.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"

#include <cstring>

namespace lumen::render {

namespace {

/**
 * @brief 将抽象 BufferUsage 转换为 Vulkan usage flags
 *
 * Vulkan 中必须在创建时声明 Buffer 的用途（不可修改）
 * 这些 flags 会影响：
 * - 可绑定的 pipeline stage
 * - 内存选择（VMA 会参考）
 *
 * 例如：
 * - Vertex → VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
 * - Staging → VK_BUFFER_USAGE_TRANSFER_SRC_BIT
 *
 * 注意：
 * - 多用途 buffer 可以组合 flags（本实现暂未支持组合）
 *
 * 参考：
 * usage flags 定义 buffer 在 pipeline 中的用途
 */
VkBufferUsageFlags to_usage_flags(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    /// Staging 本质是 transfer src
    case BufferUsage::Staging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    case BufferUsage::TransferSrc: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case BufferUsage::TransferDst: return VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    default: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
}

} // namespace

bool Buffer::create(const Context &ctx, const BufferCreateInfo &info) {
    /// 非法参数保护
    if (info.size == 0) {
        return false;
    }

    /// 获取 VMA allocator
    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        LUMEN_LOG_ERROR("Buffer 创建失败: VMA 未初始化");
        return false;
    }

    /// 保存上下文信息
    device_ = ctx.device();
    vma_allocator_ = vma;
    size_ = info.size;

    /**
     * @brief 填充 VkBufferCreateInfo
     */
    VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = info.size;
    bufferInfo.usage = to_usage_flags(info.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    /**
     * @brief VMA 分配策略
     *
     * VMA_MEMORY_USAGE_AUTO：
     * - 由 VMA 根据 usage 自动选择最优内存类型
     * - 通常：
     *   - GPU-only（device local）
     *   - CPU-visible（host visible）
     *
     * 参考：
     * VMA 会根据 usage 和 flags 自动选择内存类型
     * :contentReference[oaicite:1]{index=1}
     */
    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    /**
     * @brief Host 可见内存配置
     *
     * VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT：
     * - 优化 CPU 顺序写入
     * - 常用于：
     *   - Uniform buffer
     *   - Staging buffer
     *
     * 注意：
     * - 不保证是 HOST_COHERENT
     * - 复杂场景需要 flush / invalidate（本实现未处理）
     */
    if (info.hostVisible) {
        allocCreate.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    /**
     * @brief 创建 Buffer + 分配内存（一步完成）
     */
    VkResult result = vmaCreateBuffer(vma_allocator_, &bufferInfo, &allocCreate,
                                      &buffer_, &allocation_, nullptr);

    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Buffer 创建失败: {} size={}", static_cast<int>(result),
                        info.size);

        /// 回滚状态
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        buffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    LUMEN_LOG_DEBUG("Buffer 创建成功 size={} hostVisible={}", info.size,
                    info.hostVisible);

    return true;
}

/**
 * @brief 上传数据（简化接口）
 *
 * 内部流程：
 *   map → memcpy → unmap
 *
 * 注意：
 * - 仅适用于 hostVisible buffer
 * - device local buffer 应使用 staging
 *
 * staging 原理：
 * - CPU 写 staging buffer
 * - 再 copy 到 GPU buffer :contentReference[oaicite:2]{index=2}
 */
void Buffer::upload(const void *data, size_t size, size_t offset) {
    if (!data || size == 0 || allocation_ == nullptr) {
        return;
    }

    void *ptr = map();
    if (!ptr) {
        return;
    }

    /// memcpy 写入
    memcpy(static_cast<char *>(ptr) + offset, data, size);

    unmap();
}

/**
 * @brief 映射内存（CPU 可访问）
 *
 * 返回：
 * - 成功：CPU 指针
 * - 失败：nullptr
 *
 * 注意：
 * - 仅 hostVisible 有效
 * - 非 coherent 内存需要手动 flush（此处未处理）
 */
void *Buffer::map() {
    if (allocation_ == nullptr || vma_allocator_ == nullptr) {
        return nullptr;
    }

    void *ptr { nullptr };
    VkResult result = vmaMapMemory(vma_allocator_, allocation_, &ptr);

    return result == VK_SUCCESS ? ptr : nullptr;
}

/**
 * @brief 解除映射
 */
void Buffer::unmap() {
    if (allocation_ != nullptr && vma_allocator_ != nullptr) {
        vmaUnmapMemory(vma_allocator_, allocation_);
    }
}

/**
 * @brief 销毁 Buffer
 *
 * VMA 会：
 * - 释放 VkBuffer
 * - 释放内存 allocation
 */
void Buffer::destroy_() {
    if (buffer_ != VK_NULL_HANDLE && vma_allocator_ != nullptr &&
        allocation_ != nullptr) {

        vmaDestroyBuffer(vma_allocator_, buffer_, allocation_);

        buffer_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }

    vma_allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
    size_ = 0;
}

Buffer::~Buffer() { destroy_(); }

/**
 * @brief 移动构造
 *
 * 转移 GPU 资源所有权
 */
Buffer::Buffer(Buffer &&other) noexcept
    : device_ { other.device_ }, vma_allocator_ { other.vma_allocator_ },
      buffer_ { other.buffer_ }, allocation_ { other.allocation_ },
      size_ { other.size_ } {

    /// 清空原对象
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_ = 0;
}

/**
 * @brief 移动赋值
 */
Buffer &Buffer::operator=(Buffer &&other) noexcept {
    if (this == &other)
        return *this;

    destroy_();

    device_ = other.device_;
    vma_allocator_ = other.vma_allocator_;
    buffer_ = other.buffer_;
    allocation_ = other.allocation_;
    size_ = other.size_;

    /// 清空源对象
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_ = 0;

    return *this;
}

/**
 * @brief VertexBuffer 创建
 */
bool VertexBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Vertex, hostVisible });
}

/**
 * @brief IndexBuffer 创建
 */
bool IndexBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Index, hostVisible });
}

/**
 * @brief UniformBuffer 创建
 */
bool UniformBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Uniform, hostVisible });
}

/**
 * @brief UniformBuffer 更新
 *
 * 实际就是 upload（语义更清晰）
 */
void UniformBuffer::update(const void *data, size_t size, size_t offset) {
    upload(data, size, offset);
}

/**
 * @brief StagingBuffer 创建
 *
 * 特点：
 * - 必为 hostVisible
 * - 用于 CPU → GPU 拷贝
 */
bool StagingBuffer::create(const Context &ctx, size_t size) {
    return Buffer::create(ctx, { size, BufferUsage::Staging, true });
}

} // namespace lumen::render
