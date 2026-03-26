/**
 * @file image.hpp
 * @brief Vulkan Image 封装：2D 纹理、立方体纹理、Mipmap 与 ImageView
 *
 * @details
 * 本模块封装了 Vulkan 中 VkImage 的创建、内存分配（VMA）、
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
 * - 简化 VkImage + VkDeviceMemory/VMA 使用流程
 * - 将 Image 与 ImageView 绑定为一个资源单元
 * - 提供安全的 RAII 生命周期管理
 *
 * @note
 * Vulkan 中：
 * - VkImage 表示“图像数据”
 * - VkImageView 表示“如何访问这块数据”
 *
 * @ingroup Render
 */

#pragma once

#include <cstdint>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/**
 * @enum ImageType
 * @brief Image 类型
 *
 * @details
 * 用于指定 VkImage 的类型及其对应的 viewType。
 */
enum class ImageType {
    Tex2D,   ///< 普通 2D 纹理（VK_IMAGE_VIEW_TYPE_2D）
    TexCube, ///< 立方体贴图（VK_IMAGE_VIEW_TYPE_CUBE）
};

/**
 * @struct ImageCreateInfo
 * @brief Image 创建描述
 *
 * @details
 * 描述 VkImage 的尺寸、格式、用途以及 View 创建参数。
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
    VkFormat format { VK_FORMAT_R8G8B8A8_UNORM };

    /// 图像类型（2D / Cube）
    ImageType type { ImageType::Tex2D };

    /**
     * @brief 使用方式（Usage Flags）
     *
     * @details
     * 指定图像的用途，决定 GPU 如何访问它：
     *
     * 常见组合：
     * - VK_IMAGE_USAGE_SAMPLED_BIT（作为纹理）
     * - VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT（作为颜色附件）
     * - VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT（作为深度）
     * - VK_IMAGE_USAGE_TRANSFER_SRC/DST（用于拷贝 / mipmap）
     *
     * @warning 必须覆盖所有实际用途，否则 validation 报错
     */
    VkImageUsageFlags usage { VK_IMAGE_USAGE_SAMPLED_BIT };

    /// 是否自动生成 Mipmap
    bool generateMipmaps { false };

    /**
     * @brief ImageView 的 aspect mask
     *
     * @details
     * 指定 ImageView 访问图像的哪个部分：
     *
     * - VK_IMAGE_ASPECT_COLOR_BIT
     * - VK_IMAGE_ASPECT_DEPTH_BIT
     * - VK_IMAGE_ASPECT_STENCIL_BIT
     *
     * @note
     * 必须与 format 匹配（如 depth format 必须用 DEPTH_BIT）
     */
    VkImageAspectFlags aspectMask { VK_IMAGE_ASPECT_COLOR_BIT };
};

/**
 * @class Image
 * @brief Vulkan 图像资源封装（VkImage + VMA + VkImageView）
 *
 * @details
 * 该类封装：
 * - VkImage（图像资源）
 * - VmaAllocation（显存分配）
 * - VkImageView（访问接口）
 *
 * 并通过 RAII 自动管理生命周期。
 *
 * 资源关系：
 * @code
 * VkImage (数据)
 *    ↓
 * VkImageView (访问方式)
 * @endcode
 *
 * @note
 * ImageView 必须在 Image 存在期间有效。
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
     * @param ctx Vulkan 上下文（提供 device / allocator）
     * @param info 创建信息
     * @return 成功返回 true
     *
     * @details
     * 内部流程：
     * 1. 创建 VkImage
     * 2. 使用 VMA 分配显存
     * 3. 绑定内存
     * 4. 创建 VkImageView
     *
     * @note
     * 初始 layout 通常为 VK_IMAGE_LAYOUT_UNDEFINED
     */
    bool create(const Context &ctx, const ImageCreateInfo &info);

    /**
     * @brief 从文件加载纹理
     *
     * @param ctx Vulkan 上下文
     * @param filePath 图片路径
     * @return 成功返回 true
     *
     * @details
     * 典型流程：
     * - CPU 加载像素数据
     * - 创建 staging buffer
     * - 拷贝到 VkImage
     * - 转换 layout → SHADER_READ_ONLY_OPTIMAL
     */
    bool create_from_file(const Context &ctx, const char *filePath);

    /**
     * @brief 创建深度附件 Image
     *
     * @param ctx Vulkan 上下文
     * @param width 宽度
     * @param height 高度
     * @return 成功返回 true
     *
     * @details
     * 用于 RenderPass 的深度缓冲：
     * - usage = DEPTH_STENCIL_ATTACHMENT
     * - aspect = DEPTH
     * - layout = DEPTH_STENCIL_ATTACHMENT_OPTIMAL
     */
    bool create_depth_attachment(const Context &ctx, uint32_t width,
                                 uint32_t height);

    /// 获取 VkImage
    [[nodiscard]] VkImage handle() const { return image_; }

    /// 获取 VkImageView（用于 descriptor / framebuffer）
    [[nodiscard]] VkImageView view() const { return imageView_; }

    /// 图像格式
    [[nodiscard]] VkFormat format() const { return format_; }

    /// 宽度
    [[nodiscard]] uint32_t width() const { return width_; }

    /// 高度
    [[nodiscard]] uint32_t height() const { return height_; }

    /// Mipmap 层数
    [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return image_ != VK_NULL_HANDLE; }

private:
    /**
     * @brief 释放资源
     *
     * @details
     * 销毁顺序：
     * 1. vkDestroyImageView
     * 2. vmaDestroyImage
     *
     * @warning 必须在 VkDevice 销毁前调用
     */
    void destroy_();

    /// 逻辑设备
    VkDevice device_ { VK_NULL_HANDLE };

    /// VMA 分配器
    VmaAllocator vma_allocator_ { nullptr };

    /// 图像句柄
    VkImage image_ { VK_NULL_HANDLE };

    /// 内存分配
    VmaAllocation allocation_ { nullptr };

    /// 图像视图
    VkImageView imageView_ { VK_NULL_HANDLE };

    /// 格式缓存
    VkFormat format_ { VK_FORMAT_UNDEFINED };

    /// 宽度缓存
    uint32_t width_ { 0 };

    /// 高度缓存
    uint32_t height_ { 0 };

    /// mip 层数
    uint32_t mipLevels_ { 0 };
};

} // namespace render
} // namespace lumen
