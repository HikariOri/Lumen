/**
 * @file buffer.hpp
 * @brief 基于 VMA 的缓冲区封装（`VkBuffer` + 分配）。
 */

#pragma once

#include <expected>
#include <string>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 创建参数（透传至 `VmaAllocationCreateInfo` 常用字段）。
 */
struct BufferCreateInfo {
    VkDeviceSize size { 0 };
    VkBufferUsageFlags usage { 0 };
    VmaMemoryUsage memoryUsage { VMA_MEMORY_USAGE_AUTO };
    VmaAllocationCreateFlags allocationFlags { 0 };
    VkSharingMode sharingMode { VK_SHARING_MODE_EXCLUSIVE };
};

/**
 * @brief 拥有 `VkBuffer` 与其 VMA 分配；可配合 `Context::allocator()` 使用。
 */
class Buffer final {
public:
    [[nodiscard]] static std::expected<Buffer, std::string>
    create(VmaAllocator allocator, const BufferCreateInfo &info);

    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;

    [[nodiscard]] VmaAllocator allocator() const noexcept {
        return vmaAllocator_;
    }
    [[nodiscard]] VkBuffer buffer() const noexcept { return vkBuffer_; }
    [[nodiscard]] VmaAllocation allocation() const noexcept {
        return vmaAllocation_;
    }
    [[nodiscard]] VkDeviceSize size() const noexcept { return byteSize_; }

    /// 是否已由 `create` 成功创建且未被移走。
    [[nodiscard]] bool is_valid() const noexcept {
        return vkBuffer_ != VK_NULL_HANDLE;
    }

    /// 映射主机可见内存；失败时返回错误说明。
    [[nodiscard]] std::expected<void *, std::string> map();
    void unmap();

private:
    Buffer(VmaAllocator allocator, VkBuffer buf, VmaAllocation allocation,
           VkDeviceSize size);

    void destroy() noexcept;

    VmaAllocator vmaAllocator_ { VK_NULL_HANDLE };
    VkBuffer vkBuffer_ { VK_NULL_HANDLE };
    VmaAllocation vmaAllocation_ { VK_NULL_HANDLE };
    VkDeviceSize byteSize_ { 0 };
};

} // namespace vulkan
