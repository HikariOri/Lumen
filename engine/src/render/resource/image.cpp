/**
 * @file image.cpp
 * @brief Image 实现
 */

#include "render/resource/image.hpp"
#include "render/context.hpp"
#include "render/resource/buffer.hpp"

#include <stb_image.h>

#include <cmath>
#include <limits>

namespace lumen::render {

    namespace {

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

        uint32_t calculate_mip_levels(uint32_t width, uint32_t height) {
            return static_cast<uint32_t>(
                       std::floor(std::log2(std::max(width, height)))) +
                   1;
        }

    } // namespace

    bool Image::create(const Context &ctx, const ImageCreateInfo &info) {
        if (info.width == 0 || info.height == 0)
            return false;

        device_ = ctx.device();
        width_ = info.width;
        height_ = info.height;
        format_ = info.format;
        mipLevels_ = info.mipLevels;

        if (info.generateMipmaps) {
            mipLevels_ = calculate_mip_levels(info.width, info.height);
        }

        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = info.width;
        imageInfo.extent.height = info.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels_;
        imageInfo.arrayLayers = info.arrayLayers;
        imageInfo.format = info.format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = info.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (info.type == ImageType::TexCube) {
            imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &image_);
        if (result != VK_SUCCESS)
            return false;

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device_, image_, &memReqs);

        VkMemoryAllocateInfo allocInfo {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
        };
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex =
            find_memory_type(ctx.physical_device(), memReqs.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = vkAllocateMemory(device_, &allocInfo, nullptr, &memory_);
        if (result != VK_SUCCESS) {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
            return false;
        }

        vkBindImageMemory(device_, image_, memory_, 0);

        VkImageViewCreateInfo viewInfo {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        viewInfo.image = image_;
        viewInfo.viewType = info.type == ImageType::TexCube
                                ? VK_IMAGE_VIEW_TYPE_CUBE
                                : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.format;
        viewInfo.subresourceRange.aspectMask = info.aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels_;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = info.arrayLayers;

        result = vkCreateImageView(device_, &viewInfo, nullptr, &imageView_);
        if (result != VK_SUCCESS) {
            destroy_();
            return false;
        }
        return true;
    }

    bool Image::create_from_file(const Context &ctx, const char *filePath) {
        stbi_set_flip_vertically_on_load(1); // Vulkan 纹理 (0,0)=左下
        int w { 0 }, h { 0 }, channels { 0 };
        stbi_uc *pixels =
            stbi_load(filePath, &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels)
            return false;

        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        size_t imageSize = static_cast<size_t>(w) * h * 4;

        Buffer staging;
        BufferCreateInfo stagingInfo { imageSize, BufferUsage::Staging, true };
        if (!staging.create(ctx, stagingInfo)) {
            stbi_image_free(pixels);
            return false;
        }
        staging.upload(pixels, imageSize);
        stbi_image_free(pixels);

        ImageCreateInfo info {};
        info.width = static_cast<uint32_t>(w);
        info.height = static_cast<uint32_t>(h);
        info.format = format;
        info.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.generateMipmaps = true;
        if (!create(ctx, info))
            return false;

        // 注：实际项目中需要 CommandBuffer 执行 layout 转换和 copy，
        // 此处简化，仅创建 Image；完整实现需配合 CommandBuffer 与 Staging。
        (void)staging; // 待实现 staging 上传
        return true;
    }

    bool Image::create_depth_attachment(const Context &ctx, uint32_t width,
                                        uint32_t height) {
        ImageCreateInfo info {};
        info.width = width;
        info.height = height;
        info.format = VK_FORMAT_D32_SFLOAT;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        return create(ctx, info);
    }

    void Image::destroy_() {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, memory_, nullptr);
            memory_ = VK_NULL_HANDLE;
        }
    }

    Image::~Image() { destroy_(); }

    Image::Image(Image &&other) noexcept
        : device_ { other.device_ }, image_ { other.image_ },
          memory_ { other.memory_ }, imageView_ { other.imageView_ },
          format_ { other.format_ }, width_ { other.width_ },
          height_ { other.height_ }, mipLevels_ { other.mipLevels_ } {
        other.device_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
    }

    Image &Image::operator=(Image &&other) noexcept {
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        image_ = other.image_;
        memory_ = other.memory_;
        imageView_ = other.imageView_;
        format_ = other.format_;
        width_ = other.width_;
        height_ = other.height_;
        mipLevels_ = other.mipLevels_;
        other.device_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.imageView_ = VK_NULL_HANDLE;
        return *this;
    }

} // namespace lumen::render
