/**
 * @FileName       : buffer.cpp
 * @Author         : yaojie
 * @Date           : 2026/4/11
 */

#include "vulkan/buffer.hpp"
#include "core/log/logger.hpp"

namespace vulkan {

UploadContext::UploadContext(VkDevice dev, VkQueue q,
                             std::uint32_t graphicsQueueFamily)
    : device(dev), queue(q) {
    VkFenceCreateInfo fenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);

    VkCommandPoolCreateInfo commandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily,
    };
    vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer);
}

UploadContext::UploadContext(VkDevice device, VkQueue queue,
                             VkCommandPool sharedCommandPool)
    : device(device), queue(queue), commandPool(sharedCommandPool),
      ownsCommandPool(false) {
    VkFenceCreateInfo fenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer);
}

void UploadContext::destroy() {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }
    if (ownsCommandPool) {
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
    } else {
        if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
    }
    commandPool = VK_NULL_HANDLE;
    commandBuffer = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
}

namespace {
static VkBufferUsageFlags toVkUsage(BufferUsage usage) {
    switch (usage) {
    case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::Storage: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case BufferUsage::UniformDynamic: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    case BufferUsage::StorageDynamic: return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    case BufferUsage::UploadStaging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    default: return 0;
    }
}

static VmaMemoryUsage toVmaUsage(MemoryMode mode) {
    switch (mode) {
    case MemoryMode::CPU_TO_GPU: return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case MemoryMode::GPU_ONLY: return VMA_MEMORY_USAGE_GPU_ONLY;
    case MemoryMode::PERSISTENT_MAP: return VMA_MEMORY_USAGE_CPU_TO_GPU;
    default: return VMA_MEMORY_USAGE_CPU_TO_GPU;
    }
}
} // namespace

// 创建缓冲
void Buffer::init(VmaAllocator allocator, VkDeviceSize size, BufferUsage usage,
                  MemoryMode memoryMode) {
    this->allocator_ = allocator;
    this->size_ = size;
    this->memoryMode_ = memoryMode;

    auto usageFlags = toVkUsage(usage);
    if (memoryMode == MemoryMode::GPU_ONLY) {
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; // ✅ 必须加！
    }

    VkBufferCreateInfo bi {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo ai {
        .usage = toVmaUsage(memoryMode),
    };

    if (memoryMode == MemoryMode::PERSISTENT_MAP) {
        ai.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    if (vmaCreateBuffer(allocator_, &bi, &ai, &buffer_, &allaction_, nullptr) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create buffer");
        return;
    }

    if (memoryMode == MemoryMode::PERSISTENT_MAP) {
        vmaMapMemory(allocator_, allaction_, &mapped_);
    }
}

// 上传数据
void Buffer::upload(const void *data, VkDeviceSize size, VkDeviceSize offset) {
    void *mapped {};
    vmaMapMemory(allocator_, allaction_, &mapped);
    memcpy((char *)mapped + offset, data, size);
    vmaUnmapMemory(allocator_, allaction_);
}

// 绑定（Vertex / Index 专用）
void Buffer::bind_vertex(VkCommandBuffer cmd, uint32_t binding,
                         VkDeviceSize offset) const {
    VkDeviceSize offs = offset;
    vkCmdBindVertexBuffers(cmd, binding, 1, &buffer_, &offs);
}

void Buffer::bind_index(VkCommandBuffer cmd, VkIndexType indexType,
                        VkDeviceSize offset) const {
    vkCmdBindIndexBuffer(cmd, buffer_, offset, indexType);
}

void Buffer::copy_to_mapped(const void *data, VkDeviceSize size,
                            VkDeviceSize offset) {
    if (mapped_ == nullptr) {
        LUMEN_LOG_ERROR("Buffer is not mapped");
        return;
    }
    memcpy((char *)mapped_ + offset, data, size);
}

void uploadToGPU(const UploadContext &ctx, Buffer &dst, const void *data,
                 VkDeviceSize size) {
     // 1. 创建 staging
     Buffer staging;
     staging.init(dst.allocator(), size, BufferUsage::UploadStaging, MemoryMode::CPU_TO_GPU);
 
     // 2. 写入 staging
     void *mapped;
     vmaMapMemory(staging.allocator(), staging.allocation(), &mapped);
     memcpy(mapped, data, size);
     vmaUnmapMemory(staging.allocator(), staging.allocation());
 
     // -------------------------------------------------------------------------
     // 正确做法：重用外部 command buffer
     // -------------------------------------------------------------------------
     // 必须重置！！
     vkResetCommandBuffer(ctx.commandBuffer, 0);
 
     VkCommandBufferBeginInfo begin{
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
     };
     vkBeginCommandBuffer(ctx.commandBuffer, &begin);
 
     // 拷贝
     VkBufferCopy copy{.size = size};
     vkCmdCopyBuffer(ctx.commandBuffer, staging.buffer(), dst.buffer(), 1, &copy);
 
     vkEndCommandBuffer(ctx.commandBuffer);
 
     // -------------------------------------------------------------------------
     // 提交并等待完成
     // -------------------------------------------------------------------------
     VkSubmitInfo submit{
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &ctx.commandBuffer,
     };
 
     vkResetFences(ctx.device, 1, &ctx.fence);
     vkQueueSubmit(ctx.queue, 1, &submit, ctx.fence);
     vkWaitForFences(ctx.device, 1, &ctx.fence, VK_TRUE, UINT64_MAX);
 
     // -------------------------------------------------------------------------
     // 清理 staging（安全，因为GPU已经完成拷贝）
     // -------------------------------------------------------------------------
     staging.destroy();
}

// 销毁
void Buffer::destroy() {

    if (!buffer_)
        return;

    // ✅ 销毁前自动 unmap（安全）
    if (mapped_) {
        vmaUnmapMemory(allocator_, allaction_);
        mapped_ = nullptr;
    }

    vmaDestroyBuffer(allocator_, buffer_, allaction_);
    buffer_ = VK_NULL_HANDLE;
    allaction_ = VK_NULL_HANDLE;
}

void DynamicRingBuffer::init(VmaAllocator allocator, VkDeviceSize perFrameSize,
                             uint32_t frameCount, uint32_t minAlignment) {
    this->frameSize = (perFrameSize + minAlignment - 1) & ~(minAlignment - 1);
    this->frameCount = frameCount;
    this->alignment = minAlignment;
    this->totalSize = this->frameSize * frameCount;

    buffer.init(allocator, totalSize, BufferUsage::UniformDynamic,
                MemoryMode::PERSISTENT_MAP);
}

void DynamicRingBuffer::destroy() { buffer.destroy(); }

void *DynamicRingBuffer::getMappedFrame(uint32_t frameIndex,
                                        VkDeviceSize &outBufferOffset) {
    outBufferOffset = frameSize * frameIndex;
    return (char *)buffer.mapped() + outBufferOffset;
}

// -----------------------------------------------------------------------------
// Buffer 池
// -----------------------------------------------------------------------------
Buffer BufferPool::getBuffer(size_t size, BufferUsage usage, MemoryMode mode) {
    auto &vec = pool[size];
    if (!vec.empty()) {
        auto buf = std::move(vec.back());
        vec.pop_back();
        return buf;
    }

    Buffer buf;
    buf.init(allocator, size, usage, mode);
    return buf;
}

void BufferPool::returnBuffer(Buffer &&buf) {
    pool[buf.size()].push_back(std::move(buf));
}

void BufferPool::clear() {
    for (auto &[k, vec] : pool) {
        for (auto &b : vec)
            b.destroy();
    }
    pool.clear();
}

} // namespace vulkan
