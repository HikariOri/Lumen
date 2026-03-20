/**
 * @file render_target.cpp
 * @brief OffscreenRenderTarget 与 SwapchainRenderTarget 实现
 */

#include "render/pass/render_target.hpp"

#include "core/logger.hpp"
#include "render/context.hpp"

namespace lumen::render {

// --- OffscreenRenderTarget ---

bool OffscreenRenderTarget::create(const Context& ctx,
                                   const OffscreenRenderTargetConfig& config) {
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

bool OffscreenRenderTarget::resize(const Context& ctx, uint32_t width,
                                   uint32_t height) {
    if (width == 0 || height == 0)
        return false;
    width_ = width;
    height_ = height;
    config_.width = width;
    config_.height = height;
    return create_internal_(ctx);
}

VkImageView OffscreenRenderTarget::color_view() const {
    return colorImage_.view();
}

VkFramebuffer OffscreenRenderTarget::framebuffer() const {
    return framebuffer_.get(0);
}

VkRenderPass OffscreenRenderTarget::render_pass() const {
    return renderPass_.handle();
}

void OffscreenRenderTarget::destroy_() {
    framebuffer_.destroy();
    framebuffer_ = Framebuffer();
    renderPass_ = RenderPass();
    depthImage_ = Image();
    colorImage_ = Image();
}

bool OffscreenRenderTarget::create_internal_(const Context& ctx) {
    // Resize 时先释放旧资源，RenderPass 可复用
    if (framebuffer_.count() > 0) {
        framebuffer_.destroy();
        colorImage_ = Image();
        depthImage_ = Image();
    }

    // RenderPass（首次创建）
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

    // Color Image
    ImageCreateInfo colorInfo {};
    colorInfo.width = width_;
    colorInfo.height = height_;
    colorInfo.format = config_.format;
    colorInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!colorImage_.create(ctx, colorInfo)) {
        LUMEN_LOG_ERROR("OffscreenRenderTarget: 颜色附件创建失败");
        return false;
    }

    // Depth Image
    if (config_.useDepth) {
        if (!depthImage_.create_depth_attachment(ctx, width_, height_)) {
            LUMEN_LOG_ERROR("OffscreenRenderTarget: 深度附件创建失败");
            return false;
        }
    }

    // Framebuffer
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

OffscreenRenderTarget::~OffscreenRenderTarget() {
    destroy_();
}

OffscreenRenderTarget::OffscreenRenderTarget(
    OffscreenRenderTarget&& other) noexcept
    : ctx_ { other.ctx_ },
      config_ { other.config_ },
      width_ { other.width_ },
      height_ { other.height_ },
      colorImage_ { std::move(other.colorImage_) },
      depthImage_ { std::move(other.depthImage_) },
      renderPass_ { std::move(other.renderPass_) },
      framebuffer_ { std::move(other.framebuffer_) } {
    other.ctx_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

OffscreenRenderTarget&
OffscreenRenderTarget::operator=(OffscreenRenderTarget&& other) noexcept {
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

// --- SwapchainRenderTarget ---

void SwapchainRenderTarget::bind(Swapchain* swapchain,
                                 Framebuffer* framebuffers) {
    swapchain_ = swapchain;
    framebuffers_ = framebuffers;
}

VkFramebuffer SwapchainRenderTarget::framebuffer(uint32_t index) const {
    return framebuffers_ ? framebuffers_->get(index) : VK_NULL_HANDLE;
}

VkExtent2D SwapchainRenderTarget::extent() const {
    return swapchain_ ? swapchain_->extent() : VkExtent2D { 0, 0 };
}

VkFormat SwapchainRenderTarget::format() const {
    return swapchain_ ? swapchain_->image_format() : VK_FORMAT_UNDEFINED;
}

uint32_t SwapchainRenderTarget::image_count() const {
    return swapchain_ ? swapchain_->image_count() : 0;
}

bool SwapchainRenderTarget::is_valid() const {
    return swapchain_ && swapchain_->is_valid() && framebuffers_ &&
           framebuffers_->count() > 0;
}

} // namespace lumen::render
