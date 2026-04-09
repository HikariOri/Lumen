/**
 * @file rg_types.hpp
 * @brief `RenderGraph` 资源句柄与访问枚举（与 `vulkan::RenderTarget` /
 *        `RenderTargetBundle` 配合）。
 */

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace vulkan {

enum class RgResourceType : std::uint8_t {
    Texture,
    Depth,
    Swapchain,
};

enum class RgAccess : std::uint8_t {
    Read,
    Write,
};

struct RgResourceHandle {
    std::uint32_t id { UINT32_MAX };

    [[nodiscard]] bool is_valid() const noexcept {
        return id != UINT32_MAX;
    }
};

/**
 * @brief 调度器为每槽维护的图像布局（与 `RgResources::layout` / `set_layout` 同步）。
 */
struct RgResourceState {
    VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
};

} // namespace vulkan
