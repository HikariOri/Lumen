/**
 * @file texture.hpp
 * @brief 纹理封装：Image + Sampler，支持从文件或内存加载
 *
 * 高层纹理抽象，整合 VkImage、ImageView、Sampler，
 * 提供
 * create_from_file（PNG/JPG）、create_from_memory（渲染结果、程序化生成等） 与
 * Staging 上传、Mipmap 生成。
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
 * @brief 立方体单 mip 的 RGBA8 面数据（顺序 +X,-X,+Y,-Y,+Z,-Z），与
 * `create_cubemap_from_rgba8_mip_chain` 配套
 */
struct CubemapRgba8MipLevel {
    uint32_t face_size { 0 };
    std::array<const uint8_t *, 6> faces {};
};

/**
 * @class Texture
 * @brief 纹理封装：Image + Sampler
 *
 * 支持从文件加载（PNG/JPG）、从内存加载（渲染结果、程序化像素等）、
 * Staging 上传、Mipmap 生成，
 * 可直接用于 Descriptor 绑定（imageView + sampler）。
 */
class Texture {
public:
    Texture() = default;
    Texture(const Texture &) = delete;
    Texture(Texture &&other) noexcept;
    Texture &operator=(const Texture &) = delete;
    Texture &operator=(Texture &&other) noexcept;
    ~Texture();

    /**
     * @brief 从文件创建纹理（PNG/JPG）
     * @param format 颜色贴图用 `R8G8B8A8_SRGB`；**数据贴图**（glTF metallicRoughness、
     * normal、occlusion）须用 `R8G8B8A8_UNORM` 以保持通道线性
     */
    bool create_from_file(const Context &ctx, const char *filePath,
                          VkQueue transferQueue, CommandPool &cmdPool,
                          const SamplerConfig &samplerConfig = {},
                          VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 从 KTX / KTX2 文件创建纹理（libktx 解码为 RGBA8 后上传）
     */
    bool create_from_ktx_file(const Context &ctx, const char *filePath,
                              VkQueue transferQueue, CommandPool &cmdPool,
                              VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                              const SamplerConfig &samplerConfig = {});

    /**
     * @brief 从 6 面 RGBA8 像素创建立方体贴图（顺序 +X,-X,+Y,-Y,+Z,-Z），含
     * Mipmap
     *
     * 每面为 `faceSize×faceSize` 连续 RGBA8；用于天空盒、IBL 环境贴图等。
     */
    /**
     * @param format `SRGB` 表示 texel 为 sRGB 编码；**线性** 环境数据（如 CPU 积分）应使用
     * `VK_FORMAT_R8G8B8A8_UNORM`
     */
    bool create_cubemap_from_rgba8_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 从完整 mip 链创建立方体贴图（每级 6 面 RGBA8，顺序 +X,-X,+Y,-Y,+Z,-Z）
     *
     * `mip_levels[i].face_size` 须为 `mip_levels[0].face_size >> i`（右移 i 位，最小为
     * 1），不自动生成 mip。
     *
     * @param format 同 `create_cubemap_from_rgba8_faces`
     */
    bool create_cubemap_from_rgba8_mip_chain(
        const Context &ctx, const CubemapRgba8MipLevel *mip_levels,
        size_t mip_level_count, VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    /**
     * @brief 从 6 面 RGBA32F 像素创建立方体贴图（顺序 +X,-X,+Y,-Y,+Z,-Z），含
     * Mipmap
     *
     * 每面为 `faceSize×faceSize` 连续 RGBA 浮点（线性）；用于 HDR IBL 等。
     */
    bool create_cubemap_from_rgba32f_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        VkQueue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {});

    /**
     * @brief 从内存创建纹理（如渲染结果、程序化生成等）
     * @param ctx 已初始化的 Context
     * @param data 像素数据指针（行优先，紧挨存储）
     * @param imageSizeBytes 数据字节数（width * height * bytesPerPixel）
     * @param width 纹理宽度
     * @param height 纹理高度
     * @param transferQueue 用于传输的队列
     * @param cmdPool 命令池
     * @param format 像素格式，默认 R8G8B8A8_UNORM（渲染结果常用）
     * @param samplerConfig 可选 Sampler 配置
     * @param generateMipmaps 是否生成 Mipmap，默认 true
     * @return 成功返回 true
     */
    bool create_from_memory(const Context &ctx, const void *data,
                            size_t imageSizeBytes, uint32_t width,
                            uint32_t height, VkQueue transferQueue,
                            CommandPool &cmdPool,
                            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
                            const SamplerConfig &samplerConfig = {},
                            bool generateMipmaps = true);

    /**
     * @brief 从 ImageCreateInfo 创建纹理（自定义格式/尺寸）
     * @param ctx 已初始化的 Context
     * @param info Image 创建信息
     * @param samplerConfig 可选 Sampler 配置
     * @return 成功返回 true
     */
    bool create(const Context &ctx, const ImageCreateInfo &info,
                const SamplerConfig &samplerConfig = {});

    /// VkImageView（用于 Descriptor 写入）
    [[nodiscard]] VkImageView view() const { return imageView_; }

    /// VkSampler（用于 Descriptor 写入）
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

    /// VkImage 句柄
    [[nodiscard]] VkImage image() const { return image_; }

    /// 格式
    [[nodiscard]] VkFormat format() const { return format_; }

    /// 宽度
    [[nodiscard]] uint32_t width() const { return width_; }

    /// 高度
    [[nodiscard]] uint32_t height() const { return height_; }

    /// Mip 层数
    [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

    /// 用于 Descriptor 的 imageLayout
    [[nodiscard]] VkImageLayout descriptor_layout() const {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    /// 是否有效
    [[nodiscard]] bool is_valid() const {
        return image_ != VK_NULL_HANDLE && imageView_ != VK_NULL_HANDLE;
    }

private:
    void destroy_();
    bool create_sampler_(const Context &ctx, const SamplerConfig &config);
    bool create_from_pixels_(const Context &ctx, const void *data,
                             size_t imageSizeBytes, uint32_t width,
                             uint32_t height, VkFormat format,
                             VkQueue transferQueue, CommandPool &cmdPool,
                             bool doMipmaps);

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
