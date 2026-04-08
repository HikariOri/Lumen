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
    /// 颜色附件来自交换链时置 true，供 `RenderPass` / 主循环判断是否走 present。
    bool is_swapchain_target { false };

    [[nodiscard]] bool is_valid() const noexcept {
        return view != VK_NULL_HANDLE;
    }
};

/**
 * @brief 由外部 `VkImageView` 构造（离屏等）；`is_swapchain_target` 为 true
 * 表示交换链图像。
 */
[[nodiscard]] inline RenderTarget render_target_from_view(
    const VkImageView view, const VkFormat fmt, const std::uint32_t w,
    const std::uint32_t h, const bool is_swapchain_target = false) noexcept {
    return RenderTarget{ .view = view,
                         .format = fmt,
                         .width = w,
                         .height = h,
                         .is_swapchain_target = is_swapchain_target };
}

/// 交换链 swapchain 图像视图（`is_swapchain_target == true`）。
[[nodiscard]] inline RenderTarget render_target_from_swapchain_view(
    const VkImageView view, const VkFormat fmt, const std::uint32_t w,
    const std::uint32_t h) noexcept {
    return render_target_from_view(view, fmt, w, h, true);
}

/**
 * @brief 从已带 `view` 的 `vulkan::Image` 填表；无效或缺 view 时返回 `!is_valid()`。
 */
[[nodiscard]] RenderTarget to_render_target(const Image &image);

} // namespace vulkan
