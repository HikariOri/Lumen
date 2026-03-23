/**
 * @file buffer.cpp
 * @brief Buffer 实现
 */

#include "render/resource/buffer.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"

#include <cstring>

namespace lumen::render {

namespace {

VkBufferUsageFlags to_usage_flags(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case BufferUsage::Staging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case BufferUsage::TransferSrc: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    case BufferUsage::TransferDst: return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    default: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
}

} // namespace

bool Buffer::create(const Context &ctx, const BufferCreateInfo &info) {
    if (info.size == 0)
        return false;

    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        LUMEN_LOG_ERROR("Buffer 创建失败: VMA 未初始化");
        return false;
    }

    device_ = ctx.device();
    vma_allocator_ = vma;
    size_ = info.size;

    VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = info.size;
    bufferInfo.usage = to_usage_flags(info.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;
    if (info.hostVisible) {
        allocCreate.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VkResult result = vmaCreateBuffer(vma_allocator_, &bufferInfo, &allocCreate,
                                      &buffer_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Buffer 创建失败: {} size={}", static_cast<int>(result),
                        info.size);
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

void Buffer::upload(const void *data, size_t size, size_t offset) {
    if (!data || size == 0 || allocation_ == nullptr)
        return;
    void *ptr = map();
    if (!ptr)
        return;
    memcpy(static_cast<char *>(ptr) + offset, data, size);
    unmap();
}

void *Buffer::map() {
    if (allocation_ == nullptr || vma_allocator_ == nullptr)
        return nullptr;
    void *ptr { nullptr };
    VkResult result = vmaMapMemory(vma_allocator_, allocation_, &ptr);
    return result == VK_SUCCESS ? ptr : nullptr;
}

void Buffer::unmap() {
    if (allocation_ != nullptr && vma_allocator_ != nullptr) {
        vmaUnmapMemory(vma_allocator_, allocation_);
    }
}

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

Buffer::Buffer(Buffer &&other) noexcept
    : device_ { other.device_ }, vma_allocator_ { other.vma_allocator_ },
      buffer_ { other.buffer_ }, allocation_ { other.allocation_ },
      size_ { other.size_ } {
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_ = 0;
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    vma_allocator_ = other.vma_allocator_;
    buffer_ = other.buffer_;
    allocation_ = other.allocation_;
    size_ = other.size_;
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_ = 0;
    return *this;
}

bool VertexBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Vertex, hostVisible });
}

bool IndexBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Index, hostVisible });
}

bool UniformBuffer::create(const Context &ctx, size_t size, bool hostVisible) {
    return Buffer::create(ctx, { size, BufferUsage::Uniform, hostVisible });
}

void UniformBuffer::update(const void *data, size_t size, size_t offset) {
    upload(data, size, offset);
}

bool StagingBuffer::create(const Context &ctx, size_t size) {
    return Buffer::create(ctx, { size, BufferUsage::Staging, true });
}

} // namespace lumen::render
