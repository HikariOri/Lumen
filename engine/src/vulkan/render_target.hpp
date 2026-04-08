/**
 * @file render_target.hpp
 * @brief 最小渲染目标描述：视图 + 格式 + 二维范围（不持有 `VkImage` / 内存）。
 */

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace vulkan {

class Image;

/**
 * @brief 单张渲染目标的视图与元数据，供 `RenderTargetBundle` 等组合使用。
 */
struct RenderTarget {
    VkImageView view { VK_NULL_HANDLE };
    VkFormat format { VK_FORMAT_UNDEFINED };
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };

    [[nodiscard]] bool is_valid() const noexcept {
        return view != VK_NULL_HANDLE;
    }
};

/**
 * @brief 由交换链等外部资源构造（无 `Image` 封装时）。
 */
[[nodiscard]] inline RenderTarget render_target_from_view(
    const VkImageView view, const VkFormat fmt, const std::uint32_t w,
    const std::uint32_t h) noexcept {
    return RenderTarget{ .view = view,
                         .format = fmt,
                         .width = w,
                         .height = h };
}

/**
 * @brief 从已带 `view` 的 `vulkan::Image` 填表；无效或缺 view 时返回 `!is_valid()`。
 */
[[nodiscard]] RenderTarget to_render_target(const Image &image);

} // namespace vulkan
