/**
 * @file buffer.cpp
 * @brief Buffer 实现
 */

#include "render/resource/buffer.hpp"
#include "render/context.hpp"

#include <cstring>

namespace lumen::render {

    namespace {

        VkBufferUsageFlags to_usage_flags(BufferUsage usage) {
            switch (usage) {
            case BufferUsage::Vertex: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            case BufferUsage::Index: return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            case BufferUsage::Uniform:
                return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            case BufferUsage::Storage:
                return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            case BufferUsage::Staging: return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            case BufferUsage::TransferSrc:
                return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            case BufferUsage::TransferDst:
                return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            default: return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
        }

        uint32_t find_memory_type(VkPhysicalDevice physical,
                                  uint32_t typeFilter,
                                  VkMemoryPropertyFlags props) {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(physical, &memProps);

            for (uint32_t i { 0 }; i < memProps.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & props) == props) {
                    return i;
                }
            }
            return UINT32_MAX;
        }

    } // namespace

    bool Buffer::create(const Context &ctx, const BufferCreateInfo &info) {
        if (info.size == 0)
            return false;

        device_ = ctx.device();
        size_ = info.size;

        VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = info.size;
        bufferInfo.usage = to_usage_flags(info.usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result =
            vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer_);
        if (result != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(device_, buffer_, &memReqs);

        VkMemoryAllocateInfo allocInfo {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = find_memory_type(
            ctx.physical_device(), memReqs.memoryTypeBits,
            info.hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = vkAllocateMemory(device_, &allocInfo, nullptr, &memory_);
        if (result != VK_SUCCESS) {
            vkDestroyBuffer(device_, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(device_, buffer_, memory_, 0);
        return true;
    }

    void Buffer::upload(const void *data, size_t size, size_t offset) {
        if (!data || size == 0 || memory_ == VK_NULL_HANDLE)
            return;
        void *ptr = map();
        if (!ptr)
            return;
        memcpy(static_cast<char *>(ptr) + offset, data, size);
        unmap();
    }

    void *Buffer::map() {
        if (memory_ == VK_NULL_HANDLE)
            return nullptr;
        void *ptr { nullptr };
        VkResult result = vkMapMemory(device_, memory_, 0, size_, 0, &ptr);
        return result == VK_SUCCESS ? ptr : nullptr;
    }

    void Buffer::unmap() {
        if (memory_ != VK_NULL_HANDLE) {
            vkUnmapMemory(device_, memory_);
        }
    }

    void Buffer::destroy_() {
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer_, nullptr);
            buffer_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
        size_ = 0;
    }

    Buffer::~Buffer() { destroy_(); }

    Buffer::Buffer(Buffer &&other) noexcept
        : device_ { other.device_ }, buffer_ { other.buffer_ },
          memory_ { other.memory_ }, size_ { other.size_ } {
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
    }

    Buffer &Buffer::operator=(Buffer &&other) noexcept {
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
        return *this;
    }

} // namespace lumen::render
