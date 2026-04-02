/**
 * @file gpu_image_mipmap.cpp
 * @brief 2D 颜色图像 layout 转换与 mipmap blit（Texture / Image 共用）
 */

#include "render/resource/detail/gpu_image_mipmap.hpp"

#include <algorithm>
#include <cmath>

namespace lumen::render::gpu_image_mipmap {

uint32_t calculate_mip_levels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(
               std::floor(std::log2(std::max(width, height)))) +
           1;
}

void transition_image_subresource(vk::CommandBuffer cmd, vk::Image image,
                                    vk::ImageLayout oldLayout,
                                    vk::ImageLayout newLayout,
                                    uint32_t baseMipLevel, uint32_t levelCount,
                                    uint32_t baseArrayLayer,
                                    uint32_t layerCount) {
    vk::ImageMemoryBarrier barrier {};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;

    vk::PipelineStageFlags srcStage {};
    vk::PipelineStageFlags dstStage {};

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eTransferSrcOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferSrcOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
               newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal &&
               newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eVertexShader |
                   vk::PipelineStageFlagBits::eFragmentShader |
                   vk::PipelineStageFlagBits::eComputeShader;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eGeneral &&
               newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                                vk::AccessFlagBits::eMemoryWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eAllCommands;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferSrcOptimal &&
               newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else {
        return;
    }

    cmd.pipelineBarrier(srcStage, dstStage, {}, 0, nullptr, 0, nullptr, 1,
                        &barrier);
}

void transition_image_layout(vk::CommandBuffer cmd, vk::Image image,
                               vk::ImageLayout oldLayout,
                               vk::ImageLayout newLayout,
                               uint32_t baseMipLevel, uint32_t levelCount) {
    transition_image_subresource(cmd, image, oldLayout, newLayout, baseMipLevel,
                                 levelCount, 0, 1);
}

void generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, uint32_t width,
                      uint32_t height, uint32_t mipLevels) {
    auto mipWidth = static_cast<int32_t>(width);
    auto mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; ++i) {
        transition_image_layout(
            cmd, image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eTransferSrcOptimal, i - 1, 1);

        vk::ImageBlit blit {};
        blit.srcOffsets[0] = vk::Offset3D { 0, 0, 0 };
        blit.srcOffsets[1] = vk::Offset3D { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = vk::Offset3D { 0, 0, 0 };
        blit.dstOffsets[1] = vk::Offset3D { std::max(1, mipWidth / 2),
                                           std::max(1, mipHeight / 2), 1 };
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
                      vk::ImageLayout::eTransferDstOptimal, 1, &blit,
                      vk::Filter::eLinear);

        transition_image_layout(
            cmd, image, vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, i - 1, 1);

        mipWidth = std::max(1, mipWidth / 2);
        mipHeight = std::max(1, mipHeight / 2);
    }

    transition_image_layout(cmd, image, vk::ImageLayout::eTransferDstOptimal,
                            vk::ImageLayout::eShaderReadOnlyOptimal,
                            mipLevels - 1, 1);
}

} // namespace lumen::render::gpu_image_mipmap
