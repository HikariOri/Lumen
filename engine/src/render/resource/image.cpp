/**
 * @file image.cpp
 * @brief Image 实现
 *
 * @details
 * 实现 Vulkan Image 的创建、内存分配（VMA）、ImageView 创建、
 * 以及基础纹理加载逻辑。
 *
 * -------------------------------
 * 【Image 创建流程】
 * -------------------------------
 * @dot
 * digraph ImageCreateFlow {
 *     rankdir=LR;
 *
 *     Info   [label="ImageCreateInfo"];
 *     Image  [label="VkImage"];
 *     Memory [label="VMA Allocation"];
 *     View   [label="VkImageView"];
 *
 *     Info -> Image -> Memory -> View;
 * }
 * @enddot
 *
 * -------------------------------
 * 【纹理加载数据流】
 * -------------------------------
 * @dot
 * digraph TextureUpload {
 *     rankdir=LR;
 *
 *     File   [label="图片文件"];
 *     CPU    [label="CPU 像素数据"];
 *     Staging[label="Staging Buffer"];
 *     Image  [label="VkImage"];
 *     Shader [label="Shader 采样"];
 *
 *     File -> CPU -> Staging -> Image -> Shader;
 * }
 * @enddot
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

#include <cstdint>

namespace lumen::render {

bool Image::create(const Context &ctx, const ImageCreateInfo &info) {
    /// 参数校验
    if (info.width == 0 || info.height == 0)
        return false;

    VmaAllocator vma = ctx.vma_allocator();
    if (vma == nullptr) {
        LUMEN_LOG_ERROR("Image 创建失败: VMA 未初始化");
        return false;
    }

    /// 保存上下文信息
    device_ = ctx.device();
    vma_allocator_ = vma;
    width_ = info.width;
    height_ = info.height;
    format_ = info.format;
    mipLevels_ = info.mipLevels;

    /// 自动计算 mipmap
    if (info.generateMipmaps) {
        mipLevels_ =
            gpu_image_mipmap::calculate_mip_levels(info.width, info.height);
    }

    /**
     * -------------------------------
     * 创建 VkImage
     * -------------------------------
     *
     * VkImage 只描述“数据结构”，不包含内存
     */
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

    /// Cube map 标记
    if (info.type == ImageType::TexCube) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    /**
     * -------------------------------
     * 使用 VMA 分配显存
     * -------------------------------
     */
    VmaAllocationCreateInfo allocCreate {};
    allocCreate.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(vma_allocator_, &imageInfo, &allocCreate,
                                     &image_, &allocation_, nullptr);

    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Image 创建失败: {} ({}x{})", static_cast<int>(result),
                        info.width, info.height);
        device_ = VK_NULL_HANDLE;
        vma_allocator_ = nullptr;
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
        return false;
    }

    /**
     * -------------------------------
     * 创建 ImageView
     * -------------------------------
     *
     * ImageView 定义：
     * - 如何访问图像
     * - 哪些 mip / layer 可见
     *
     * VkImage 本身不能直接用于 shader 或 framebuffer
     */
    VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image_;

    viewInfo.viewType = info.type == ImageType::TexCube
                            ? VK_IMAGE_VIEW_TYPE_CUBE
                            : VK_IMAGE_VIEW_TYPE_2D;

    viewInfo.format = info.format;

    /// 指定访问范围（颜色 / 深度）
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

bool Image::create_from_file(const Context &ctx, const char *filePath,
                             VkQueue transferQueue, CommandPool &cmdPool) {
    /**
     * Vulkan 坐标系：
     * (0,0) 在左下角，因此需要翻转
     */
    stbi_set_flip_vertically_on_load(1);

    int w { 0 }, h { 0 }, channels { 0 };

    /// 加载图片
    stbi_uc *pixels = stbi_load(filePath, &w, &h, &channels, STBI_rgb_alpha);

    if (!pixels)
        return false;

    if (w <= 0 || h <= 0) {
        stbi_image_free(pixels);
        return false;
    }

    const VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
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

    ImageCreateInfo info {};
    info.width = texW;
    info.height = texH;
    info.format = format;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT;
    info.generateMipmaps = true;

    if (!create(ctx, info)) {
        return false;
    }

    const bool uploaded = cmdPool.submit_one_shot(
        transferQueue, [&](const CommandBuffer &cmd) {
            const VkCommandBuffer vkCmd = cmd.handle();
            gpu_image_mipmap::transition_image_layout(
                vkCmd, image_, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, mipLevels_);

            VkBufferImageCopy region {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { texW, texH, 1 };

            vkCmdCopyBufferToImage(vkCmd, staging.handle(), image_,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &region);

            if (mipLevels_ > 1) {
                gpu_image_mipmap::generate_mipmaps(vkCmd, image_, texW, texH,
                                                   mipLevels_);
            } else {
                gpu_image_mipmap::transition_image_layout(
                    vkCmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
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
    /**
     * 深度图像创建
     *
     * 用于：
     * - RenderPass depth attachment
     *
     * 要求：
     * - usage = DEPTH_STENCIL_ATTACHMENT
     * - aspect = DEPTH
     */
    ImageCreateInfo info {};
    info.width = width;
    info.height = height;
    info.format = VK_FORMAT_D32_SFLOAT;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    return create(ctx, info);
}

void Image::destroy_() {
    /// 先销毁 View（依赖 Image）
    if (imageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }

    /// 再销毁 Image + 内存
    if (image_ != VK_NULL_HANDLE && vma_allocator_ != nullptr &&
        allocation_ != nullptr) {
        vmaDestroyImage(vma_allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }

    vma_allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

Image::~Image() { destroy_(); }

/**
 * @brief 移动构造
 *
 * @details
 * 转移 GPU 资源所有权，避免重复释放
 */
Image::Image(Image &&other) noexcept
    : device_ { other.device_ }, vma_allocator_ { other.vma_allocator_ },
      image_ { other.image_ }, allocation_ { other.allocation_ },
      imageView_ { other.imageView_ }, format_ { other.format_ },
      width_ { other.width_ }, height_ { other.height_ },
      mipLevels_ { other.mipLevels_ } {

    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;
}

Image &Image::operator=(Image &&other) noexcept {
    if (this == &other)
        return *this;

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

    other.device_ = VK_NULL_HANDLE;
    other.vma_allocator_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.imageView_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace lumen::render
