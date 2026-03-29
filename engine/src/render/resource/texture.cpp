/**
 * @file texture.cpp
 * @brief Texture 实现（上传 / Layout 转换 / Mipmap / Cubemap）
 *
 * @details
 * 实现 Texture 的 GPU 资源创建流程，包括：
 *
 * - CPU → GPU 数据上传（Staging Buffer）
 * - VkImage 创建（VMA 分配）
 * - Image Layout 转换（Pipeline Barrier）
 * - Mipmap 生成（vkCmdBlitImage）
 * - Cubemap 构建（6 faces / mip chain）
 *
 * @note
 * 该文件只包含“实现逻辑”，不重复 API 说明（见 texture.hpp）
 *
 * @warning
 * 当前实现使用 vkQueueWaitIdle（同步简单但性能较差）
 * 实际项目建议改为 Fence / 异步提交
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

/**
 * @brief 计算 mipmap 层数
 *
 * @details
 * mipLevels = floor(log2(max(width, height))) + 1
 *
 * Vulkan 中完整 mip 链要求尺寸逐级减半直到 1x1。
 *
 * @note
 * - 最小尺寸为 1
 * - 用于 vkCmdBlitImage 生成 mip
 */
uint32_t calculate_mip_levels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(
               std::floor(std::log2(std::max(width, height)))) +
           1;
}

/**
 * @brief 图像子资源 layout 转换（带 pipeline barrier）
 *
 * @param cmd 命令缓冲
 * @param image 目标图像
 * @param oldLayout 原 layout
 * @param newLayout 新 layout
 * @param baseMipLevel 起始 mip
 * @param levelCount mip 数量
 * @param baseArrayLayer 起始层
 * @param layerCount 层数
 *
 * @details
 * 封装 vkCmdPipelineBarrier，用于：
 * - 写后读同步（TRANSFER → SHADER）
 * - blit 前后同步（DST ↔ SRC）
 *
 * @note
 * 这里只处理常见路径：
 * - UNDEFINED → TRANSFER_DST
 * - TRANSFER_DST → TRANSFER_SRC
 * - TRANSFER_SRC → SHADER_READ_ONLY
 *
 * @warning
 * - 未覆盖的 layout 组合会被忽略（直接 return）
 * - 不适用于 depth/stencil
 *
 * @todo 支持更多 layout 组合
 */
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

    VkPipelineStageFlags srcStage {};
    VkPipelineStageFlags dstStage {};

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
    transition_image_subresource(cmd, image, oldLayout, newLayout, baseMipLevel,
                                 levelCount, 0, 1);
}

/**
 * @brief 生成 2D 纹理 mipmap（GPU blit）
 *
 * @param cmd 命令缓冲
 * @param image 图像
 * @param width 初始宽度
 * @param height 初始高度
 * @param mipLevels mip 层数
 *
 * @details
 * 过程：
 * 1. 将 mip[i-1] 转为 TRANSFER_SRC
 * 2. blit → mip[i]
 * 3. mip[i-1] 转为 SHADER_READ_ONLY
 *
 * 最后一层单独转 layout。
 *
 * @note
 * 使用 VK_FILTER_LINEAR 做下采样
 *
 * @warning
 * - 要求 format 支持 linear blit
 * - 否则需要使用 compute shader
 */
