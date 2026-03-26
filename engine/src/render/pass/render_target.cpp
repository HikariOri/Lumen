/**
 * @file render_target.cpp
 * @brief OffscreenRenderTarget 与 SwapchainRenderTarget 实现
 *
 * 本文件实现渲染目标抽象层的核心逻辑，包括：
 *
 * 1. OffscreenRenderTarget
 *    - 管理自建 VkImage / VkImageView / VkFramebuffer
 *    - 支持动态 resize（重建 GPU 资源）
 *    - 用于离屏渲染（Editor / PostProcess / Shadow / GBuffer）
 *
 * 2. SwapchainRenderTarget
 *    - 封装 VkSwapchainKHR 对应的 framebuffer 映射
 *    - 用于最终 present 到屏幕
 *
 * Vulkan 资源生命周期关键点：
 * --------------------------------------
 * - Image / Framebuffer / RenderPass 属于 device-local resource
 * - resize 会触发“全量重建”
 * - 必须保证 GPU 不再使用旧 framebuffer（外部需同步）
 */

#include "render/pass/render_target.hpp"

#include "core/logger.hpp"
#include "render/context.hpp"

namespace lumen::render {

// ============================================================
// OffscreenRenderTarget
// ============================================================

/**
 * @brief 创建离屏渲染目标
 *
 * 初始化流程（Vulkan resource graph）：
 * --------------------------------------
 * 1. 验证输入尺寸合法性
 * 2. 保存 Context 与配置
 * 3. 调用 create_internal_ 构建 GPU 资源：
 *    - VkRenderPass
 *    - VkImage (color / depth)
 *    - VkImageView
 *    - VkFramebuffer
 *
 * @note 注意：
 * - create 不是幂等的（重复调用会重建资源）
 * - 必须保证旧资源不再被 GPU 使用
 */
bool OffscreenRenderTarget::create(const Context &ctx,
                                   const OffscreenRenderTargetConfig &config) {
    if (config.width == 0 || config.height == 0) {
        LUMEN_LOG_ERROR("OffscreenRenderTarget: 无效尺寸 {}x{}", config.width,
                        config.height);
        return false;
    }

    ctx_ = &ctx;
    config_ = config;
    width_ = config.width;
    height_ = config.height;

    return create_internal_(ctx);
}

/**
 * @brief 调整离屏渲染目标大小
 *
 * 该操作等价于“销毁并重建 GPU framebuffer 资源”，包括：
 * --------------------------------------
 * - VkImage (color/depth)
 * - VkImageView
 * - VkFramebuffer
 *
 * RenderPass 通常复用（除非 format/layout 改变）
 *
 * ⚠ Vulkan 关键风险：
 * --------------------------------------
 * - resize 必须在 GPU idle 或 frame fence 保护下执行
 * - 否则可能产生 use-after-free GPU crash
 */
bool OffscreenRenderTarget::resize(const Context &ctx, uint32_t width,
                                   uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }

    width_ = width;
    height_ = height;
    config_.width = width;
    config_.height = height;

    return create_internal_(ctx);
}

/**
 * @brief 获取 color attachment ImageView
 *
 * 用途：
 * --------------------------------------
 * - ImGui texture preview
 * - shader sampler input
 * - post-process input attachment
 */
VkImageView OffscreenRenderTarget::color_view() const {
    return colorImage_.view();
}

/**
 * @brief 获取 framebuffer（Offscreen 通常单 framebuffer）
 */
VkFramebuffer OffscreenRenderTarget::framebuffer() const {
    return framebuffer_.get(0);
}

/**
 * @brief 获取 RenderPass 句柄
 *
 * RenderPass 定义：
 * --------------------------------------
 * - attachment format
 * - load/store ops
 * - subpass dependency
 * - final layout transition
 */
VkRenderPass OffscreenRenderTarget::render_pass() const {
    return renderPass_.handle();
}

/**
 * @brief 销毁所有 GPU 资源
 *
 * 销毁顺序（必须严格）：
 * --------------------------------------
 * Framebuffer → ImageView → Image → RenderPass
 *
 * @note 注意：
 * RenderPass 在 Vulkan 中可复用，但本实现随 target 生命周期绑定
 */
void OffscreenRenderTarget::destroy_() {
    framebuffer_.destroy();
    framebuffer_ = Framebuffer();

    renderPass_ = RenderPass();

    depthImage_ = Image();
    colorImage_ = Image();
}

/**
 * @brief 内部资源创建函数（核心 Vulkan 构建逻辑）
 *
 * 执行顺序：
 * --------------------------------------
 * 1. 若 framebuffer 已存在 → destroy（resize path）
 * 2. 创建 / 复用 RenderPass
 * 3. 创建 Color Image
 * 4. 创建 Depth Image（可选）
 * 5. 创建 Framebuffer 并绑定 attachments
 *
 * ⚠ 设计说明：
 * - RenderPass 只在 format/layout 不变时复用
 * - Image 是实际 GPU memory owner（VMA allocation）
 */
