/**
 * @file pick_id_render_target.cpp
 */

#include "render/pass/pick_id_render_target.hpp"

#include <array>
#include <span>

#include "core/log/logger.hpp"

namespace lumen {
namespace render {

PickIdRenderTarget::PickIdRenderTarget(PickIdRenderTarget &&other) noexcept
    : ctx_ { other.ctx_ }, width_ { other.width_ }, height_ { other.height_ },
      colorImage_ { std::move(other.colorImage_) },
      depthImage_ { std::move(other.depthImage_) },
      renderPass_ { std::move(other.renderPass_) },
      framebuffer_ { std::move(other.framebuffer_) } {
    other.ctx_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

PickIdRenderTarget &
PickIdRenderTarget::operator=(PickIdRenderTarget &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy_();
    ctx_ = other.ctx_;
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

PickIdRenderTarget::~PickIdRenderTarget() { destroy_(); }

void PickIdRenderTarget::destroy_() {
    framebuffer_.destroy();
    framebuffer_ = Framebuffer();
    renderPass_ = RenderPass();
    depthImage_ = Image();
    colorImage_ = Image();
    ctx_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool PickIdRenderTarget::create(const Context &ctx, const uint32_t width,
                                const uint32_t height) {
    if (width == 0 || height == 0) {
        LUMEN_LOG_ERROR("PickIdRenderTarget: 无效尺寸 {}x{}", width, height);
        return false;
    }
    ctx_ = &ctx;
    width_ = width;
    height_ = height;
    return create_internal_(ctx);
}

bool PickIdRenderTarget::resize(const uint32_t width, const uint32_t height) {
    if (ctx_ == nullptr) {
        LUMEN_LOG_ERROR("PickIdRenderTarget::resize: 尚未 create");
        return false;
    }
    if (width == 0 || height == 0) {
        return false;
    }
    const uint32_t pw = width_;
    const uint32_t ph = height_;
    width_ = width;
    height_ = height;
    if (!create_internal_(*ctx_)) {
        width_ = pw;
        height_ = ph;
        destroy_();
        return false;
    }
    return true;
}

bool PickIdRenderTarget::create_internal_(const Context &ctx) {
    framebuffer_.destroy();
    colorImage_ = Image();
    depthImage_ = Image();
    renderPass_ = RenderPass();

    RenderPassConfig rpConfig {};
    rpConfig.useDepth = true;
    rpConfig.colorAttachment.format = vk::Format::eR32Uint;
    rpConfig.colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    rpConfig.colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    rpConfig.colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    rpConfig.colorAttachment.finalLayout =
        vk::ImageLayout::eTransferSrcOptimal;
    rpConfig.depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    rpConfig.depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    rpConfig.depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    rpConfig.depthAttachment.finalLayout =
        vk::ImageLayout::eDepthStencilAttachmentOptimal;

    if (!renderPass_.create(ctx.device(), rpConfig)) {
        LUMEN_LOG_ERROR("PickIdRenderTarget: RenderPass 创建失败");
        return false;
    }

    ImageCreateInfo colorInfo {};
    colorInfo.width = width_;
    colorInfo.height = height_;
    colorInfo.format = vk::Format::eR32Uint;
    colorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                      vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eSampled;
    if (!colorImage_.create(ctx, colorInfo)) {
        LUMEN_LOG_ERROR("PickIdRenderTarget: 颜色附件创建失败");
        return false;
    }

    if (!depthImage_.create_depth_attachment(ctx, width_, height_)) {
        LUMEN_LOG_ERROR("PickIdRenderTarget: 深度附件创建失败");
        return false;
    }

    std::array<vk::ImageView, 2> views { vk::ImageView { colorImage_.view() },
                                         vk::ImageView { depthImage_.view() } };
    if (!framebuffer_.create_offscreen(ctx.device(), renderPass_.handle(),
                                       width_, height_,
                                       std::span<const vk::ImageView>(
                                           views.data(), views.size()))) {
        LUMEN_LOG_ERROR("PickIdRenderTarget: Framebuffer 创建失败");
        return false;
    }

    return true;
}

void PickIdRenderTarget::record_color_barrier_to_undefined(
    const VkCommandBuffer cmd, const VkImage color_image) {
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = color_image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}

void PickIdRenderTarget::record_color_barrier_transfer_src_to_shader_read(
    const VkCommandBuffer cmd, const VkImage color_image) {
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = color_image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
        &b);
}

void PickIdRenderTarget::record_color_barrier_shader_read_to_undefined(
    const VkCommandBuffer cmd, const VkImage color_image) {
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = color_image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}

} // namespace render
} // namespace lumen
