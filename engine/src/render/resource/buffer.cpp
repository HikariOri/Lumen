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
#include "render/command_buffer.hpp"
#include "render/context.hpp"

#include <cstring>

namespace lumen::render {

bool Buffer::create(const Context &ctx, const BufferCreateInfo &createInfo) {
    if (createInfo.size == 0) {
        return false;
    }

    if (createInfo.persistentlyMapped && !createInfo.hostVisible) {
        LUMEN_LOG_ERROR(
            "Buffer 创建失败: persistentlyMapped 需要 hostVisible=true");
        return false;
    }

    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        LUMEN_LOG_ERROR("Buffer 创建失败: VMA 未初始化");
        return false;
    }

    if (is_valid()) {
        destroy_();
    }

    device_ = ctx.device();
    vmaAllocator = vma;
    byteSize = createInfo.size;
    persistentMappedBase = nullptr;

    vk::BufferCreateInfo bufferInfo {};
    bufferInfo.size = createInfo.size;
    bufferInfo.usage = createInfo.usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    if (createInfo.hostVisible) {
        allocCreate.flags = createInfo.hostRandomAccess
                                ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    if (createInfo.persistentlyMapped) {
        allocCreate.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer vk_buf {};
    VkResult result = vmaCreateBuffer(
        vmaAllocator,
        reinterpret_cast<const VkBufferCreateInfo *>(&bufferInfo),
        &allocCreate, &vk_buf, &vmaAllocation, nullptr);

    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Buffer 创建失败: {} size={}", static_cast<int>(result),
                        createInfo.size);

        device_ = nullptr;
        vmaAllocator = nullptr;
        buffer_ = nullptr;
        vmaAllocation = nullptr;
        byteSize = 0;
        return false;
    }
    buffer_ = vk_buf;

    if (createInfo.persistentlyMapped) {
        VmaAllocationInfo ai {};
        vmaGetAllocationInfo(vmaAllocator, vmaAllocation, &ai);
        persistentMappedBase = ai.pMappedData;
        if (persistentMappedBase == nullptr) {
            LUMEN_LOG_ERROR("Buffer 创建失败: 持久映射未返回 pMappedData");
            vmaDestroyBuffer(vmaAllocator, static_cast<VkBuffer>(buffer_),
                             vmaAllocation);
            device_ = nullptr;
            vmaAllocator = nullptr;
            buffer_ = nullptr;
            vmaAllocation = nullptr;
            byteSize = 0;
            return false;
        }
    }

    LUMEN_LOG_DEBUG("Buffer 创建成功 size={} hostVisible={} persistent={}",
                    createInfo.size, createInfo.hostVisible,
                    createInfo.persistentlyMapped);

    return true;
}

void Buffer::upload(const void *srcBytes, size_t byteCount, size_t byteOffset) {
    if (!srcBytes || byteCount == 0 || vmaAllocation == nullptr) {
        return;
    }

    if (byteOffset > byteSize || byteCount > byteSize - byteOffset) {
        LUMEN_LOG_ERROR("Buffer::upload 越界: offset={} size={} buffer_size={}",
                        byteOffset, byteCount, byteSize);
        return;
    }

    void *ptr = nullptr;
    if (persistentMappedBase != nullptr) {
        ptr = persistentMappedBase;
    } else {
        ptr = map();
        if (!ptr) {
            return;
        }
    }

    memcpy(static_cast<char *>(ptr) + byteOffset, srcBytes, byteCount);

    const VkResult flushResult = vmaFlushAllocation(
        vmaAllocator, vmaAllocation, static_cast<VkDeviceSize>(byteOffset),
        static_cast<VkDeviceSize>(byteCount));
    if (flushResult != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Buffer::upload vmaFlushAllocation 失败: {}",
                        static_cast<int>(flushResult));
    }

    if (persistentMappedBase == nullptr) {
        unmap();
    }
}

void *Buffer::map() {
    if (vmaAllocation == nullptr || vmaAllocator == nullptr) {
        return nullptr;
    }

    if (persistentMappedBase != nullptr) {
        return persistentMappedBase;
    }

    void *ptr { nullptr };
    VkResult result = vmaMapMemory(vmaAllocator, vmaAllocation, &ptr);

    return result == VK_SUCCESS ? ptr : nullptr;
}

void Buffer::unmap() {
    if (persistentMappedBase != nullptr) {
        return;
    }
    if (vmaAllocation != nullptr && vmaAllocator != nullptr) {
        vmaUnmapMemory(vmaAllocator, vmaAllocation);
    }
}

void Buffer::invalidate_mapped_range(const size_t byte_offset,
                                     const size_t byte_count) {
    if (vmaAllocation == nullptr || vmaAllocator == nullptr ||
        byte_count == 0) {
        return;
    }
    const VkResult r = vmaInvalidateAllocation(
        vmaAllocator, vmaAllocation, static_cast<VkDeviceSize>(byte_offset),
        static_cast<VkDeviceSize>(byte_count));
    if (r != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Buffer::invalidate_mapped_range 失败: {}",
                        static_cast<int>(r));
    }
}

