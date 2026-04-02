/**
 * @file texture.hpp
 * @brief Vulkan 纹理封装（Image + ImageView + Sampler）
 *
 * 提供统一的纹理抽象，封装 `vk::Image` / `vk::ImageView` / `vk::Sampler`，
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
#include "render/vulkan.hpp"

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
 */
struct CubemapRgba8MipLevel {
    uint32_t face_size { 0 };
    std::array<const uint8_t *, 6> faces {};
};

/**
 * @class Texture
 * @brief GPU 纹理对象（Image + View + Sampler）
 */
class Texture {
public:
    Texture() = default;
    Texture(const Texture &) = delete;
    Texture(Texture &&other) noexcept;
    Texture &operator=(const Texture &) = delete;
    Texture &operator=(Texture &&other) noexcept;

    ~Texture();

    bool create_from_file(const Context &ctx, const char *filePath,
                          vk::Queue transferQueue, CommandPool &cmdPool,
                          const SamplerConfig &samplerConfig = {},
                          vk::Format format = vk::Format::eR8G8B8A8Srgb);

    bool create_from_ktx_file(const Context &ctx, const char *filePath,
                              vk::Queue transferQueue, CommandPool &cmdPool,
                              vk::Format format = vk::Format::eR8G8B8A8Srgb,
                              const SamplerConfig &samplerConfig = {});

    bool create_cubemap_from_rgba8_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        vk::Queue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        vk::Format format = vk::Format::eR8G8B8A8Srgb);

    bool create_cubemap_from_rgba8_mip_chain(
        const Context &ctx, const CubemapRgba8MipLevel *mip_levels,
        size_t mip_level_count, vk::Queue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {},
        vk::Format format = vk::Format::eR8G8B8A8Srgb);

    bool create_cubemap_from_rgba32f_faces(
        const Context &ctx, const void *const faces[6], uint32_t faceSize,
        vk::Queue transferQueue, CommandPool &cmdPool,
        const SamplerConfig &samplerConfig = {});

    bool create_from_memory(const Context &ctx, const void *data,
                            size_t imageSizeBytes, uint32_t width,
                            uint32_t height, vk::Queue transferQueue,
                            CommandPool &cmdPool,
                            vk::Format format = vk::Format::eR8G8B8A8Unorm,
                            const SamplerConfig &samplerConfig = {},
                            bool generateMipmaps = true);

    bool create(const Context &ctx, const ImageCreateInfo &info,
                const SamplerConfig &samplerConfig = {});

    [[nodiscard]] vk::ImageView view() const { return imageView_; }

    [[nodiscard]] vk::Sampler sampler() const { return sampler_; }

    [[nodiscard]] vk::Image image() const { return image_; }

    [[nodiscard]] vk::Format format() const { return format_; }

    [[nodiscard]] uint32_t width() const { return width_; }

    [[nodiscard]] uint32_t height() const { return height_; }

    [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

    [[nodiscard]] vk::ImageLayout descriptor_layout() const {
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    [[nodiscard]] bool is_valid() const {
        return static_cast<bool>(image_) && static_cast<bool>(imageView_);
    }

private:
    void destroy_();

    bool create_sampler_(const Context &ctx, const SamplerConfig &config);

    bool create_from_pixels_(const Context &ctx, const void *data,
                             size_t imageSizeBytes, uint32_t width,
                             uint32_t height, vk::Format format,
                             vk::Queue transferQueue, CommandPool &cmdPool,
                             bool doMipmaps);

    vk::Device device_ {};
    VmaAllocator vma_allocator_ { nullptr };

    vk::Image image_ {};
    VmaAllocation allocation_ { nullptr };

    vk::ImageView imageView_ {};
    vk::Sampler sampler_ {};

    vk::Format format_ { vk::Format::eUndefined };

    uint32_t width_ { 0 };
    uint32_t height_ { 0 };
    uint32_t mipLevels_ { 0 };
};

} // namespace render
} // namespace lumen
