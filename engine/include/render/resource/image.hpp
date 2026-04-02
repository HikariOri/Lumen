/**
 * @file image.hpp
 * @brief Vulkan Image 封装：2D 纹理、立方体纹理、Mipmap 与 ImageView
 *
 * @details
 * 本模块封装了 Vulkan 中 Image 的创建、内存分配（VMA）、
 * ImageView 创建以及资源生命周期管理。
 *
 * 提供能力：
 * - 2D / Cube 纹理创建
 * - 自动创建 ImageView
 * - 支持多级 Mipmap
 * - 深度附件 Image 创建
 * - 从文件加载纹理（PNG / JPG 等）
 *
 * 设计目标：
 * - 简化 Image + VkDeviceMemory/VMA 使用流程
 * - 将 Image 与 ImageView 绑定为一个资源单元
 * - 提供安全的 RAII 生命周期管理
 *
 * @note
 * Vulkan 中：
 * - `vk::Image` 表示「图像数据」
 * - `vk::ImageView` 表示「如何访问这块数据」
 *
 * @ingroup Render
 */

#pragma once

#include <cstdint>

#include <vk_mem_alloc.h>
#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Context;
class CommandPool;

/**
 * @enum ImageType
 * @brief Image 类型
 *
 * @details
 * 用于指定图像类型及其对应的 `vk::ImageViewType`。
 */
enum class ImageType {
    Tex2D,   ///< 普通 2D（`vk::ImageViewType::e2D`）
    TexCube, ///< 立方体贴图（`vk::ImageViewType::eCube`）
};

/**
 * @struct ImageCreateInfo
 * @brief Image 创建描述
 *
 * @details
 * 描述图像的尺寸、格式、用途以及 View 创建参数。
 *
 * @note
 * usage 和 aspectMask 必须匹配，否则会导致验证层错误。
 */
struct ImageCreateInfo {
    /// 图像宽度（像素）
    uint32_t width { 0 };

    /// 图像高度（像素）
    uint32_t height { 0 };

    /// 深度（3D 纹理使用）
    uint32_t depth { 1 };

    /// Mipmap 层数
    uint32_t mipLevels { 1 };

    /// 数组层数（Cube = 6）
    uint32_t arrayLayers { 1 };

    /// 图像格式（如 RGBA8、D32）
    vk::Format format { vk::Format::eR8G8B8A8Unorm };

    /// 图像类型（2D / Cube）
    ImageType type { ImageType::Tex2D };

    /**
     * @brief 使用方式（Usage Flags）
     *
     * @details
     * 常见组合：
     * - `eSampled`（作为纹理）
     * - `eColorAttachment`（作为颜色附件）
     * - `eDepthStencilAttachment`（作为深度）
     * - `eTransferSrc` / `eTransferDst`（拷贝 / mipmap）
     *
     * @warning 必须覆盖所有实际用途，否则 validation 报错
     */
    vk::ImageUsageFlags usage { vk::ImageUsageFlagBits::eSampled };

    /// 是否自动生成 Mipmap
    bool generateMipmaps { false };

    /**
     * @brief ImageView 的 aspect mask
     *
     * @note 必须与 format 匹配（如 depth format 须用 `eDepth`）
     */
    vk::ImageAspectFlags aspectMask { vk::ImageAspectFlagBits::eColor };
};

/**
 * @class Image
 * @brief Vulkan 图像资源封装（`vk::Image` + VMA + `vk::ImageView`）
 *
 * @details
 * 通过 RAII 自动管理生命周期；ImageView 须在 Image 存在期间有效。
 */
class Image {
public:
    Image() = default;

    /// 禁止拷贝（避免重复释放 GPU 资源）
    Image(const Image &) = delete;

    /// 支持移动语义
    Image(Image &&other) noexcept;

    Image &operator=(const Image &) = delete;
    Image &operator=(Image &&other) noexcept;

    /// 析构时自动释放 Image + View + VMA
    ~Image();

    /**
     * @brief 创建 Image（核心函数）
     *
     * @note 初始 layout 通常为 `eUndefined`
     */
    bool create(const Context &ctx, const ImageCreateInfo &info);

    /**
     * @brief 从文件加载 2D 纹理（RGBA8 → SRGB）
     *
     * @details
     * Staging → copy → 生成完整 mip 链 → layout 为 `eShaderReadOnlyOptimal`。
     */
    bool create_from_file(const Context &ctx, const char *filePath,
                          vk::Queue transferQueue, CommandPool &cmdPool);

    /**
     * @brief 创建深度附件 Image
     */
    bool create_depth_attachment(const Context &ctx, uint32_t width,
                                 uint32_t height);

    [[nodiscard]] vk::Image handle() const { return image_; }

    [[nodiscard]] vk::ImageView view() const { return imageView_; }

    [[nodiscard]] vk::Format format() const { return format_; }

    [[nodiscard]] uint32_t width() const { return width_; }

    [[nodiscard]] uint32_t height() const { return height_; }

    [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

    [[nodiscard]] bool is_valid() const { return static_cast<bool>(image_); }

private:
    void destroy_();

    vk::Device device_ {};
    VmaAllocator vma_allocator_ { nullptr };

    vk::Image image_ {};
    VmaAllocation allocation_ { nullptr };

    vk::ImageView imageView_ {};

    vk::Format format_ { vk::Format::eUndefined };

    uint32_t width_ { 0 };
    uint32_t height_ { 0 };
    uint32_t mipLevels_ { 0 };
};

} // namespace render
} // namespace lumen
