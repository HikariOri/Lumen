/**
 * @file render_target.hpp
 * @brief 渲染目标抽象：离屏目标与 Swapchain 目标
 *
 * 流程：Scene → OffscreenRenderTarget → ImGui 采样 → SwapchainRenderTarget →
 * Present 离屏渲染不需要 swapchain；显示到屏幕时必须用 swapchain。
 */

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/resource/image.hpp"
#include "render/swapchain.hpp"

namespace lumen {
namespace render {

/// 离屏渲染目标配置
struct OffscreenRenderTargetConfig {
    uint32_t width { 0 };
    uint32_t height { 0 };
    VkFormat format { VK_FORMAT_R8G8B8A8_SRGB };
    bool useDepth { true };
    /// 颜色附件 finalLayout，用于 ImGui 采样时用 SHADER_READ_ONLY_OPTIMAL
    VkImageLayout colorFinalLayout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};

/**
 * @class OffscreenRenderTarget
 * @brief 离屏渲染目标：自有 VkImage，用于场景/视口渲染，可被 ImGui 采样
 *
 * 不依赖 swapchain。创建颜色+深度 Image、Framebuffer、RenderPass。
 */
class OffscreenRenderTarget {
public:
    OffscreenRenderTarget() = default;
    OffscreenRenderTarget(const OffscreenRenderTarget &) = delete;
    OffscreenRenderTarget(OffscreenRenderTarget &&other) noexcept;
    OffscreenRenderTarget &operator=(const OffscreenRenderTarget &) = delete;
    OffscreenRenderTarget &operator=(OffscreenRenderTarget &&other) noexcept;
    ~OffscreenRenderTarget();

    /**
     * @brief 创建离屏目标
     */
    bool create(const Context &ctx, const OffscreenRenderTargetConfig &config);

    /**
     * @brief 调整尺寸（窗口 resize 时调用）
     * @return 成功返回 true
     */
    bool resize(const Context &ctx, uint32_t width, uint32_t height);

    /// 颜色附件 ImageView（供 ImGui::Image 等采样）
    [[nodiscard]] VkImageView color_view() const;

    /// 颜色附件 Image（供 RenderGraph 使用）
    [[nodiscard]] const Image& color_image() const { return colorImage_; }
    /// 深度附件 Image（供 RenderGraph 使用）
    [[nodiscard]] const Image& depth_image() const { return depthImage_; }

    /// 颜色附件的最终 Layout（采样前需 transition 到此）
    [[nodiscard]] VkImageLayout color_sample_layout() const {
        return config_.colorFinalLayout;
    }

    /// Framebuffer（索引 0）
    [[nodiscard]] VkFramebuffer framebuffer() const;

    /// RenderPass 句柄
    [[nodiscard]] VkRenderPass render_pass() const;

    /// 分辨率
    [[nodiscard]] VkExtent2D extent() const { return { width_, height_ }; }

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] VkFormat format() const { return config_.format; }

    [[nodiscard]] bool is_valid() const { return framebuffer_.count() > 0; }

private:
    void destroy_();
    bool create_internal_(const Context &ctx);

    const Context *ctx_ { nullptr };
    OffscreenRenderTargetConfig config_ {};
    uint32_t width_ { 0 };
    uint32_t height_ { 0 };

    Image colorImage_ {};
    Image depthImage_ {};
    RenderPass renderPass_ {};
    Framebuffer framebuffer_ {};
};

/**
 * @class SwapchainRenderTarget
 * @brief Swapchain 渲染目标：最终显示到屏幕的缓冲
 *
 * 包装 Swapchain + Framebuffer，提供按索引获取 framebuffer。
 * Present 必须使用 swapchain。
 */
class SwapchainRenderTarget {
public:
    SwapchainRenderTarget() = default;

    void bind(Swapchain *swapchain, Framebuffer *framebuffers);

    /// 获取指定帧的 Framebuffer
    [[nodiscard]] VkFramebuffer framebuffer(uint32_t index) const;

    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] VkFormat format() const;
    [[nodiscard]] uint32_t image_count() const;

    [[nodiscard]] bool is_valid() const;

private:
    Swapchain *swapchain_ { nullptr };
    Framebuffer *framebuffers_ { nullptr };
};

} // namespace render
} // namespace lumen
