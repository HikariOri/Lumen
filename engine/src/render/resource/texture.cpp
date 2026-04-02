/**
 * @file texture.cpp
 * @brief Texture 实现（上传 / Layout 转换 / Mipmap / Cubemap）
 */

#include "render/resource/texture.hpp"
#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/detail/gpu_image_mipmap.hpp"
#include "render/resource/image.hpp"
#include "render/resource/sampler.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace lumen::render {

namespace {

void generate_mipmaps_cube(vk::CommandBuffer cmd, vk::Image image, uint32_t dim,
                           uint32_t mipLevels) {
    for (uint32_t face = 0; face < 6; ++face) {
        auto mipWidth = static_cast<int32_t>(dim);
        auto mipHeight = static_cast<int32_t>(dim);
        for (uint32_t i = 1; i < mipLevels; ++i) {
            gpu_image_mipmap::transition_image_subresource(
                cmd, image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eTransferSrcOptimal, i - 1, 1, face, 1);
            gpu_image_mipmap::transition_image_subresource(
                cmd, image, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, i, 1, face, 1);

            vk::ImageBlit blit {};
            blit.srcOffsets[0] = vk::Offset3D { 0, 0, 0 };
            blit.srcOffsets[1] = vk::Offset3D { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = face;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = vk::Offset3D { 0, 0, 0 };
            blit.dstOffsets[1] = vk::Offset3D { std::max(1, mipWidth / 2),
                                                std::max(1, mipHeight / 2), 1 };
            blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = face;
            blit.dstSubresource.layerCount = 1;

            cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
                          vk::ImageLayout::eTransferDstOptimal, 1, &blit,
                          vk::Filter::eLinear);

            gpu_image_mipmap::transition_image_subresource(
                cmd, image, vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal, i - 1, 1, face, 1);

            mipWidth = std::max(1, mipWidth / 2);
            mipHeight = std::max(1, mipHeight / 2);
        }
        gpu_image_mipmap::transition_image_subresource(
            cmd, image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels - 1, 1, face,
            1);
    }
}

} // namespace

