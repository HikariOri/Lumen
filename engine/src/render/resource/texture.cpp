/**
 * @file texture.cpp
 * @brief Texture 实现：从文件加载、Staging 上传、Mipmap 生成
 */

#include "render/resource/texture.hpp"
#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/image.hpp"
#include "render/resource/sampler.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lumen::render {

namespace {

uint32_t calculate_mip_levels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(
               std::floor(std::log2(std::max(width, height)))) +
           1;
}

void transition_image_subresource(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout,
                                  uint32_t baseMipLevel, uint32_t levelCount,
                                  uint32_t baseArrayLayer,
                                  uint32_t layerCount) {
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags srcStage, dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        return;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             uint32_t baseMipLevel, uint32_t levelCount) {
    transition_image_subresource(cmd, image, oldLayout, newLayout,
                                 baseMipLevel, levelCount, 0, 1);
}

void generate_mipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width,
                      uint32_t height, uint32_t mipLevels) {
    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; ++i) {
        // mip i-1: TRANSFER_DST -> TRANSFER_SRC
        transition_image_layout(cmd, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1);

        VkImageBlit blit {};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { std::max(1, mipWidth / 2),
                               std::max(1, mipHeight / 2), 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);

        // mip i-1: TRANSFER_SRC -> SHADER_READ_ONLY
        transition_image_layout(
            cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1);

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    // 最后一层 mip: TRANSFER_DST -> SHADER_READ_ONLY
    transition_image_layout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            mipLevels - 1, 1);
}

void generate_mipmaps_cube(VkCommandBuffer cmd, VkImage image, uint32_t dim,
                           uint32_t mipLevels) {
    for (uint32_t face = 0; face < 6; ++face) {
        int32_t mipWidth = static_cast<int32_t>(dim);
        int32_t mipHeight = static_cast<int32_t>(dim);
        for (uint32_t i = 1; i < mipLevels; ++i) {
            transition_image_subresource(
                cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1, face, 1);
            transition_image_subresource(
                cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, i, 1, face, 1);

            VkImageBlit blit {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = face;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { std::max(1, mipWidth / 2),
                                   std::max(1, mipHeight / 2), 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = face;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);

            transition_image_subresource(
                cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1, face, 1);

            mipWidth = std::max(1, mipWidth / 2);
            mipHeight = std::max(1, mipHeight / 2);
        }
        transition_image_subresource(
            cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels - 1, 1, face, 1);
    }
}

} // namespace

