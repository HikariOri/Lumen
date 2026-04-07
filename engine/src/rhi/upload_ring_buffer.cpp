#include "rhi/upload_ring_buffer.hpp"

#include "core/log/logger.hpp"

#include <cstring>

namespace rhi {

namespace {

[[nodiscard]] vk::DeviceSize align_up_size(vk::DeviceSize v,
                                           vk::DeviceSize a) noexcept {
    if (a == 0) {
        return v;
    }
    return (v + a - 1) / a * a;
}

} // namespace

bool UploadRingBuffer::init(VmaAllocator allocator,
                            vk::DeviceSize capacity_bytes) {
    shutdown(allocator);
    if (allocator == nullptr || capacity_bytes == 0) {
        return false;
    }
    allocator_ = allocator;
    capacity_ = capacity_bytes;
    head_ = 0;

    VkBufferCreateInfo bci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = static_cast<VkDeviceSize>(capacity_);
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buf {};
    const VkResult vr =
        vmaCreateBuffer(allocator_, &bci, &aci, &buf, &allocation_, nullptr);
    if (vr != VK_SUCCESS) {
        LUMEN_LOG_ERROR("UploadRingBuffer: vmaCreateBuffer 失败 {}",
                        static_cast<int>(vr));
        capacity_ = 0;
        allocator_ = nullptr;
        return false;
    }
    buffer_ = vk::Buffer { buf };
    VmaAllocationInfo alloc_info {};
    vmaGetAllocationInfo(allocator_, allocation_, &alloc_info);
    mapped_ = alloc_info.pMappedData;
    if (mapped_ == nullptr) {
        LUMEN_LOG_ERROR("UploadRingBuffer: MAPPED_BIT 但 pMappedData 为空");
        vmaDestroyBuffer(allocator_, buf, allocation_);
        buffer_ = nullptr;
        allocation_ = nullptr;
        capacity_ = 0;
        allocator_ = nullptr;
        return false;
    }
    return true;
}

void UploadRingBuffer::shutdown(VmaAllocator allocator) {
    if (allocation_ != nullptr &&
        (allocator != nullptr || allocator_ != nullptr)) {
        VmaAllocator a = allocator != nullptr ? allocator : allocator_;
        vmaDestroyBuffer(a, static_cast<VkBuffer>(buffer_), allocation_);
    }
    buffer_ = nullptr;
    allocation_ = nullptr;
    mapped_ = nullptr;
    capacity_ = 0;
    head_ = 0;
    allocator_ = nullptr;
}

UploadRingBuffer::Allocation UploadRingBuffer::allocate(const std::size_t size,
                                                        std::size_t align) {
    Allocation out {};
    if (mapped_ == nullptr || size == 0 || capacity_ == 0) {
        return out;
    }
    if (align == 0) {
        align = 1;
    }
    vk::DeviceSize cursor =
        align_up_size(head_, static_cast<vk::DeviceSize>(align));
    const vk::DeviceSize need = static_cast<vk::DeviceSize>(size);
    if (cursor + need > capacity_) {
        LUMEN_LOG_ERROR("UploadRingBuffer: 本帧 staging 不足 (need {} 已用 {})",
                        static_cast<std::uint64_t>(need),
                        static_cast<std::uint64_t>(cursor));
        return out;
    }
    out.cpu = static_cast<std::byte *>(mapped_) + cursor;
    out.buffer = buffer_;
    out.offset = cursor;
    head_ = cursor + need;
    return out;
}

} // namespace rhi
