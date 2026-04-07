#pragma once

#include "rhi/buffer.hpp"
#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rhi {

enum class Format : std::uint8_t {
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
};

enum class ImageUsage : std::uint32_t {
    Sampled = 1u << 0,
    ColorAttachment = 1u << 1,
    DepthStencil = 1u << 2,
    Storage = 1u << 3,
    TransferSrc = 1u << 4,
    TransferDst = 1u << 5,
};

constexpr ImageUsage operator|(ImageUsage a, ImageUsage b) noexcept {
    using U = std::underlying_type_t<ImageUsage>;
    return static_cast<ImageUsage>(static_cast<U>(a) | static_cast<U>(b));
}

constexpr ImageUsage operator&(ImageUsage a, ImageUsage b) noexcept {
    using U = std::underlying_type_t<ImageUsage>;
    return static_cast<ImageUsage>(static_cast<U>(a) & static_cast<U>(b));
}

constexpr ImageUsage &operator|=(ImageUsage &a, ImageUsage b) noexcept {
    a = a | b;
    return a;
}

struct ImageDesc {
    std::uint32_t width {};
    std::uint32_t height {};
    std::uint32_t mip_levels { 1 };
    Format format { Format::R8G8B8A8_UNORM };
    ImageUsage usage {};
    MemoryUsage memory { MemoryUsage::GPU_ONLY };
    const void *data = nullptr;
    /// 源像素每行字节数；0 表示紧密排列 `width * bytes_per_pixel(format)`
    std::size_t data_row_pitch {};
};

struct ImageHandle {
    std::uint32_t id {};
};

struct ImageResource {
    vk::Image image;
    vk::ImageView view;
    VmaAllocation allocation {};
    vk::Format vk_format { vk::Format::eUndefined };
    vk::Extent3D extent {};
    std::uint32_t mip_levels { 1 };
};

[[nodiscard]] constexpr bool is_valid(ImageHandle h) noexcept {
    return h.id != 0;
}

[[nodiscard]] vk::Format to_vk(Format f) noexcept;
[[nodiscard]] std::uint32_t format_texel_block_bytes(Format f) noexcept;
[[nodiscard]] vk::ImageUsageFlags to_vk(ImageUsage u) noexcept;

} // namespace rhi
