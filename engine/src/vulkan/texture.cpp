#include "vulkan/texture.hpp"
#include "core/log/logger.hpp"
#include "vulkan/sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace vulkan {

void Texture2D::load_from_file(const std::string_view &path,
                             VmaAllocator allocator, VkDevice device,
                             const UploadContext &uploadCtx) {
    this->allocator = allocator;
    this->device = device;

    stbi_set_flip_vertically_on_load(true);
    int w, h, channels;
    uint8_t *pixels = stbi_load(path.data(), &w, &h, &channels, 4);
    if (!pixels) {
        LUMEN_LOG_ERROR("Failed to load image: {}", path);
        return;
    }

    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
    format = VK_FORMAT_R8G8B8A8_SRGB;

    const std::uint32_t dim = std::max(width, height);
    mipLevels = static_cast<std::uint32_t>(
                    std::floor(std::log2(static_cast<float>(dim)))) +
                1U;

    VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;

    // Staging buffer
    Buffer staging;
    staging.init(allocator, size, BufferUsage::UploadStaging,
                 MemoryMode::CPU_TO_GPU);

    void *mapped;
    vmaMapMemory(allocator, staging.allocation(), &mapped);
    memcpy(mapped, pixels, size);
    vmaUnmapMemory(allocator, staging.allocation());
    stbi_image_free(pixels);

    // Create GPU image
    VkImageCreateInfo imageInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent { width, height, 1 },
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo allocInfo {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };

    vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation,
                   nullptr);

    // Command buffer
    VkCommandBuffer cmd = uploadCtx.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    // Barrier 1: undefined -> transfer dst
    VkImageMemoryBarrier barrier1 {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = image,
        .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = 1,
                            .layerCount = 1 }
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier1);

    // Copy buffer to mip 0
    VkBufferImageCopy copy {
        .imageSubresource { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel = 0,
                            .layerCount = 1 },
        .imageExtent { .width = width, .height = height, .depth = 1 }
    };

    vkCmdCopyBufferToImage(cmd, staging.buffer(), image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    // Mipmap：blit 要求源为 TRANSFER_SRC_OPTIMAL，目标为
    // TRANSFER_DST_OPTIMAL（同一张图需逐层 barrier）
    for (std::uint32_t i = 1; i < mipLevels; ++i) {
        VkImageMemoryBarrier toSrc {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .image = image,
            .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = i - 1,
                                .levelCount = 1,
                                .layerCount = 1 }
        };
        VkImageMemoryBarrier toDst {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image,
            .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = i,
                                .levelCount = 1,
                                .layerCount = 1 }
        };
        std::array<VkImageMemoryBarrier, 2> mips { toSrc, toDst };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, static_cast<std::uint32_t>(mips.size()),
                             mips.data());

        const int32_t srcW =
            std::max(1, static_cast<int32_t>(width >> (i - 1)));
        const int32_t srcH =
            std::max(1, static_cast<int32_t>(height >> (i - 1)));
        const int32_t dstW = std::max(1, static_cast<int32_t>(width >> i));
        const int32_t dstH = std::max(1, static_cast<int32_t>(height >> i));

        VkImageBlit blit {
            .srcSubresource { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .mipLevel = i - 1,
                              .layerCount = 1 },
            .srcOffsets { { 0, 0, 0 }, { srcW, srcH, 1 } },
            .dstSubresource { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                              .mipLevel = i,
                              .layerCount = 1 },
            .dstOffsets { { 0, 0, 0 }, { dstW, dstH, 1 } }
        };

        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);

        VkImageMemoryBarrier srcBack {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = image,
            .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .baseMipLevel = i - 1,
                                .levelCount = 1,
                                .layerCount = 1 }
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &srcBack);
    }

    // Barrier 2：所有 mip → shader read（含最后仍为 DST 的 mip）
    VkImageMemoryBarrier barrier2 {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = image,
        .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = mipLevels,
                            .layerCount = 1 }
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier2);

    vkEndCommandBuffer(cmd);

    // Submit & wait
    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };

    vkResetFences(device, 1, &uploadCtx.fence);
    vkQueueSubmit(uploadCtx.queue, 1, &submit, uploadCtx.fence);
    vkWaitForFences(device, 1, &uploadCtx.fence, VK_TRUE, UINT64_MAX);

    staging.destroy();

    // Image view
    VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = mipLevels,
                            .layerCount = 1 }
    };
    vkCreateImageView(device, &viewInfo, nullptr, &view);

    // Sampler：使用 mip 链时需线性 mipmap + LOD 范围
    sampler = create_sampler(
        device,
        {
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .minLod = 0.0F,
            .maxLod = static_cast<float>(mipLevels > 0 ? mipLevels - 1 : 0),
        });
}

void Texture2D::destroy(VkDevice device) {
    if (sampler)
        vkDestroySampler(device, sampler, nullptr);
    if (view)
        vkDestroyImageView(device, view, nullptr);
    if (image)
        vmaDestroyImage(allocator, image, allocation);

    image = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    sampler = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

TexturePool &TexturePool::instance() {
    static TexturePool inst;
    return inst;
}

void TexturePool::init(VmaAllocator allocator, VkDevice device,
                       const UploadContext *uploadContext) {
    this->allocator_ = allocator;
    this->device_ = device;
    this->uploadContext_ = uploadContext;
}

Texture2D *TexturePool::get_or_load(const std::string &path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) {
        return &it->second;
    }

    Texture2D tex;
    tex.load_from_file(path, allocator_, device_, *uploadContext_);
    textures_[path] = std::move(tex);
    return &textures_[path];
}

void TexturePool::clear() {
    for (auto &[k, v] : textures_) {
        v.destroy(device_);
    }
    textures_.clear();
}

} // namespace vulkan
