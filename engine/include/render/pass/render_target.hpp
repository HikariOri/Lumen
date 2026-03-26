/**
 * @file render_target.hpp
 * @brief 渲染目标抽象层：Offscreen Render Target 与 Swapchain Render Target
 *
 * 本模块定义渲染管线中的“输出目标（Render Target）抽象层”，用于统一管理：
 *
 * 1. 离屏渲染（Offscreen Rendering）
 *    - 渲染到 VkImage（非屏幕呈现）
 *    - 用于：
 *        • 视口（Editor Viewport）
 *        • 后处理链（Post Processing）
 *        • Shadow Map / GBuffer
 *        • ImGui 纹理预览
 *
 * 2. 屏幕渲染（Swapchain Rendering）
 *    - 渲染到 VkSwapchainImage
 *    - 用于最终 Present 到窗口系统
 *
 * 核心设计原则：
 * --------------------------------------
 * - RenderTarget ≠ Swapchain
 * - RenderPass 不关心输出来源（image / swapchain）
 * - Framebuffer 负责绑定最终 attachment
 * - Layout transition 由 RenderGraph 或上层控制
 *
 * 渲染流程示意：
 * --------------------------------------
 * Scene → OffscreenRenderTarget → (ImGui / PostProcess) →
 * SwapchainRenderTarget → vkQueuePresentKHR
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

/**
 * @brief 离屏渲染目标配置结构体
 *
 * 用于定义 Offscreen Render Target 的创建参数。
 *
 * 注意 Vulkan 约束：
 * --------------------------------------
 * - format 必须与 RenderPass attachment format 匹配
 * - colorFinalLayout 决定“是否可被 shader 采样”
 * - depth attachment 通常为 VK_FORMAT_D32_SFLOAT
 */
struct OffscreenRenderTargetConfig {
    /// 渲染分辨率（离屏 framebuffer 尺寸）
    uint32_t width { 0 };
    uint32_t height { 0 };

    /// 颜色附件格式（影响 RenderPass / Image / Framebuffer）
    VkFormat format { VK_FORMAT_R8G8B8A8_SRGB };

    /// 是否创建 depth buffer（用于 3D 场景）
    bool useDepth { true };

    /**
     * @brief 颜色附件最终 Layout
     *
     * 常见取值：
     * --------------------------------------
     * - VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
     *     → ImGui / PostProcess 采样
     *
     * - VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
     *     → 仅用于渲染，不采样
     *
     * @note 注意：
     * 如果要用于 ImGui::Image 或 shader texture sampler，
     * 必须保证 transition 到该 layout
     */
    VkImageLayout colorFinalLayout { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};

/**
 * @class OffscreenRenderTarget
 * @brief 离屏渲染目标（Framebuffer + RenderPass + Image 封装）
 *
 * 该类封装完整 Offscreen Rendering 所需资源：
 *
 * 包含：
 * --------------------------------------
 * - VkImage（color / depth）
 * - VkImageView
 * - VkFramebuffer
 * - VkRenderPass
 *
 * 使用场景：
 * --------------------------------------
 * - Editor 视口渲染
 * - 后处理输入源
 * - HDR/IBL 计算目标
 * - UI texture preview
 *
 * 生命周期约束：
 * --------------------------------------
 * - resize() 会销毁并重建 GPU resources
 * - 必须在 device idle 或同步安全状态调用
 */
class OffscreenRenderTarget {
public:
    // 可移动不可复制
    OffscreenRenderTarget() = default;
    OffscreenRenderTarget(const OffscreenRenderTarget &) = delete;
    OffscreenRenderTarget(OffscreenRenderTarget &&other) noexcept;
    OffscreenRenderTarget &operator=(const OffscreenRenderTarget &) = delete;
    OffscreenRenderTarget &operator=(OffscreenRenderTarget &&other) noexcept;
    ~OffscreenRenderTarget();

    /**
     * @brief 创建离屏渲染目标
     *
     * 创建流程：
     * --------------------------------------
     * 1. 创建 color image
     * 2. 创建 depth image（可选）
     * 3. 创建 image views
     * 4. 创建 render pass
     * 5. 创建 framebuffer
     *
     * @param ctx Vulkan 上下文（device / allocator）
     * @param config 离屏目标配置
     */
    bool create(const Context &ctx, const OffscreenRenderTargetConfig &config);

    /**
     * @brief 动态调整渲染目标尺寸（窗口 resize）
     *
     * Vulkan 注意事项：
     * --------------------------------------
     * - 必须等待 GPU idle 或确保旧 framebuffer 不再使用
     * - swapchain resize 类似逻辑，但这里是 offscreen image resize
     *
     * @param width 新宽度
     * @param height 新高度
     */
    bool resize(const Context &ctx, uint32_t width, uint32_t height);

    /**
     * @brief 获取颜色 attachment ImageView
     *
     * 用途：
     * --------------------------------------
     * - ImGui::Image
     * - shader sampler binding
     */
    [[nodiscard]] VkImageView color_view() const;

    /**
     * @brief 获取底层 Image（RenderGraph 使用）
     */
    [[nodiscard]] const Image &color_image() const { return colorImage_; }

    /**
     * @brief 获取 depth buffer Image
     */
    [[nodiscard]] const Image &depth_image() const { return depthImage_; }

    /**
     * @brief 采样前目标 layout
     */
    [[nodiscard]] VkImageLayout color_sample_layout() const {
        return config_.colorFinalLayout;
    }

    /**
     * @brief Framebuffer（通常单层 or 多层）
     */
    [[nodiscard]] VkFramebuffer framebuffer() const;

    /**
     * @brief RenderPass（定义 attachment / subpass / dependency）
     */
    [[nodiscard]] VkRenderPass render_pass() const;

    /**
     * @brief 当前渲染分辨率
     */
    [[nodiscard]] VkExtent2D extent() const { return { width_, height_ }; }

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] VkFormat format() const { return config_.format; }

    /**
     * @brief 是否有效（资源已创建）
     */
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
 * @brief Swapchain 渲染目标（最终呈现层）
 *
 * 与 Offscreen RenderTarget 的关键区别：
 * --------------------------------------
 * - Offscreen → 用户管理 VkImage
 * - Swapchain → Vulkan 管理 present image
 *
 * 该类仅负责：
 * --------------------------------------
 * - 绑定 swapchain images
 * - 提供 framebuffer 索引访问
 * - 提供 extent / format
 *
 * Present 流程：
 * --------------------------------------
 * render → swapchain image → vkQueuePresentKHR
 */
class SwapchainRenderTarget {
public:
    SwapchainRenderTarget() = default;

    /**
     * @brief 绑定 Swapchain 与 Framebuffer 列表
     *
     * @param swapchain Vulkan swapchain wrapper
     * @param framebuffers 每个 swapchain image 对应 framebuffer
     */
    void bind(Swapchain *swapchain, Framebuffer *framebuffers);

    /**
     * @brief 获取指定帧 framebuffer
     *
     * @param index swapchain image index
     */
    [[nodiscard]] VkFramebuffer framebuffer(uint32_t index) const;

    /**
     * @brief swapchain 分辨率
     */
    [[nodiscard]] VkExtent2D extent() const;

    /**
     * @brief swapchain format
     */
    [[nodiscard]] VkFormat format() const;

    /**
     * @brief image 数量（双/三缓冲）
     */
    [[nodiscard]] uint32_t image_count() const;

    /**
     * @brief 是否有效
     */
    [[nodiscard]] bool is_valid() const;

private:
    Swapchain *swapchain_ { nullptr };
    Framebuffer *framebuffers_ { nullptr };
};

} // namespace render
} // namespace lumen
