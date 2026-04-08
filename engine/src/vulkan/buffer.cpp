/**
 * @file buffer.cpp
 * @brief `vulkan::Buffer` 实现。
 */

#include "vulkan/buffer.hpp"

namespace vulkan {

Buffer::Buffer(const VmaAllocator allocator, const VkBuffer buf,
               const VmaAllocation allocation, const VkDeviceSize byteSize)
    : vmaAllocator_(allocator), vkBuffer_(buf), vmaAllocation_(allocation),
      byteSize_(byteSize) {}

void Buffer::destroy() noexcept {
    if (vmaAllocator_ != VK_NULL_HANDLE && vkBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(vmaAllocator_, vkBuffer_, vmaAllocation_);
    }
    vmaAllocator_ = VK_NULL_HANDLE;
    vkBuffer_ = VK_NULL_HANDLE;
    vmaAllocation_ = VK_NULL_HANDLE;
    byteSize_ = 0;
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer &&other) noexcept
    : vmaAllocator_(other.vmaAllocator_), vkBuffer_(other.vkBuffer_),
      vmaAllocation_(other.vmaAllocation_), byteSize_(other.byteSize_) {
    other.vmaAllocator_ = VK_NULL_HANDLE;
    other.vkBuffer_ = VK_NULL_HANDLE;
    other.vmaAllocation_ = VK_NULL_HANDLE;
    other.byteSize_ = 0;
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
    if (this != &other) {
        destroy();
        vmaAllocator_ = other.vmaAllocator_;
        vkBuffer_ = other.vkBuffer_;
        vmaAllocation_ = other.vmaAllocation_;
        byteSize_ = other.byteSize_;
        other.vmaAllocator_ = VK_NULL_HANDLE;
        other.vkBuffer_ = VK_NULL_HANDLE;
        other.vmaAllocation_ = VK_NULL_HANDLE;
        other.byteSize_ = 0;
    }
    return *this;
}

std::expected<Buffer, std::string>
Buffer::create(const VmaAllocator allocator, const BufferCreateInfo &info) {
    if (allocator == VK_NULL_HANDLE) {
        return std::unexpected(std::string("Buffer::create: null allocator"));
    }
    if (info.size == 0) {
        return std::unexpected(std::string("Buffer::create: size is 0"));
    }
    if (info.usage == 0U) {
        return std::unexpected(std::string("Buffer::create: usage is 0"));
    }

    VkBufferCreateInfo bufferInfo { .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = info.size;
    bufferInfo.usage = info.usage;
    bufferInfo.sharingMode = info.sharingMode;

    VmaAllocationCreateInfo allocInfo {};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.allocationFlags;

    VkBuffer vkBuf { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    const VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                            &vkBuf, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        return std::unexpected(
            std::string("Buffer::create: vmaCreateBuffer failed ec=") +
            std::to_string(static_cast<int>(result)));
    }
    return Buffer(allocator, vkBuf, allocation, info.size);
}

std::expected<void *, std::string> Buffer::map() {
    if (vmaAllocator_ == VK_NULL_HANDLE || vmaAllocation_ == VK_NULL_HANDLE) {
        return std::unexpected(std::string("Buffer::map: invalid buffer"));
    }
    void *ptr { nullptr };
    const VkResult result =
        vmaMapMemory(vmaAllocator_, vmaAllocation_, &ptr);
    if (result != VK_SUCCESS) {
        return std::unexpected(
            std::string("Buffer::map: vmaMapMemory failed ec=") +
            std::to_string(static_cast<int>(result)));
    }
    return ptr;
}

void Buffer::unmap() {
    if (vmaAllocator_ != VK_NULL_HANDLE && vmaAllocation_ != VK_NULL_HANDLE) {
        vmaUnmapMemory(vmaAllocator_, vmaAllocation_);
    }
}

} // namespace vulkan