void generate_mipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width,
                      uint32_t height, uint32_t mipLevels) {
    auto mipWidth = static_cast<int32_t>(width);
    auto mipHeight = static_cast<int32_t>(height);

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

/**
 * @brief 生成 Cubemap mipmap
 *
 * @details
 * 对 6 个 face 分别执行 mipmap 生成。
 *
 * 每个 face 独立：
 * - 避免 layer 间依赖
 * - 简化 barrier 范围
 *
 * @note
 * baseArrayLayer = face
 *
 * @warning
 * - 必须确保 image 创建时 arrayLayers=6
 * - 必须带 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
 */
void generate_mipmaps_cube(VkCommandBuffer cmd, VkImage image, uint32_t dim,
                           uint32_t mipLevels) {
    for (uint32_t face = 0; face < 6; ++face) {
        auto mipWidth = static_cast<int32_t>(dim);
        auto mipHeight = static_cast<int32_t>(dim);
        for (uint32_t i = 1; i < mipLevels; ++i) {
            transition_image_subresource(
                cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i - 1, 1, face, 1);
            transition_image_subresource(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         i, 1, face, 1);

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
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &blit, VK_FILTER_LINEAR);

            transition_image_subresource(
                cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i - 1, 1, face, 1);

            mipWidth = std::max(1, mipWidth / 2);
            mipHeight = std::max(1, mipHeight / 2);
        }
        transition_image_subresource(cmd, image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     mipLevels - 1, 1, face, 1);
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

/**
 * @brief 从文件加载纹理（stb_image）
 *
 * @details
 * - 强制转换为 RGBA8
 * - 自动生成 mipmap
 *
 * @note
 * stb 默认：
 * - (0,0) 在左上
 *
 * Vulkan：
 * - (0,0) 在左下
 *
 * → 需要 flip（stbi_set_flip_vertically_on_load）
 */
bool Texture::create_from_file(const Context &ctx, const char *filePath,
                               VkQueue transferQueue, CommandPool &cmdPool,
                               const SamplerConfig &samplerConfig,
                               VkFormat format) {
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
                                   VkQueue transferQueue, CommandPool &cmdPool,
                                   VkFormat format,
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

/**
 * @brief 从像素数据创建 GPU 纹理（内部实现）
 *
 * @param data 像素数据
 * @param imageSizeBytes 数据大小
 * @param texWidth 宽度
 * @param texHeight 高度
 * @param format 图像格式
 * @param doMipmaps 是否生成 mip
 *
 * @details
 * 完整流程：
 * 1. 创建 staging buffer（CPU → GPU）
 * 2. 创建 VkImage（VMA 分配）
 * 3. UNDEFINED → TRANSFER_DST
 * 4. 拷贝 buffer → image
 * 5. 生成 mip（可选）
 * 6. 转换为 SHADER_READ_ONLY
 *
 * @note
 * 使用 one-time command buffer 提交
 *
 * @warning
 * - 使用 vkQueueWaitIdle（简单但低效）
 * - 实际项目建议用 fence / async
 */
bool Texture::create_from_pixels_(const Context &ctx, const void *data,
                                  size_t imageSizeBytes, uint32_t texWidth,
                                  uint32_t texHeight, VkFormat format,
                                  VkQueue transferQueue, CommandPool &cmdPool,
                                  bool doMipmaps) {
    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        return false;
    }

    mipLevels_ = doMipmaps ? calculate_mip_levels(texWidth, texHeight) : 1U;
    format_ = format;
    width_ = texWidth;
    height_ = texHeight;
    device_ = ctx.device();
    vma_allocator_ = vma;

    // Staging buffer
    // TODO: 换成专用的 Staging buffer
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
    auto buffers = cmdPool.allocate(1, CommandBufferLevel::Primary);
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

bool Texture::create_cubemap_from_rgba8_faces(
    const Context &ctx, const void *const faces[6], uint32_t faceSize,
    VkQueue transferQueue, CommandPool &cmdPool,
    const SamplerConfig &samplerConfig, VkFormat format) {
    for (uint32_t i = 0; i < 6; ++i) {
        if (faces[i] == nullptr) {
            return false;
        }
    }
    if (faceSize == 0) {
        return false;
    }
    if (format != VK_FORMAT_R8G8B8A8_SRGB &&
        format != VK_FORMAT_R8G8B8A8_UNORM) {
        return false;
    }

    destroy_();

    const size_t face_bytes = static_cast<size_t>(faceSize) * faceSize * 4U;
    const size_t total_bytes = face_bytes * 6U;
    mipLevels_ = calculate_mip_levels(faceSize, faceSize);
    format_ = format;
    width_ = faceSize;
    height_ = faceSize;
    device_ = ctx.device();
    vma_allocator_ = ctx.vma_allocator();
    if (vma_allocator_ == nullptr) {
        device_ = VK_NULL_HANDLE;
        return false;
    }

    // TODO: 换成 StageBuffer
    // 1. Upload image data to staging buffer
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

    // 2. Create images
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

    // 3. Create image views
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

    // 3. Copy buffer to image
    auto buffers = cmdPool.allocate(1, CommandBufferLevel::Primary);
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
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
    }

    // 4. Need generater mipmaps?
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

bool Texture::create_cubemap_from_rgba8_mip_chain(
    const Context &ctx, const CubemapRgba8MipLevel *mip_levels,
    size_t mip_level_count, VkQueue transferQueue, CommandPool &cmdPool,
    const SamplerConfig &samplerConfig, VkFormat format) {
    // 各种 Check
    if (mip_levels == nullptr || mip_level_count == 0) {
        return false;
    }
    if (format != VK_FORMAT_R8G8B8A8_SRGB &&
        format != VK_FORMAT_R8G8B8A8_UNORM) {
        return false;
    }
    const uint32_t base = mip_levels[0].face_size;
    if (base == 0) {
        return false;
    }
    // Check mipmap 是否合理
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

    // 计算总 size
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
        device_ = VK_NULL_HANDLE;
        return false;
    }

    // TODO: 使用 StageBuffer 替换
    // Create staging buffer and upload data to buffer
    Buffer staging;
    BufferCreateInfo stagingInfo { staging_total, BufferUsage::Staging, true };
    if (!staging.create(ctx, stagingInfo)) {
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

    // Create image
    VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = base;
    imageInfo.extent.height = base;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels_;
    imageInfo.arrayLayers = 6;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

    // Create image view
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

    // Copy buffer to image
    auto buffers = cmdPool.allocate(1, CommandBufferLevel::Primary);
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
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                 mipLevels_, 0, 6);

    VkDeviceSize buffer_offset = 0;
    for (uint32_t mip = 0; mip < mipLevels_; ++mip) {
        const uint32_t fs = mip_levels[mip].face_size;
        for (uint32_t f = 0; f < 6u; ++f) {
            VkBufferImageCopy region {};
            region.bufferOffset = buffer_offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = f;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { fs, fs, 1 };

            vkCmdCopyBufferToImage(cmd, staging.handle(), image_,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &region);
            buffer_offset += static_cast<VkDeviceSize>(fs) *
                             static_cast<VkDeviceSize>(fs) * 4u;
        }
    }

    transition_image_subresource(
        cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, mipLevels_, 0, 6);

    vkEndCommandBuffer(cmd);

    // Submit
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

bool Texture::create_cubemap_from_rgba32f_faces(
    const Context &ctx, const void *const faces[6], uint32_t faceSize,
    VkQueue transferQueue, CommandPool &cmdPool,
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

    auto buffers = cmdPool.allocate(1, CommandBufferLevel::Primary);
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
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
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

/**
 * @brief 创建 Vulkan Sampler
 *
 * @details
 * - 自动 clamp anisotropy 到设备上限
 * - maxLod 默认 = mipLevels
 *
 * @note
 * anisotropyEnable 仅在 >0 时开启
 */
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
    if (samplerInfo.maxAnisotropy > 0.0F) {
        samplerInfo.anisotropyEnable = VK_TRUE;
    }

    VkResult result =
        vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_);
    return result == VK_SUCCESS;
}

/**
 * @brief 释放 GPU 资源
 *
 * @details
 * 释放顺序：
 * 1. sampler
 * 2. image view
 * 3. image（VMA）
 *
 * @note
 * 该函数被：
 * - 析构函数
 * - move assignment
 * 调用
 */
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
    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
    return *this;
}

} // namespace lumen::render