bool OffscreenRenderTarget::create_internal_(const Context &ctx) {
    // Resize path：释放旧 framebuffer
    if (framebuffer_.count() > 0) {
        framebuffer_.destroy();
        colorImage_ = Image();
        depthImage_ = Image();
    }

    // RenderPass（只在首次创建）
    if (!renderPass_.is_valid()) {
        RenderPassConfig rpConfig {};
        rpConfig.useDepth = config_.useDepth;
        rpConfig.colorAttachment.format = config_.format;
        rpConfig.colorAttachment.finalLayout = config_.colorFinalLayout;

        if (!renderPass_.create(ctx.device(), rpConfig)) {
            LUMEN_LOG_ERROR("OffscreenRenderTarget: RenderPass 创建失败");
            return false;
        }
    }

    // Color attachment image（GPU local render target）
    ImageCreateInfo colorInfo {};
    colorInfo.width = width_;
    colorInfo.height = height_;
    colorInfo.format = config_.format;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT; // 支持后处理采样

    if (!colorImage_.create(ctx, colorInfo)) {
        LUMEN_LOG_ERROR("OffscreenRenderTarget: 颜色附件创建失败");
        return false;
    }

    // Depth attachment（optional）
    if (config_.useDepth) {
        if (!depthImage_.create_depth_attachment(ctx, width_, height_)) {
            LUMEN_LOG_ERROR("OffscreenRenderTarget: 深度附件创建失败");
            return false;
        }
    }

    // Framebuffer assembly（attachment binding）
    std::vector<VkImageView> attachments { colorImage_.view() };
    if (config_.useDepth) {
        attachments.push_back(depthImage_.view());
    }

    if (!framebuffer_.create_offscreen(ctx.device(), renderPass_.handle(),
                                       width_, height_, attachments)) {
        LUMEN_LOG_ERROR("OffscreenRenderTarget: Framebuffer 创建失败");
        return false;
    }

    LUMEN_LOG_DEBUG("OffscreenRenderTarget 创建成功 {}x{}", width_, height_);
    return true;
}

// Destructor
OffscreenRenderTarget::~OffscreenRenderTarget() { destroy_(); }

/**
 * @brief Move constructor（GPU resource transfer semantics）
 *
 * 说明：
 * --------------------------------------
 * - 仅转移 CPU handle（VkImage / VkFramebuffer wrapper）
 * - 不触发 GPU copy
 * - 旧对象被置空，避免 double destroy
 */
OffscreenRenderTarget::OffscreenRenderTarget(
    OffscreenRenderTarget &&other) noexcept
    : ctx_ { other.ctx_ }, config_ { other.config_ }, width_ { other.width_ },
      height_ { other.height_ }, colorImage_ { std::move(other.colorImage_) },
      depthImage_ { std::move(other.depthImage_) },
      renderPass_ { std::move(other.renderPass_) },
      framebuffer_ { std::move(other.framebuffer_) } {

    other.ctx_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

/**
 * @brief Move assignment（释放旧资源 + 接管新资源）
 */
OffscreenRenderTarget &
OffscreenRenderTarget::operator=(OffscreenRenderTarget &&other) noexcept {
    if (this == &other)
        return *this;

    destroy_();

    ctx_ = other.ctx_;
    config_ = other.config_;
    width_ = other.width_;
    height_ = other.height_;

    colorImage_ = std::move(other.colorImage_);
    depthImage_ = std::move(other.depthImage_);
    renderPass_ = std::move(other.renderPass_);
    framebuffer_ = std::move(other.framebuffer_);

    other.ctx_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;

    return *this;
}

// ============================================================
// SwapchainRenderTarget
// ============================================================

/**
 * @brief 绑定 Swapchain 与 Framebuffer 映射
 *
 * 设计说明：
 * --------------------------------------
 * Swapchain 本身不持有 framebuffer，
 * framebuffer 由外部根据 swapchain image 创建。
 */
void SwapchainRenderTarget::bind(Swapchain *swapchain,
                                 Framebuffer *framebuffers) {
    swapchain_ = swapchain;
    framebuffers_ = framebuffers;
}

/**
 * @brief 获取指定 swapchain image 对应 framebuffer
 */
VkFramebuffer SwapchainRenderTarget::framebuffer(uint32_t index) const {
    return framebuffers_ ? framebuffers_->get(index) : VK_NULL_HANDLE;
}

/**
 * @brief swapchain 分辨率（通常跟 window 一致）
 */
VkExtent2D SwapchainRenderTarget::extent() const {
    return swapchain_ ? swapchain_->extent() : VkExtent2D { 0, 0 };
}

/**
 * @brief swapchain image format（由 surface 决定）
 */
VkFormat SwapchainRenderTarget::format() const {
    return swapchain_ ? swapchain_->image_format() : VK_FORMAT_UNDEFINED;
}

/**
 * @brief swapchain image 数量（double / triple buffering）
 */
uint32_t SwapchainRenderTarget::image_count() const {
    return swapchain_ ? swapchain_->image_count() : 0;
}

/**
 * @brief 是否可用（swapchain + framebuffer 必须同时有效）
 */
bool SwapchainRenderTarget::is_valid() const {
    return swapchain_ && swapchain_->is_valid() && framebuffers_ &&
           framebuffers_->count() > 0;
}

} // namespace lumen::render
