/**
 * @file render_pass.hpp
 * @brief 由 `RenderTargetBundle` 描述构建的简化图形 `VkRenderPass`（多颜色 +
 * 可选深度）。
 */

#pragma once

#include <expected>
#include <string>

#include <vulkan/vulkan.h>

#include "vulkan/render_target_bundle.hpp"

namespace vulkan {

/**
 * @brief 单 subpass、`LOAD_OP_CLEAR` / `STORE_OP_STORE`；深度为
 * `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。
 *
 * @param color_final_present_src 为 true 时，所有颜色附件的 `finalLayout` 为
 * `PRESENT_SRC_KHR`（交换链呈现）；否则为 `COLOR_ATTACHMENT_OPTIMAL`（离屏）。
 *
 * @overload `create(device, bundle)` 使用 `bundle.is_output_to_swapchain()` 作为
 * `color_final_present_src`。
 *
 * @note 与 `RenderTargetBundle::get_framebuffer(device, vk_render_pass())`
 * 配套使用； 附件顺序须与 bundle 一致（颜色 → 深度）。
 */
class RenderPass final {
public:
    [[nodiscard]] static std::expected<RenderPass, std::string>
    create(VkDevice device, const RenderTargetBundle &bundle);

    [[nodiscard]] static std::expected<RenderPass, std::string>
    create(VkDevice device, const RenderTargetBundle &bundle,
           bool color_final_present_src);

    ~RenderPass();

    RenderPass(RenderPass &&other) noexcept;
    RenderPass &operator=(RenderPass &&other) noexcept;

    RenderPass(const RenderPass &) = delete;
    RenderPass &operator=(const RenderPass &) = delete;

    [[nodiscard]] VkRenderPass vk_render_pass() const noexcept {
        return vk_render_pass_;
    }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] bool is_valid() const noexcept {
        return vk_render_pass_ != VK_NULL_HANDLE;
    }

private:
    explicit RenderPass(VkDevice device, VkRenderPass render_pass) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkRenderPass vk_render_pass_ { VK_NULL_HANDLE };
};

} // namespace vulkan
