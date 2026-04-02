/**
 * @file image.cpp
 * @brief Image 实现
 *
 * @details
 * 实现 Vulkan Image 的创建、内存分配（VMA）、ImageView 创建、
 * 以及基础纹理加载逻辑。
 *
 * @note
 * `create_from_file` 通过 `CommandPool::submit_one_shot` 完成上传与 mip 生成。
 */

#include "render/resource/image.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/detail/gpu_image_mipmap.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cstdint>

namespace lumen::render {

bool Image::create(const Context &ctx, const ImageCreateInfo &info) {
    if (info.width == 0 || info.height == 0) {
        return false;
    }

    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        LUMEN_LOG_ERROR("Image 创建失败: VMA 未初始化");
        return false;
    }

    device_ = ctx.device();
    vma_allocator_ = vma;
    width_ = info.width;
    height_ = info.height;
    format_ = info.format;
    mipLevels_ = info.mipLevels;

    if (info.generateMipmaps) {
        mipLevels_ =
            gpu_image_mipmap::calculate_mip_levels(info.width, info.height);
    }

    vk::ImageCreateInfo imageInfo {};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent =
        vk::Extent3D { info.width, info.height, std::max(1U, info.depth) };
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.format = info.format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = info.usage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    if (info.type == ImageType::TexCube) {
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_img {};
    VkResult result = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_img, &allocation_, nullptr);

    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Image 创建失败: {} ({}x{})", static_cast<int>(result),
                        info.width, info.height);
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_img;

    vk::ImageViewCreateInfo viewInfo {};
    viewInfo.image = image_;
    viewInfo.viewType = info.type == ImageType::TexCube ? vk::ImageViewType::eCube
                                                        : vk::ImageViewType::e2D;
    viewInfo.format = info.format;
    viewInfo.subresourceRange.aspectMask = info.aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = info.arrayLayers;

    vk::ImageView view {};
    if (device_.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
        destroy_();
        return false;
    }
    imageView_ = view;

    return true;
}

bool Image::create_from_file(const Context &ctx, const char *filePath,
                             vk::Queue transferQueue, CommandPool &cmdPool) {
    stbi_set_flip_vertically_on_load(1);

    int w { 0 }, h { 0 }, channels { 0 };

    stbi_uc *pixels = stbi_load(filePath, &w, &h, &channels, STBI_rgb_alpha);

    if (!pixels) {
        return false;
    }

    if (w <= 0 || h <= 0) {
        stbi_image_free(pixels);
        return false;
    }

    const vk::Format format = vk::Format::eR8G8B8A8Srgb;
    const size_t imageSize =
        static_cast<size_t>(w) * static_cast<size_t>(h) * 4U;

    StagingBuffer staging;
    if (!staging.create(ctx, imageSize)) {
        stbi_image_free(pixels);
        return false;
    }

    staging.upload(pixels, imageSize);
    stbi_image_free(pixels);

    const uint32_t texW = static_cast<uint32_t>(w);
    const uint32_t texH = static_cast<uint32_t>(h);

    ImageCreateInfo create_info {};
    create_info.width = texW;
    create_info.height = texH;
    create_info.format = format;
    create_info.usage = vk::ImageUsageFlagBits::eTransferSrc |
                         vk::ImageUsageFlagBits::eTransferDst |
                         vk::ImageUsageFlagBits::eSampled;
    create_info.generateMipmaps = true;

    if (!create(ctx, create_info)) {
        return false;
    }

    const bool uploaded = cmdPool.submit_one_shot(
        transferQueue, [&](vk::CommandBuffer cmd) {
            const vk::CommandBuffer cb = cmd;
            gpu_image_mipmap::transition_image_layout(
                cb, image_, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, 0, mipLevels_);

            vk::BufferImageCopy region {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask =
                vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D { 0, 0, 0 };
            region.imageExtent = vk::Extent3D { texW, texH, 1 };

            cb.copyBufferToImage(staging.handle(), image_,
                                 vk::ImageLayout::eTransferDstOptimal, 1,
                                 &region);

            if (mipLevels_ > 1) {
                gpu_image_mipmap::generate_mipmaps(cb, image_, texW, texH,
                                                     mipLevels_);
            } else {
                gpu_image_mipmap::transition_image_layout(
                    cb, image_, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1);
            }
        });

    if (!uploaded) {
        destroy_();
        return false;
    }

    return true;
}

bool Image::create_depth_attachment(const Context &ctx, uint32_t width,
                                    uint32_t height) {
    ImageCreateInfo info {};
    info.width = width;
    info.height = height;
    info.format = vk::Format::eD32Sfloat;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    info.aspectMask = vk::ImageAspectFlagBits::eDepth;

    return create(ctx, info);
}

void Image::destroy_() {
    if (imageView_) {
        device_.destroyImageView(imageView_, nullptr);
        imageView_ = nullptr;
    }

    if (image_ && vma_allocator_ != nullptr && allocation_ != nullptr) {
        vmaDestroyImage(vma_allocator_, static_cast<VkImage>(image_),
                        allocation_);
        image_ = nullptr;
        allocation_ = nullptr;
    }

    vma_allocator_ = nullptr;
    device_ = nullptr;
}

Image::~Image() { destroy_(); }

Image::Image(Image &&other) noexcept
    : device_ { other.device_ }, vma_allocator_ { other.vma_allocator_ },
      image_ { other.image_ }, allocation_ { other.allocation_ },
      imageView_ { other.imageView_ }, format_ { other.format_ },
      width_ { other.width_ }, height_ { other.height_ },
      mipLevels_ { other.mipLevels_ } {

    other.device_ = nullptr;
    other.vma_allocator_ = nullptr;
    other.image_ = nullptr;
    other.allocation_ = nullptr;
    other.imageView_ = nullptr;
}

Image &Image::operator=(Image &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    vma_allocator_ = other.vma_allocator_;
    image_ = other.image_;
    allocation_ = other.allocation_;
    imageView_ = other.imageView_;
    format_ = other.format_;
    width_ = other.width_;
    height_ = other.height_;
    mipLevels_ = other.mipLevels_;

    other.device_ = nullptr;
    other.vma_allocator_ = nullptr;
    other.image_ = nullptr;
    other.allocation_ = nullptr;
    other.imageView_ = nullptr;

    return *this;
}

} // namespace lumen::render
