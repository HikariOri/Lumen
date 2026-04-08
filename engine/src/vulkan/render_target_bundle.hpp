/**
 * @file render_target_bundle.hpp
 * @brief `RenderTarget` 组合为一块「画布」；按 `VkRenderPass` 懒创建并缓存
 * `VkFramebuffer`。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkan/render_target.hpp"

namespace vulkan {

/**
 * @brief 多个 `RenderTarget`（颜色 + 可选深度）对应各 `VkRenderPass` 各缓存一个
 * framebuffer。
 *
 * @note 在首次 `get_framebuffer` 之后若改附件列表，须先对同一 `VkDevice` 调用
 * `destroy(device)`，否则会泄漏旧的 `VkFramebuffer`。
 * @note 析构时若曾成功调用过 `get_framebuffer`，会用记录的 `VkDevice` 销毁缓存；
 * 须保证此时 `VkDevice` 仍有效（通常先于 `Context` / 设备销毁调用 `destroy`）。
 */
class RenderTargetBundle final {
public:
    RenderTargetBundle() = default;
    ~RenderTargetBundle();

    RenderTargetBundle(RenderTargetBundle &&other) noexcept = default;
    RenderTargetBundle &operator=(RenderTargetBundle &&other) noexcept =
        default;
    RenderTargetBundle(const RenderTargetBundle &) = delete;
    RenderTargetBundle &operator=(const RenderTargetBundle &) = delete;

    /**
     * @brief 追加颜色附件；须 `rt.is_valid()`，且宽高与已有约定一致。
     */
    [[nodiscard]] bool add_color_target(const RenderTarget &rt);

    /**
     * @brief 设置深度/模板附件；`!rt.is_valid()` 表示清除深度槽位。
     */
    [[nodiscard]] bool set_depth_target(const RenderTarget &rt);

    /// 占位：尺寸由 `add_color_target` / `set_depth_target` 写入。
    void auto_size() const noexcept {}

    /**
     * @brief 按 `render_pass` 的附件顺序（颜色 → 深度）懒创建 framebuffer。
     * @note `render_pass` 的 attachment 数量与顺序须与本 bundle 一致。
     */
    [[nodiscard]] std::expected<VkFramebuffer, std::string>
    get_framebuffer(VkDevice device, VkRenderPass render_pass);

    void destroy(VkDevice device) noexcept;

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<RenderTarget> &color_targets() const noexcept {
        return color_targets_;
    }
    [[nodiscard]] const RenderTarget &depth_target() const noexcept {
        return depth_target_;
    }
    [[nodiscard]] bool has_depth() const noexcept {
        return depth_target_.is_valid();
    }

    /// 任一颜色附件标记为交换链输出时，主循环应对应调用 `vkQueuePresentKHR`。
    [[nodiscard]] bool is_output_to_swapchain() const noexcept;

private:
    [[nodiscard]] bool set_extent_or_match_(std::uint32_t w,
                                            std::uint32_t h) noexcept;

    std::vector<RenderTarget> color_targets_;
    RenderTarget depth_target_ {};
    std::uint32_t width_ { 0 };
    std::uint32_t height_ { 0 };

    std::map<VkRenderPass, VkFramebuffer> framebuffers_;
    VkDevice framebuffer_device_ { VK_NULL_HANDLE };
};

} // namespace vulkan
