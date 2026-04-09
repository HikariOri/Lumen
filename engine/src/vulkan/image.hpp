/**
 * @file image.hpp
 * @brief 基于 VMA 的图像封装（`VkImage` + 分配），可选同时创建并持有 2D
 * `VkImageView`。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 创建参数（默认 2D、单 mip、单层、`UNDEFINED` 初始布局）。
 */
struct ImageCreateInfo {
    VkExtent3D extent { .width = 1, .height = 1, .depth = 1 };
    std::uint32_t mipLevels { 1 };
    std::uint32_t arrayLayers { 1 };
    VkFormat format { VK_FORMAT_UNDEFINED };
    VkImageUsageFlags usage { 0 };
    VkImageTiling tiling { VK_IMAGE_TILING_OPTIMAL };
    VkSampleCountFlagBits samples { VK_SAMPLE_COUNT_1_BIT };
    VmaMemoryUsage memoryUsage { VMA_MEMORY_USAGE_AUTO };
    VmaAllocationCreateFlags allocationFlags { 0 };
    VkSharingMode sharingMode { VK_SHARING_MODE_EXCLUSIVE };

    /// 非空时在创建图像后立刻建 2D view，并由 `Image` 析构时销毁。
    VkDevice viewDevice { VK_NULL_HANDLE };
    VkImageAspectFlags viewAspectMask { 0 };
    std::uint32_t viewBaseMipLevel { 0 };
    /// 0 表示使用 `mipLevels`。
    std::uint32_t viewMipLevelCount { 0 };
    std::uint32_t viewBaseArrayLayer { 0 };
    /// 0 表示使用 `arrayLayers`。
    std::uint32_t viewLayerCount { 0 };
};

/**
 * @brief 拥有 `VkImage` 与其 VMA 分配；可按 `ImageCreateInfo::viewDevice` 同时持有
 * `VkImageView`。
 */
class Image final {
public:
    [[nodiscard]] static std::expected<Image, std::string>
    create(VmaAllocator allocator, const ImageCreateInfo &info);

    Image() = default;
    ~Image();

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;

    [[nodiscard]] VmaAllocator allocator() const noexcept {
        return vmaAllocator_;
    }
    [[nodiscard]] VkImage image() const noexcept { return vkImage_; }
    [[nodiscard]] VmaAllocation allocation() const noexcept {
        return vmaAllocation_;
    }
    [[nodiscard]] VkExtent3D extent() const noexcept { return extent_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] std::uint32_t mip_levels() const noexcept {
        return mipLevels_;
    }
    [[nodiscard]] std::uint32_t array_layers() const noexcept {
        return arrayLayers_;
    }
    [[nodiscard]] VkImageView view() const noexcept { return vkImageView_; }

    /// 是否已由 `create` 成功创建且未被移走（与是否带 `VkImageView` 无关）。
    [[nodiscard]] bool is_valid() const noexcept {
        return vkImage_ != VK_NULL_HANDLE;
    }

    /**
     * @brief 录制整幅图像（单 mip、单层）的布局屏障；`aspect` 为 0 时按 `format_`
     * 推断颜色或深度/模板方面。
     */
    void record_layout_barrier(VkCommandBuffer cmd, VkImageLayout old_layout,
                               VkImageLayout new_layout,
                               VkPipelineStageFlags src_stage_mask,
                               VkPipelineStageFlags dst_stage_mask,
                               VkAccessFlags src_access_mask,
                               VkAccessFlags dst_access_mask,
                               VkImageAspectFlags aspect = 0) const;

private:
    Image(VmaAllocator allocator, VkDevice viewDevice, VkImage img,
          VkImageView imageView, VmaAllocation allocation, VkExtent3D extent,
          VkFormat format, std::uint32_t mipLevels, std::uint32_t arrayLayers);

    void destroy() noexcept;

    VmaAllocator vmaAllocator_ { VK_NULL_HANDLE };
    VkDevice vkViewDevice_ { VK_NULL_HANDLE };
    VkImage vkImage_ { VK_NULL_HANDLE };
    VkImageView vkImageView_ { VK_NULL_HANDLE };
    VmaAllocation vmaAllocation_ { VK_NULL_HANDLE };
    VkExtent3D extent_ { 1, 1, 1 };
    VkFormat format_ { VK_FORMAT_UNDEFINED };
    std::uint32_t mipLevels_ { 1 };
    std::uint32_t arrayLayers_ { 1 };
};

} // namespace vulkan
