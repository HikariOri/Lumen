/**
 * @file image.cpp
 * @brief `vulkan::Image` 与 view 辅助函数实现。
 */

#include "vulkan/image.hpp"

namespace vulkan {

namespace {

[[nodiscard]] std::expected<VkImageView, std::string>
create_image_view_2d(const VkDevice device, const VkImage image,
                      const VkFormat format,
                      const VkImageAspectFlags aspectMask,
                      const std::uint32_t mipLevelCount,
                      const std::uint32_t baseMipLevel,
                      const std::uint32_t layerCount,
                      const std::uint32_t baseArrayLayer) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("Image::create: null device for image view"));
    }
    if (image == VK_NULL_HANDLE) {
        return std::unexpected(std::string("Image::create: null image for view"));
    }

    VkImageViewCreateInfo viewInfo {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
    viewInfo.subresourceRange.levelCount = mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView view { VK_NULL_HANDLE };
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return std::unexpected(
            std::string("Image::create: vkCreateImageView failed"));
    }
    return view;
}

} // namespace

Image::Image(const VmaAllocator allocator, const VkDevice viewDevice,
             const VkImage img, const VkImageView imageView,
             const VmaAllocation allocation, const VkExtent3D extent,
             const VkFormat format, const std::uint32_t mipLevels,
             const std::uint32_t arrayLayers)
    : vmaAllocator_(allocator), vkViewDevice_(viewDevice), vkImage_(img),
      vkImageView_(imageView), vmaAllocation_(allocation), extent_(extent),
      format_(format), mipLevels_(mipLevels), arrayLayers_(arrayLayers) {}

void Image::destroy() noexcept {
    if (vkViewDevice_ != VK_NULL_HANDLE && vkImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vkViewDevice_, vkImageView_, nullptr);
    }
    vkViewDevice_ = VK_NULL_HANDLE;
    vkImageView_ = VK_NULL_HANDLE;
    if (vmaAllocator_ != VK_NULL_HANDLE && vkImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(vmaAllocator_, vkImage_, vmaAllocation_);
    }
    vmaAllocator_ = VK_NULL_HANDLE;
    vkImage_ = VK_NULL_HANDLE;
    vmaAllocation_ = VK_NULL_HANDLE;
    extent_ = { 1, 1, 1 };
    format_ = VK_FORMAT_UNDEFINED;
    mipLevels_ = 1;
    arrayLayers_ = 1;
}

Image::~Image() {
    destroy();
}

Image::Image(Image &&other) noexcept
    : vmaAllocator_(other.vmaAllocator_), vkViewDevice_(other.vkViewDevice_),
      vkImage_(other.vkImage_), vkImageView_(other.vkImageView_),
      vmaAllocation_(other.vmaAllocation_), extent_(other.extent_),
      format_(other.format_), mipLevels_(other.mipLevels_),
      arrayLayers_(other.arrayLayers_) {
    other.vmaAllocator_ = VK_NULL_HANDLE;
    other.vkViewDevice_ = VK_NULL_HANDLE;
    other.vkImage_ = VK_NULL_HANDLE;
    other.vkImageView_ = VK_NULL_HANDLE;
    other.vmaAllocation_ = VK_NULL_HANDLE;
    other.extent_ = { 1, 1, 1 };
    other.format_ = VK_FORMAT_UNDEFINED;
    other.mipLevels_ = 1;
    other.arrayLayers_ = 1;
}

Image &Image::operator=(Image &&other) noexcept {
    if (this != &other) {
        destroy();
        vmaAllocator_ = other.vmaAllocator_;
        vkViewDevice_ = other.vkViewDevice_;
        vkImage_ = other.vkImage_;
        vkImageView_ = other.vkImageView_;
        vmaAllocation_ = other.vmaAllocation_;
        extent_ = other.extent_;
        format_ = other.format_;
        mipLevels_ = other.mipLevels_;
        arrayLayers_ = other.arrayLayers_;
        other.vmaAllocator_ = VK_NULL_HANDLE;
        other.vkViewDevice_ = VK_NULL_HANDLE;
        other.vkImage_ = VK_NULL_HANDLE;
        other.vkImageView_ = VK_NULL_HANDLE;
        other.vmaAllocation_ = VK_NULL_HANDLE;
        other.extent_ = { 1, 1, 1 };
        other.format_ = VK_FORMAT_UNDEFINED;
        other.mipLevels_ = 1;
        other.arrayLayers_ = 1;
    }
    return *this;
}

std::expected<Image, std::string>
Image::create(const VmaAllocator allocator, const ImageCreateInfo &info) {
    if (allocator == VK_NULL_HANDLE) {
        return std::unexpected(std::string("Image::create: null allocator"));
    }
    if (info.format == VK_FORMAT_UNDEFINED) {
        return std::unexpected(
            std::string("Image::create: format is UNDEFINED"));
    }
    if (info.usage == 0U) {
        return std::unexpected(std::string("Image::create: usage is 0"));
    }
    if (info.extent.width == 0U || info.extent.height == 0U) {
        return std::unexpected(
            std::string("Image::create: extent width/height is 0"));
    }
    if (info.extent.depth != 1U) {
        return std::unexpected(
            std::string("Image::create: only 2D images (extent.depth must be 1)"));
    }
    if (info.mipLevels == 0U || info.arrayLayers == 0U) {
        return std::unexpected(
            std::string("Image::create: mipLevels or arrayLayers is 0"));
    }

    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = info.extent;
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.format = info.format;
    imageInfo.tiling = info.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = info.usage;
    imageInfo.samples = info.samples;
    imageInfo.sharingMode = info.sharingMode;

    VmaAllocationCreateInfo allocInfo {};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.allocationFlags;

    VkImage vkImg { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    const VkResult result = vmaCreateImage(allocator, &imageInfo, &allocInfo,
                                           &vkImg, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        return std::unexpected(
            std::string("Image::create: vmaCreateImage failed ec=") +
            std::to_string(static_cast<int>(result)));
    }

    VkDevice viewDev { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    if (info.viewDevice != VK_NULL_HANDLE) {
        if (info.viewAspectMask == 0U) {
            vmaDestroyImage(allocator, vkImg, allocation);
            return std::unexpected(std::string(
                "Image::create: viewDevice set but viewAspectMask is 0"));
        }
        const std::uint32_t mipCount =
            info.viewMipLevelCount != 0U ? info.viewMipLevelCount
                                         : info.mipLevels;
        const std::uint32_t layerCount =
            info.viewLayerCount != 0U ? info.viewLayerCount : info.arrayLayers;
        auto viewResult =
            create_image_view_2d(info.viewDevice, vkImg, info.format,
                                 info.viewAspectMask, mipCount,
                                 info.viewBaseMipLevel, layerCount,
                                 info.viewBaseArrayLayer);
        if (!viewResult) {
            vmaDestroyImage(allocator, vkImg, allocation);
            return std::unexpected(viewResult.error());
        }
        viewDev = info.viewDevice;
        view = viewResult.value();
    }

    return Image(allocator, viewDev, vkImg, view, allocation, info.extent,
                 info.format, info.mipLevels, info.arrayLayers);
}

} // namespace vulkan