bool Texture::create_from_memory(const Context &ctx, const void *data,
                                 size_t imageSizeBytes, uint32_t width,
                                 uint32_t height, VkQueue transferQueue,
                                 CommandPool &cmdPool, VkFormat format,
                                 const SamplerConfig &samplerConfig,
                                 bool generateMipmaps) {
    if (!data || imageSizeBytes == 0 || width == 0 || height == 0) {
        return false;
    }
    // 至少需 width*height 字节（如 R8），常用 RGBA8 为 width*height*4
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
                               VkQueue transferQueue, CommandPool &cmdPool,
                               const SamplerConfig &samplerConfig) {
    stbi_set_flip_vertically_on_load(
        1); // Vulkan 纹理 (0,0)=左下，stb 默认行 0=顶部，需翻转
    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load(filePath, &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LUMEN_LOG_ERROR("纹理加载失败: {}", filePath);
        return false;
    }

    const uint32_t texWidth = static_cast<unsigned>(w);
    const uint32_t texHeight = static_cast<unsigned>(h);
    const size_t imageSize = static_cast<size_t>(texWidth) * texHeight * 4;

    bool ok = create_from_pixels_(ctx, pixels, imageSize, texWidth, texHeight,
                                  VK_FORMAT_R8G8B8A8_SRGB, transferQueue,
                                  cmdPool, true);
    stbi_image_free(pixels);

    if (!ok) {
        LUMEN_LOG_ERROR("纹理从像素创建失败: {}x{}", texWidth, texHeight);
        return false;
    }
    LUMEN_LOG_DEBUG("纹理加载成功: {} {}x{}", filePath, texWidth, texHeight);
    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_from_ktx_file(const Context &ctx, const char *filePath,
                                   VkQueue transferQueue, CommandPool &cmdPool,
                                   VkFormat format,
                                   const SamplerConfig &samplerConfig) {
    std::vector<std::uint8_t> rgba;
    std::uint32_t texWidth = 0;
    std::uint32_t texHeight = 0;
    std::string kerr;
    if (!lumen::core::decode_ktx_file_to_rgba8(filePath, rgba, texWidth,
                                               texHeight, &kerr)) {
        LUMEN_LOG_ERROR("KTX 解码失败 {}: {}", filePath != nullptr ? filePath : "",
                        kerr);
        return false;
    }
    const size_t imageSize =
        static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4u;
    if (!create_from_pixels_(ctx, rgba.data(), imageSize, texWidth, texHeight,
                             format, transferQueue, cmdPool, true)) {
        LUMEN_LOG_ERROR("KTX 纹理上传失败: {}x{}", texWidth, texHeight);
        return false;
    }
    LUMEN_LOG_DEBUG("KTX 纹理加载成功: {} {}x{}", filePath, texWidth, texHeight);
    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_from_pixels_(const Context &ctx, const void *data,
                                  size_t imageSizeBytes, uint32_t texWidth,
                                  uint32_t texHeight, VkFormat format,
                                  VkQueue transferQueue, CommandPool &cmdPool,
                                  bool doMipmaps) {
    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        return false;
    }

    mipLevels_ = doMipmaps ? calculate_mip_levels(texWidth, texHeight) : 1u;
    format_ = format;
    width_ = texWidth;
    height_ = texHeight;
    device_ = ctx.device();
    vma_allocator_ = vma;

    // Staging buffer
    Buffer staging;
    BufferCreateInfo stagingInfo { imageSizeBytes, BufferUsage::Staging, true };
    if (!staging.create(ctx, stagingInfo)) {
        return false;
    }
    staging.upload(data, imageSizeBytes);

    // Create image
    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(vma_allocator_, &imageInfo, &allocCreate,
                                     &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("纹理 Image/VMA 创建失败: {} ({}x{})",
                        static_cast<int>(result), texWidth, texHeight);
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    // ImageView
    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(device_, &viewInfo, nullptr, &imageView_);
    if (result != VK_SUCCESS) {
        destroy_();
        return false;
    }

    // One-shot command buffer: copy + mipmaps
    auto buffers = cmdPool.allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    if (buffers.empty()) {
        destroy_();
        return false;
    }
    VkCommandBuffer cmd = buffers[0];

    VkCommandBufferBeginInfo beginInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    transition_image_layout(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                            mipLevels_);

    VkBufferImageCopy region {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { texWidth, texHeight, 1 };

    vkCmdCopyBufferToImage(cmd, staging.handle(), image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (mipLevels_ > 1) {
        generate_mipmaps(cmd, image_, texWidth, texHeight, mipLevels_);
    } else {
        transition_image_layout(cmd, image_,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(transferQueue);

    cmdPool.free(buffers);

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
    imageInfo.usage = info.usage | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    if (info.type == ImageType::TexCube) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(vma_allocator_, &imageInfo, &allocCreate,
                                     &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image_;
    viewInfo.viewType = (info.type == ImageType::TexCube)
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

    return create_sampler_(ctx, samplerConfig);
}

bool Texture::create_cubemap_from_rgba8_faces(const Context &ctx,
                                              const void *const faces[6],
                                              uint32_t faceSize,
                                              VkQueue transferQueue,
                                              CommandPool &cmdPool,
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

    const size_t face_bytes =
        static_cast<size_t>(faceSize) * faceSize * 4u;
    const size_t total_bytes = face_bytes * 6u;
    mipLevels_ = calculate_mip_levels(faceSize, faceSize);
    format_ = VK_FORMAT_R8G8B8A8_SRGB;
    width_ = faceSize;
    height_ = faceSize;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = VK_NULL_HANDLE;
        return false;
    }

    Buffer staging;
    BufferCreateInfo stagingInfo { total_bytes, BufferUsage::Staging, true };
    if (!staging.create(ctx, stagingInfo)) {
        return false;
    }
    std::vector<uint8_t> concat(total_bytes);
    for (uint32_t f = 0; f < 6; ++f) {
        std::memcpy(concat.data() + f * face_bytes, faces[f], face_bytes);
    }
    staging.upload(concat.data(), total_bytes);

    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = faceSize;
    imageInfo.extent.height = faceSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(vma_allocator_, &imageInfo, &allocCreate,
                                     &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    result = vkCreateImageView(device_, &viewInfo, nullptr, &imageView_);
    if (result != VK_SUCCESS) {
        destroy_();
        return false;
    }

    auto buffers = cmdPool.allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    if (buffers.empty()) {
        destroy_();
        return false;
    }
    VkCommandBuffer cmd = buffers[0];

    VkCommandBufferBeginInfo beginInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transition_image_subresource(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 0,
                                 6);

    for (uint32_t f = 0; f < 6; ++f) {
        VkBufferImageCopy region {};
        region.bufferOffset = static_cast<VkDeviceSize>(f * face_bytes);
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = f;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { faceSize, faceSize, 1 };

        vkCmdCopyBufferToImage(cmd, staging.handle(), image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    if (mipLevels_ > 1) {
        generate_mipmaps_cube(cmd, image_, faceSize, mipLevels_);
    } else {
        transition_image_subresource(
            cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, 0, 6);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(transferQueue);

    cmdPool.free(buffers);

    if (!create_sampler_(ctx, samplerConfig)) {
        destroy_();
        return false;
    }
    return true;
}

bool Texture::create_cubemap_from_rgba32f_faces(const Context &ctx,
                                               const void *const faces[6],
                                               uint32_t faceSize,
                                               VkQueue transferQueue,
                                               CommandPool &cmdPool,
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

    constexpr size_t kBytesPerTexel = 16u;
    const size_t face_bytes =
        static_cast<size_t>(faceSize) * faceSize * kBytesPerTexel;
    const size_t total_bytes = face_bytes * 6u;
    mipLevels_ = calculate_mip_levels(faceSize, faceSize);
    format_ = VK_FORMAT_R32G32B32A32_SFLOAT;
    width_ = faceSize;
    height_ = faceSize;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = VK_NULL_HANDLE;
        return false;
    }

    Buffer staging;
    BufferCreateInfo stagingInfo { total_bytes, BufferUsage::Staging, true };
    if (!staging.create(ctx, stagingInfo)) {
        return false;
    }
    std::vector<uint8_t> concat(total_bytes);
    for (uint32_t f = 0; f < 6; ++f) {
        std::memcpy(concat.data() + f * face_bytes, faces[f], face_bytes);
    }
    staging.upload(concat.data(), total_bytes);

    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = faceSize;
    imageInfo.extent.height = faceSize;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(vma_allocator_, &imageInfo, &allocCreate,
                                     &image_, &allocation_, nullptr);
    if (result != VK_SUCCESS) {
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels_;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    result = vkCreateImageView(device_, &viewInfo, nullptr, &imageView_);
    if (result != VK_SUCCESS) {
        destroy_();
        return false;
    }

    auto buffers = cmdPool.allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    if (buffers.empty()) {
        destroy_();
        return false;
    }
    VkCommandBuffer cmd = buffers[0];

    VkCommandBufferBeginInfo beginInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transition_image_subresource(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 0,
                                 6);

    for (uint32_t f = 0; f < 6; ++f) {
        VkBufferImageCopy region {};
        region.bufferOffset = static_cast<VkDeviceSize>(f * face_bytes);
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = f;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { faceSize, faceSize, 1 };

        vkCmdCopyBufferToImage(cmd, staging.handle(), image_,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    if (mipLevels_ > 1) {
        generate_mipmaps_cube(cmd, image_, faceSize, mipLevels_);
    } else {
        transition_image_subresource(
            cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, 0, 6);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(transferQueue);

    cmdPool.free(buffers);

    if (!create_sampler_(ctx, samplerConfig)) {
        destroy_();
        return false;
    }
    return true;
}

bool Texture::create_sampler_(const Context &ctx, const SamplerConfig &config) {
    const auto &props = ctx.physical_device_properties();
    VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.mipmapMode = config.mipmapMode;
    samplerInfo.addressModeU = config.addressModeU;
    samplerInfo.addressModeV = config.addressModeV;
    samplerInfo.addressModeW = config.addressModeW;
    samplerInfo.maxAnisotropy =
        std::min(config.maxAnisotropy, props.limits.maxSamplerAnisotropy);
    samplerInfo.minLod = config.minLod;
    samplerInfo.maxLod = (config.maxLod == VK_LOD_CLAMP_NONE)
                             ? static_cast<float>(mipLevels_)
                             : config.maxLod;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    if (samplerInfo.maxAnisotropy > 0.0f) {
        samplerInfo.anisotropyEnable = VK_TRUE;
    }

    VkResult result =
        vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);
    return result == VK_SUCCESS;
}

void Texture::destroy_() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (imageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE && vma_allocator_ != nullptr &&
        allocation_ != nullptr) {
        vmaDestroyImage(vma_allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
    vma_allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
    format_ = VK_FORMAT_UNDEFINED;
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
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

Texture &Texture::operator=(Texture &&other) noexcept {
    if (this == &other)
        return *this;
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
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    return *this;
}

} // namespace lumen::render