void Buffer::destroy_() {
    persistentMappedBase = nullptr;

    if (buffer_ && vmaAllocator != nullptr && vmaAllocation != nullptr) {

        vmaDestroyBuffer(vmaAllocator, static_cast<VkBuffer>(buffer_),
                         vmaAllocation);

        buffer_ = nullptr;
        vmaAllocation = nullptr;
    }

    vmaAllocator = nullptr;
    device_ = nullptr;
    byteSize = 0;
}

Buffer::~Buffer() { destroy_(); }

Buffer::Buffer(Buffer &&rhs) noexcept
    : device_ { rhs.device_ }, vmaAllocator { rhs.vmaAllocator },
      buffer_ { rhs.buffer_ }, vmaAllocation { rhs.vmaAllocation },
      byteSize { rhs.byteSize },
      persistentMappedBase { rhs.persistentMappedBase } {

    rhs.device_ = nullptr;
    rhs.vmaAllocator = nullptr;
    rhs.buffer_ = nullptr;
    rhs.vmaAllocation = nullptr;
    rhs.byteSize = 0;
    rhs.persistentMappedBase = nullptr;
}

Buffer &Buffer::operator=(Buffer &&rhs) noexcept {
    if (this == &rhs) {
        return *this;
    }

    destroy_();

    device_ = rhs.device_;
    vmaAllocator = rhs.vmaAllocator;
    buffer_ = rhs.buffer_;
    vmaAllocation = rhs.vmaAllocation;
    byteSize = rhs.byteSize;
    persistentMappedBase = rhs.persistentMappedBase;

    rhs.device_ = nullptr;
    rhs.vmaAllocator = nullptr;
    rhs.buffer_ = nullptr;
    rhs.vmaAllocation = nullptr;
    rhs.byteSize = 0;
    rhs.persistentMappedBase = nullptr;

    return *this;
}

bool Buffer::create_device_local_upload_impl(
    const Context &ctx, vk::Queue transferQueue, CommandPool &cmdPool,
    vk::BufferUsageFlags usage, const void *srcBytes, size_t byteCount) {
    if (!srcBytes || byteCount == 0) {
        return false;
    }

    const BufferCreateInfo createInfo {
        .size = byteCount,
        .usage = usage | vk::BufferUsageFlagBits::eTransferDst,
        .hostVisible = false,
        .persistentlyMapped = false,
    };
    if (!create(ctx, createInfo)) {
        return false;
    }

    StagingBuffer staging;
    if (!staging.create(ctx, byteCount)) {
        destroy_();
        return false;
    }
    staging.upload(srcBytes, byteCount);

    const bool ok =
        cmdPool.submit_one_shot(transferQueue, [&](vk::CommandBuffer cmd) {
            vk::BufferCopy region {};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = static_cast<vk::DeviceSize>(byteCount);
            cmd.copyBuffer(staging.handle(), handle(), 1, &region);
        });

    if (!ok) {
        destroy_();
        return false;
    }
    return true;
}

bool VertexBuffer::create(const Context &ctx, size_t byteCount,
                          bool hostVisible) {
    return Buffer::create(
        ctx, { .size = byteCount,
               .usage = vk::BufferUsageFlagBits::eVertexBuffer,
               .hostVisible = hostVisible,
               .persistentlyMapped = false });
}

bool VertexBuffer::create_device_local_and_upload(const Context &ctx,
                                                  vk::Queue transferQueue,
                                                  CommandPool &cmdPool,
                                                  const void *srcBytes,
                                                  size_t byteCount) {
    return create_device_local_upload_impl(
        ctx, transferQueue, cmdPool, vk::BufferUsageFlagBits::eVertexBuffer,
        srcBytes, byteCount);
}

bool IndexBuffer::create(const Context &ctx, size_t byteCount,
                         bool hostVisible) {
    return Buffer::create(
        ctx, { .size = byteCount,
               .usage = vk::BufferUsageFlagBits::eIndexBuffer,
               .hostVisible = hostVisible,
               .persistentlyMapped = false });
}

bool IndexBuffer::create_device_local_and_upload(const Context &ctx,
                                                 vk::Queue transferQueue,
                                                 CommandPool &cmdPool,
                                                 const void *srcBytes,
                                                 size_t byteCount) {
    return create_device_local_upload_impl(
        ctx, transferQueue, cmdPool, vk::BufferUsageFlagBits::eIndexBuffer,
        srcBytes, byteCount);
}

bool UniformBuffer::create(const Context &ctx, size_t byteCount,
                           bool hostVisible) {
    return Buffer::create(
        ctx, { .size = byteCount,
               .usage = vk::BufferUsageFlagBits::eUniformBuffer,
               .hostVisible = hostVisible,
               .persistentlyMapped = false });
}

bool UniformBuffer::create_persistent(const Context &ctx, size_t byteCount) {
    return Buffer::create(
        ctx, { .size = byteCount,
               .usage = vk::BufferUsageFlagBits::eUniformBuffer,
               .hostVisible = true,
               .persistentlyMapped = true });
}

void UniformBuffer::update(const void *srcBytes, size_t byteCount,
                           size_t byteOffset) {
    upload(srcBytes, byteCount, byteOffset);
}

bool StagingBuffer::create(const Context &ctx, size_t byteCount) {
    return Buffer::create(
        ctx, { .size = byteCount,
               .usage = vk::BufferUsageFlagBits::eTransferSrc,
               .hostVisible = true,
               .persistentlyMapped = false });
}

} // namespace lumen::render
