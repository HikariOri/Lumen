/**
 * @file render_pass.cpp
 * @brief RenderPass 与 Framebuffer 实现（Vulkan-Hpp）
 */

#include "render/pass/render_pass.hpp"
#include "core/logger.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

bool RenderPass::create(vk::Device device, const RenderPassConfig &config) {
    device_ = device;

    std::vector<vk::AttachmentDescription> attachments;

    vk::AttachmentDescription colorAtt {};
    colorAtt.format = config.colorAttachment.format;
    colorAtt.samples = config.colorAttachment.samples;
    colorAtt.loadOp = config.colorAttachment.loadOp;
    colorAtt.storeOp = config.colorAttachment.storeOp;
    colorAtt.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAtt.initialLayout = config.colorAttachment.initialLayout;
    colorAtt.finalLayout = config.colorAttachment.finalLayout;
    attachments.emplace_back(colorAtt);

    uint32_t depthIndex { 0 };
    if (config.useDepth) {
        depthIndex = static_cast<uint32_t>(attachments.size());

        vk::AttachmentDescription depthAtt {};
        depthAtt.format = config.depthAttachment.format;
        depthAtt.samples = config.depthAttachment.samples;
        depthAtt.loadOp = config.depthAttachment.loadOp;
        depthAtt.storeOp = config.depthAttachment.storeOp;
        depthAtt.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAtt.initialLayout = config.depthAttachment.initialLayout;
        depthAtt.finalLayout = config.depthAttachment.finalLayout;

        attachments.emplace_back(depthAtt);
    }

    vk::AttachmentReference colorRef {
        0, vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::AttachmentReference depthRef {
        depthIndex, vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    vk::SubpassDescription subpass {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = config.useDepth ? &depthRef : nullptr;

    vk::SubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests;

    dependency.srcAccessMask = {};

    dependency.dstStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput |
        vk::PipelineStageFlagBits::eEarlyFragmentTests;

    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                               vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo createInfo {};
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    const vk::Result result =
        device_.createRenderPass(&createInfo, nullptr, &renderPass_);

    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("RenderPass 创建成功, useDepth={}", config.useDepth);
    }

    return result == vk::Result::eSuccess;
}

void RenderPass::destroy_() {
    if (renderPass_) {
        device_.destroyRenderPass(renderPass_, nullptr);
        renderPass_ = nullptr;
    }
}

RenderPass::~RenderPass() { destroy_(); }

RenderPass::RenderPass(RenderPass &&other) noexcept
    : device_ { other.device_ }, renderPass_ { other.renderPass_ } {
    other.device_ = nullptr;
    other.renderPass_ = nullptr;
}

RenderPass &RenderPass::operator=(RenderPass &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    renderPass_ = other.renderPass_;

    other.device_ = nullptr;
    other.renderPass_ = nullptr;

    return *this;
}

bool Framebuffer::create(vk::Device device, vk::RenderPass renderPass,
                         const Swapchain &swapchain,
                         vk::ImageView depthImageView) {
    if (!framebuffers_.empty()) {
        destroy_();
    }

    device_ = device;
    framebuffers_.resize(swapchain.image_count());

    for (size_t i { 0 }; i < framebuffers_.size(); ++i) {

        std::vector<vk::ImageView> attachments { swapchain.image_view(i) };

        if (depthImageView) {
            attachments.emplace_back(depthImageView);
        }

        vk::FramebufferCreateInfo createInfo {};
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = swapchain.extent().width;
        createInfo.height = swapchain.extent().height;
        createInfo.layers = 1;

        const vk::Result res =
            device_.createFramebuffer(&createInfo, nullptr, &framebuffers_[i]);

        if (res != vk::Result::eSuccess) {
            LUMEN_LOG_ERROR("Framebuffer 创建失败 index={}", i);
            destroy_();
            return false;
        }
    }

    LUMEN_LOG_DEBUG("Framebuffer 创建成功 count={} {}x{}", framebuffers_.size(),
                    swapchain.extent().width, swapchain.extent().height);

    return true;
}

bool Framebuffer::create_offscreen(vk::Device device, vk::RenderPass renderPass,
                                   uint32_t width, uint32_t height,
                                   std::span<const vk::ImageView> attachments) {

    destroy_();
    device_ = device;

    vk::FramebufferCreateInfo createInfo {};
    createInfo.renderPass = renderPass;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.width = width;
    createInfo.height = height;
    createInfo.layers = 1;

    vk::Framebuffer fb {};
    const vk::Result result =
        device_.createFramebuffer(&createInfo, nullptr, &fb);

    if (result != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("Framebuffer 离屏创建失败 result={}",
                        static_cast<int>(result));
        destroy_();
        device_ = nullptr;
        return false;
    }

    framebuffers_.push_back(fb);
    return true;
}

void Framebuffer::destroy() { destroy_(); }

void Framebuffer::destroy_() {
    for (const vk::Framebuffer fb : framebuffers_) {
        if (fb) {
            device_.destroyFramebuffer(fb, nullptr);
        }
    }
    framebuffers_.clear();
}

Framebuffer::~Framebuffer() { destroy_(); }

Framebuffer::Framebuffer(Framebuffer &&other) noexcept
    : device_ { other.device_ },
      framebuffers_ { std::move(other.framebuffers_) } {
    other.device_ = nullptr;
}

Framebuffer &Framebuffer::operator=(Framebuffer &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    framebuffers_ = std::move(other.framebuffers_);

    other.device_ = nullptr;

    return *this;
}

} // namespace lumen::render
