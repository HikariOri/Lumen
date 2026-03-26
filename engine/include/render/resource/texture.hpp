/**
 * @file texture.hpp
 * @brief Vulkan 纹理封装（Image + ImageView + Sampler）
 *
 * 提供统一的纹理抽象，封装 VkImage / VkImageView / VkSampler，
 * 支持从文件（PNG/JPG/KTX/KTX2）或内存创建纹理，
 * 内部完成 Staging Buffer 上传、Layout 转换以及 Mipmap 生成。
 *
 * @note
 * - 该类负责 GPU 资源生命周期（RAII）
 * - 可直接用于 Descriptor（combined image sampler）
 *
 * @warning
 * - 纹理格式（SRGB vs UNORM）必须正确选择，否则会导致光照错误
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "render/resource/sampler.hpp"

namespace lumen {
namespace render {

class Context;
class CommandPool;
struct ImageCreateInfo;

/**
 * @struct CubemapRgba8MipLevel
 * @brief 立方体贴图单级 mip 数据（RGBA8）
 *
 * 每个 mip level 包含 6 个面（顺序固定）：
 * +X, -X, +Y, -Y, +Z, -Z
 *
 * @note
 * - 每个 face 为连续 RGBA8 数据
 * - face_size = 宽度 = 高度
 */
struct CubemapRgba8MipLevel {
    uint32_t face_size { 0 };                ///< 单面尺寸（宽 = 高）
    std::array<const uint8_t *, 6> faces {}; ///< 六个面的像素数据
};

/**
 * @class Texture
 * @brief GPU 纹理对象（Image + View + Sampler）
 *
 * 提供统一接口创建：
 * - 2D 纹理（文件 / 内存）
 * - Cubemap（RGBA8 / RGBA32F / mip chain）
 *
 * 内部负责：
 * - VkImage 创建与内存分配（VMA）
 * - Staging 上传
 * - Image Layout 转换
 * - Mipmap 生成（可选）
 *
 * @note
 * 该类是“只读纹理”（Shader Read Only），默认 layout：
 * VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
 */
class Texture {
public:
    /// 可移动不可复制
    Texture() = default;
    Texture(const Texture &) = delete;
    Texture(Texture &&other) noexcept;
    Texture &operator=(const Texture &) = delete;
    Texture &operator=(Texture &&other) noexcept;

    /**
     * @brief 析构函数（释放 GPU 资源）
     */
    ~Texture();

    /**
     * @brief 从图片文件创建纹理（PNG / JPG）
     *
     * @param ctx Vulkan 上下文
     * @param filePath 文件路径
     * @param transferQueue 传输队列
     * @param cmdPool 命令池
     * @param samplerConfig Sampler 配置
     * @param format 图像格式
     *
     * @return 是否创建成功
     *
     * @note
     * - 颜色贴图使用 VK_FORMAT_R8G8B8A8_SRGB
     * - 数据贴图（normal / metallicRoughness / AO）必须使用 UNORM
     */
    bool create_from_file(const Context &ctx, const char *filePath,
                          VkQueue transferQueue, CommandPool &cmdPool,
                          const SamplerConfig &samplerConfig = {},
                          VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 从 KTX / KTX2 文件创建纹理
     *
     * @details
     * 使用 libktx 解码为 RGBA8 后上传 GPU
     */
    bool create_from_ktx_file(const Context &ctx, const char *filePath,
                              VkQueue transferQueue, CommandPool &cmdPool,
                              VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                              const SamplerConfig &samplerConfig = {});

    /**
     * @brief 创建立方体贴图（RGBA8，自动生成 mip）
     *
     * @param faces 六个面的数据（+X,-X,+Y,-Y,+Z,-Z）
     * @param faceSize 面尺寸
     *
     * @note
     * - SRGB：用于天空盒（LDR）
     * - UNORM：用于线性数据（如预计算 IBL）
     */
    bool create_cubemap_from_rgba8_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 创建立方体贴图（完整 mip chain）
     *
     * @param mip_levels 每级 mip 数据
     * @param mip_level_count mip 数量
     *
     * @pre
     * mip_levels[i].face_size == mip_levels[0].face_size >> i
     *
     * @warning
     * 不会自动生成 mip，必须提供完整链
     */
    bool create_cubemap_from_rgba8_mip_chain(
        const Context &ctx, const CubemapRgba8MipLevel *mip_levels,
        size_t mip_level_count, VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 创建 HDR Cubemap（RGBA32F）
     *
     * @note
     * 用于 IBL / 环境光照（线性空间）
     */
    bool create_cubemap_from_rgba32f_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {});

    /**
     * @brief 从内存创建 2D 纹理
     *
     * @param data 像素数据（行优先）
     * @param imageSizeBytes 数据大小
     * @param width 宽
     * @param height 高
     * @param generateMipmaps 是否生成 mip
     *
     * @return 是否成功
     */
    bool create_from_memory(const Context &ctx, const void *data,
                            size_t imageSizeBytes, uint32_t width,
                            uint32_t height, VkQueue transferQueue,
                            CommandPool &cmdPool,
                            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
                            const SamplerConfig &samplerConfig = {},
                            bool generateMipmaps = true);

    /**
     * @brief 通过自定义 ImageCreateInfo 创建纹理
     */
    bool create(const Context &ctx, const ImageCreateInfo &info,
                const SamplerConfig &samplerConfig = {});

    /// @brief 获取 ImageView（用于 Descriptor）
    [[nodiscard]] VkImageView view() const { return imageView_; }

    /// @brief 获取 Sampler
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

    /// @brief 获取 VkImage
    [[nodiscard]] VkImage image() const { return image_; }

    /// @brief 获取格式
    [[nodiscard]] VkFormat format() const { return format_; }

    /// @brief 宽度
    [[nodiscard]] uint32_t width() const { return width_; }

    /// @brief 高度
    [[nodiscard]] uint32_t height() const { return height_; }

    /// @brief mip 层数
    [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

    /// @brief Descriptor 使用的 layout
    [[nodiscard]] VkImageLayout descriptor_layout() const {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    /// @brief 是否有效
    [[nodiscard]] bool is_valid() const {
        return image_ != VK_NULL_HANDLE && imageView_ != VK_NULL_HANDLE;
    }

private:
    void destroy_();

    /**
     * @brief 创建 Sampler
     */
    bool create_sampler_(const Context &ctx, const SamplerConfig &config);

    /**
     * @brief 内部像素上传实现
     */
    bool create_from_pixels_(const Context &ctx, const void *data,
                             size_t imageSizeBytes, uint32_t width,
                             uint32_t height, VkFormat format,
                             VkQueue transferQueue, CommandPool &cmdPool,
                             bool doMipmaps);

private:
    VkDevice device_ { VK_NULL_HANDLE };
    VmaAllocator vma_allocator_ { nullptr };

    VkImage image_ { VK_NULL_HANDLE };
    VmaAllocation allocation_ { nullptr };

    VkImageView imageView_ { VK_NULL_HANDLE };
    VkSampler sampler_ { VK_NULL_HANDLE };

    VkFormat format_ { VK_FORMAT_UNDEFINED };

    uint32_t width_ { 0 };
    uint32_t height_ { 0 };
    uint32_t mipLevels_ { 0 };
};

} // namespace render
} // namespace lumen