bool Texture::create_from_memory(const Context &ctx, const void *data,
                                 size_t imageSizeBytes, uint32_t width,
                                 uint32_t height, vk::Queue transferQueue,
                                 CommandPool &cmdPool, vk::Format format,
                                 const SamplerConfig &samplerConfig,
                                 bool generateMipmaps) {
    if (!data || imageSizeBytes == 0 || width == 0 || height == 0) {
        return false;
    }
    if (imageSizeBytes < static_cast<size_t>(width) * height) {
        return false;
    }

    if (!create_from_pixels_(ctx, data, imageSizeBytes, width, height, format,
                             transferQueue, cmdPool, generateMipmaps)) {
        return false;
    }
    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_from_file(const Context &ctx, const char *filePath,
                               vk::Queue transferQueue, CommandPool &cmdPool,
                               const SamplerConfig &samplerConfig,
                               vk::Format format) {
    stbi_set_flip_vertically_on_load(1);
    int w {};
    int h {};
    int channels {};
    stbi_uc *pixels = stbi_load(filePath, &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LUMEN_LOG_ERROR("纹理加载失败: {}", filePath);
        return false;
    }

    const auto texWidth = static_cast<unsigned>(w);
    const auto texHeight = static_cast<unsigned>(h);
    const auto imageSize = static_cast<size_t>(texWidth) * texHeight * 4;

    bool ok = create_from_pixels_(ctx, pixels, imageSize, texWidth, texHeight,
                                  format, transferQueue, cmdPool, true);
    stbi_image_free(pixels);

    if (!ok) {
        LUMEN_LOG_ERROR("纹理从像素创建失败: {}x{}", texWidth, texHeight);
        return false;
    }
    LUMEN_LOG_DEBUG("纹理加载成功: {} {}x{}", filePath, texWidth, texHeight);
    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_from_ktx_file(const Context &ctx, const char *filePath,
                                   vk::Queue transferQueue, CommandPool &cmdPool,
                                   vk::Format format,
                                   const SamplerConfig &samplerConfig) {
    std::vector<std::uint8_t> rgba;
    std::uint32_t texWidth = 0;
    std::uint32_t texHeight = 0;
    std::string kerr;
    if (!lumen::core::decode_ktx_file_to_rgba8(filePath, rgba, texWidth,
                                               texHeight, &kerr)) {
        LUMEN_LOG_ERROR("KTX 解码失败 {}: {}",
                        filePath != nullptr ? filePath : "", kerr);
        return false;
    }
    const size_t imageSize =
        static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4U;
    if (!create_from_pixels_(ctx, rgba.data(), imageSize, texWidth, texHeight,
                             format, transferQueue, cmdPool, true)) {
        LUMEN_LOG_ERROR("KTX 纹理上传失败: {}x{}", texWidth, texHeight);
        return false;
    }
    LUMEN_LOG_DEBUG("KTX 纹理加载成功: {} {}x{}", filePath, texWidth,
                    texHeight);
    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_from_pixels_(const Context &ctx, const void *data,
                                  size_t imageSizeBytes, uint32_t texWidth,
                                  uint32_t texHeight, vk::Format format,
                                  vk::Queue transferQueue, CommandPool &cmdPool,
                                  bool doMipmaps) {
    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        return false;
    }

    mipLevels_ =
        doMipmaps ? gpu_image_mipmap::calculate_mip_levels(texWidth, texHeight)
                  : 1U;
    format_ = format;
    width_ = texWidth;
    height_ = texHeight;
    device_ = ctx.device();
    vma_allocator_ = vma;

    StagingBuffer staging;
    if (!staging.create(ctx, imageSizeBytes)) {
        return false;
    }
    staging.upload(data, imageSizeBytes);

    vk::ImageCreateInfo imageInfo {};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D { texWidth, texHeight, 1 };
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_handle {};
    const VkResult vma_rc = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_handle, &allocation_, nullptr);
    if (static_cast<vk::Result>(vma_rc) != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("纹理 Image/VMA 创建失败: {} ({}x{})",
                        static_cast<int>(vma_rc), texWidth, texHeight);
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_handle;

    vk::ImageViewCreateInfo viewInfo {};
    viewInfo.image = image_;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vk::ImageView view {};
    if (device_.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
        destroy_();
        return false;
    }
    imageView_ = view;

    if (!cmdPool.submit_one_shot(transferQueue, [&](vk::CommandBuffer cmd) {
            gpu_image_mipmap::transition_image_layout(
                cmd, image_, vk::ImageLayout::eUndefined,
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
            region.imageExtent = vk::Extent3D { texWidth, texHeight, 1 };

            cmd.copyBufferToImage(staging.handle(), image_,
                                  vk::ImageLayout::eTransferDstOptimal, 1,
                                  &region);

            if (mipLevels_ > 1) {
                gpu_image_mipmap::generate_mipmaps(cmd, image_, texWidth,
                                                     texHeight, mipLevels_);
            } else {
                gpu_image_mipmap::transition_image_layout(
                    cmd, image_, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1);
            }
        })) {
        destroy_();
        return false;
    }

    return true;
}

bool Texture::create(const Context &ctx, const ImageCreateInfo &info,
                     const SamplerConfig &samplerConfig) {
    if (info.width == 0 || info.height == 0) {
        return false;
    }

    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
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
    imageInfo.usage = info.usage | vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    if (info.type == ImageType::TexCube) {
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    }

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_handle {};
    const VkResult vma_rc = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_handle, &allocation_, nullptr);
    if (static_cast<vk::Result>(vma_rc) != vk::Result::eSuccess) {
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_handle;

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

    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_cubemap_from_rgba8_faces(
    const Context &ctx, const void *const faces[6], uint32_t faceSize,
    vk::Queue transferQueue, CommandPool &cmdPool,
    const SamplerConfig &samplerConfig, vk::Format format) {
    for (uint32_t i = 0; i < 6; ++i) {
        if (faces[i] == nullptr) {
            return false;
        }
    }
    if (faceSize == 0) {
        return false;
    }
    if (format != vk::Format::eR8G8B8A8Srgb &&
        format != vk::Format::eR8G8B8A8Unorm) {
        return false;
    }

    destroy_();

    const size_t face_bytes = static_cast<size_t>(faceSize) * faceSize * 4U;
    const size_t total_bytes = face_bytes * 6U;
    mipLevels_ = gpu_image_mipmap::calculate_mip_levels(faceSize, faceSize);
    format_ = format;
    width_ = faceSize;
    height_ = faceSize;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = nullptr;
        return false;
    }

    StagingBuffer staging;
    if (!staging.create(ctx, total_bytes)) {
        return false;
    }
    std::vector<uint8_t> concat(total_bytes);
    for (uint32_t f = 0; f < 6; ++f) {
        std::memcpy(concat.data() + f * face_bytes, faces[f], face_bytes);
    }
    staging.upload(concat.data(), total_bytes);

    vk::ImageCreateInfo imageInfo {};
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D { faceSize, faceSize, 1 };
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_handle {};
    const VkResult vma_rc = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_handle, &allocation_, nullptr);
    if (static_cast<vk::Result>(vma_rc) != vk::Result::eSuccess) {
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_handle;

    vk::ImageViewCreateInfo viewInfo {};
    viewInfo.image = image_;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vk::ImageView view {};
    if (device_.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
        destroy_();
        return false;
    }
    imageView_ = view;

    if (!cmdPool.submit_one_shot(transferQueue, [&](vk::CommandBuffer cmd) {
            gpu_image_mipmap::transition_image_subresource(
                cmd, image_, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, 0, 1, 0, 6);

            for (uint32_t f = 0; f < 6; ++f) {
                vk::BufferImageCopy region {};
                region.bufferOffset =
                    static_cast<vk::DeviceSize>(f * face_bytes);
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask =
                    vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = f;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D { 0, 0, 0 };
                region.imageExtent = vk::Extent3D { faceSize, faceSize, 1 };

                cmd.copyBufferToImage(staging.handle(), image_,
                                      vk::ImageLayout::eTransferDstOptimal, 1,
                                      &region);
            }

            if (mipLevels_ > 1) {
                generate_mipmaps_cube(cmd, image_, faceSize, mipLevels_);
            } else {
                gpu_image_mipmap::transition_image_subresource(
                    cmd, image_, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 0, 6);
            }
        })) {
        destroy_();
        return false;
    }

    if (!create_sampler_(ctx, samplerConfig)) {
        destroy_();
        return false;
    }
    return true;
}

bool Texture::create_cubemap_from_rgba8_mip_chain(
    const Context &ctx, const CubemapRgba8MipLevel *mip_levels,
    size_t mip_level_count, vk::Queue transferQueue, CommandPool &cmdPool,
    const SamplerConfig &samplerConfig, vk::Format format) {
    if (mip_levels == nullptr || mip_level_count == 0) {
        return false;
    }
    if (format != vk::Format::eR8G8B8A8Srgb &&
        format != vk::Format::eR8G8B8A8Unorm) {
        return false;
    }
    const uint32_t base = mip_levels[0].face_size;
    if (base == 0) {
        return false;
    }
    for (size_t i = 0; i < mip_level_count; ++i) {
        const uint32_t expected =
            std::max(1U, base >> static_cast<uint32_t>(i));
        if (mip_levels[i].face_size != expected) {
            return false;
        }
        for (uint32_t f = 0; f < 6U; ++f) {
            if (mip_levels[i].faces[f] == nullptr) {
                return false;
            }
        }
    }

    destroy_();

    size_t staging_total = 0;
    for (size_t i = 0; i < mip_level_count; ++i) {
        const auto fs = static_cast<size_t>(mip_levels[i].face_size);
        staging_total += 6U * fs * fs * 4U;
    }

    mipLevels_ = static_cast<uint32_t>(mip_level_count);
    format_ = format;
    width_ = base;
    height_ = base;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = nullptr;
        return false;
    }

    StagingBuffer staging;
    if (!staging.create(ctx, staging_total)) {
        return false;
    }
    {
        std::vector<uint8_t> concat(staging_total);
        size_t off = 0;
        for (size_t mip = 0; mip < mip_level_count; ++mip) {
            const uint32_t fs = mip_levels[mip].face_size;
            const size_t face_bytes =
                static_cast<size_t>(fs) * static_cast<size_t>(fs) * 4U;
            for (uint32_t f = 0; f < 6U; ++f) {
                std::memcpy(concat.data() + off, mip_levels[mip].faces[f],
                            face_bytes);
                off += face_bytes;
            }
        }
        staging.upload(concat.data(), staging_total);
    }

    vk::ImageCreateInfo imageInfo {};
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D { base, base, 1 };
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_handle {};
    const VkResult vma_rc = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_handle, &allocation_, nullptr);
    if (static_cast<vk::Result>(vma_rc) != vk::Result::eSuccess) {
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_handle;

    vk::ImageViewCreateInfo viewInfo {};
    viewInfo.image = image_;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vk::ImageView view {};
    if (device_.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
        destroy_();
        return false;
    }
    imageView_ = view;

    if (!cmdPool.submit_one_shot(transferQueue, [&](vk::CommandBuffer cmd) {
            gpu_image_mipmap::transition_image_subresource(
                cmd, image_, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, 0, mipLevels_, 0, 6);

            vk::DeviceSize buffer_offset = 0;
            for (uint32_t mip = 0; mip < mipLevels_; ++mip) {
                const uint32_t fs = mip_levels[mip].face_size;
                for (uint32_t f = 0; f < 6u; ++f) {
                    vk::BufferImageCopy region {};
                    region.bufferOffset = buffer_offset;
                    region.bufferRowLength = 0;
                    region.bufferImageHeight = 0;
                    region.imageSubresource.aspectMask =
                        vk::ImageAspectFlagBits::eColor;
                    region.imageSubresource.mipLevel = mip;
                    region.imageSubresource.baseArrayLayer = f;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = vk::Offset3D { 0, 0, 0 };
                    region.imageExtent = vk::Extent3D { fs, fs, 1 };

                    cmd.copyBufferToImage(staging.handle(), image_,
                                          vk::ImageLayout::eTransferDstOptimal,
                                          1, &region);
                    buffer_offset +=
                        static_cast<vk::DeviceSize>(fs) *
                        static_cast<vk::DeviceSize>(fs) * 4u;
                }
            }

            gpu_image_mipmap::transition_image_subresource(
                cmd, image_, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal, 0, mipLevels_, 0, 6);
        })) {
        destroy_();
        return false;
    }

    if (!create_sampler_(ctx, samplerConfig)) {
        destroy_();
        return false;
    }
    return true;
}

bool Texture::create_cubemap_from_rgba32f_faces(
    const Context &ctx, const void *const faces[6], uint32_t faceSize,
    vk::Queue transferQueue, CommandPool &cmdPool,
    const SamplerConfig &samplerConfig) {
    for (uint32_t i = 0; i < 6; ++i) {
        if (faces[i] == nullptr) {
            return false;
        }
    }
    if (faceSize == 0) {
        return false;
    }

    destroy_();

    constexpr size_t kBytesPerTexel = 16U;
    const size_t face_bytes =
        static_cast<size_t>(faceSize) * faceSize * kBytesPerTexel;
    const size_t total_bytes = face_bytes * 6U;
    mipLevels_ = gpu_image_mipmap::calculate_mip_levels(faceSize, faceSize);
    format_ = vk::Format::eR32G32B32A32Sfloat;
    width_ = faceSize;
    height_ = faceSize;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = nullptr;
        return false;
    }

    StagingBuffer staging;
    if (!staging.create(ctx, total_bytes)) {
        return false;
    }
    std::vector<uint8_t> concat(total_bytes);
    for (uint32_t f = 0; f < 6; ++f) {
        std::memcpy(concat.data() + f * face_bytes, faces[f], face_bytes);
    }
    staging.upload(concat.data(), total_bytes);

    vk::ImageCreateInfo imageInfo {};
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D { faceSize, faceSize, 1 };
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage vk_handle {};
    const VkResult vma_rc = vmaCreateImage(
        vma_allocator_,
        reinterpret_cast<const VkImageCreateInfo *>(&imageInfo), &allocCreate,
        &vk_handle, &allocation_, nullptr);
    if (static_cast<vk::Result>(vma_rc) != vk::Result::eSuccess) {
        device_ = nullptr;
        vma_allocator_ = nullptr;
        image_ = nullptr;
        allocation_ = nullptr;
        return false;
    }
    image_ = vk_handle;

    vk::ImageViewCreateInfo viewInfo {};
    viewInfo.image = image_;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    vk::ImageView view {};
    if (device_.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
        destroy_();
        return false;
    }
    imageView_ = view;

    if (!cmdPool.submit_one_shot(transferQueue, [&](vk::CommandBuffer cmd) {
            gpu_image_mipmap::transition_image_subresource(
                cmd, image_, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, 0, 1, 0, 6);

            for (uint32_t f = 0; f < 6; ++f) {
                vk::BufferImageCopy region {};
                region.bufferOffset =
                    static_cast<vk::DeviceSize>(f * face_bytes);
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask =
                    vk::ImageAspectFlagBits::eColor;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = f;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = vk::Offset3D { 0, 0, 0 };
                region.imageExtent = vk::Extent3D { faceSize, faceSize, 1 };

                cmd.copyBufferToImage(staging.handle(), image_,
                                      vk::ImageLayout::eTransferDstOptimal, 1,
                                      &region);
            }

            if (mipLevels_ > 1) {
                generate_mipmaps_cube(cmd, image_, faceSize, mipLevels_);
            } else {
                gpu_image_mipmap::transition_image_subresource(
                    cmd, image_, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageLayout::eShaderReadOnlyOptimal, 0, 1, 0, 6);
            }
        })) {
        destroy_();
        return false;
    }

    if (!create_sampler_(ctx, samplerConfig)) {
        destroy_();
        return false;
    }
    return true;
}

bool Texture::create_sampler_(const Context &ctx, const SamplerConfig &config) {
    const auto &props = ctx.physical_device_properties();
    vk::SamplerCreateInfo samplerInfo {};
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;
    samplerInfo.maxAnisotropy =
        std::min(config.maxAnisotropy, props.limits.maxSamplerAnisotropy);
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = (config.maxLod == SamplerConfig {}.maxLod)
                             ? static_cast<float>(mipLevels_)
                             : config.maxLod;
    samplerInfo.borderColor = config.borderColor;
    samplerInfo.anisotropyEnable =
        config.maxAnisotropy > 1.0F ? vk::True : vk::False;

    vk::Sampler samp {};
    if (device_.createSampler(&samplerInfo, nullptr, &samp) !=
        vk::Result::eSuccess) {
        return false;
    }
    sampler_ = samp;
    return true;
}

void Texture::destroy_() {
    if (sampler_) {
        device_.destroySampler(sampler_);
        sampler_ = nullptr;
    }
    if (imageView_) {
        device_.destroyImageView(imageView_);
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
    format_ = vk::Format::eUndefined;
    width_ = 0;
    height_ = 0;
    mipLevels_ = 0;
}

Texture::~Texture() { destroy_(); }

Texture::Texture(Texture &&other) noexcept
    : device_ { other.device_ }, vma_allocator_ { other.vma_allocator_ },
      image_ { other.image_ }, allocation_ { other.allocation_ },
      imageView_ { other.imageView_ }, sampler_ { other.sampler_ },
      format_ { other.format_ }, width_ { other.width_ },
      height_ { other.height_ }, mipLevels_ { other.mipLevels_ } {

    other.device_ = nullptr;
    other.vma_allocator_ = nullptr;
    other.image_ = nullptr;
    other.allocation_ = nullptr;
    other.imageView_ = nullptr;
    other.sampler_ = nullptr;
}

Texture &Texture::operator=(Texture &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy_();
    device_ = other.device_;
    vma_allocator_ = other.vma_allocator_;
    image_ = other.image_;
    allocation_ = other.allocation_;
    imageView_ = other.imageView_;
    sampler_ = other.sampler_;
    format_ = other.format_;
    width_ = other.width_;
    height_ = other.height_;
    mipLevels_ = other.mipLevels_;
    other.device_ = nullptr;
    other.vma_allocator_ = nullptr;
    other.image_ = nullptr;
    other.allocation_ = nullptr;
    other.imageView_ = nullptr;
    other.sampler_ = nullptr;
    return *this;
}

} // namespace lumen::render
