/**
 * @file render_pass.cpp
 * @brief RenderPass 与 Framebuffer 实现
 */

#include "render/pass/render_pass.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

    bool RenderPass::create(VkDevice device, const RenderPassConfig &config) {
        device_ = device;

        std::vector<VkAttachmentDescription> attachments;

        VkAttachmentDescription colorAtt { 0 };
        colorAtt.format = config.colorAttachment.format;
        colorAtt.samples = config.colorAttachment.samples;
        colorAtt.loadOp = config.colorAttachment.loadOp;
        colorAtt.storeOp = config.colorAttachment.storeOp;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = config.colorAttachment.initialLayout;
        colorAtt.finalLayout = config.colorAttachment.finalLayout;
        attachments.push_back(colorAtt);

        uint32_t depthIndex { 0 };
        if (config.useDepth) {
            depthIndex = static_cast<uint32_t>(attachments.size());
            VkAttachmentDescription depthAtt { 0 };
            depthAtt.format = config.depthAttachment.format;
            depthAtt.samples = config.depthAttachment.samples;
            depthAtt.loadOp = config.depthAttachment.loadOp;
            depthAtt.storeOp = config.depthAttachment.storeOp;
            depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.initialLayout = config.depthAttachment.initialLayout;
            depthAtt.finalLayout = config.depthAttachment.finalLayout;
            attachments.push_back(depthAtt);
        }

        VkAttachmentReference colorRef {
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkAttachmentReference depthRef {
            depthIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };

        VkSubpassDescription subpass { 0 };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = config.useDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency { 0 };
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo {
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        };
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        VkResult result =
            vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_);
        return result == VK_SUCCESS;
    }

    void RenderPass::destroy_() {
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }

    RenderPass::~RenderPass() { destroy_(); }

    RenderPass::RenderPass(RenderPass &&other) noexcept
        : device_ { other.device_ }, renderPass_ { other.renderPass_ } {
        other.device_ = VK_NULL_HANDLE;
        other.renderPass_ = VK_NULL_HANDLE;
    }

    RenderPass &RenderPass::operator=(RenderPass &&other) noexcept {
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        renderPass_ = other.renderPass_;
        other.device_ = VK_NULL_HANDLE;
        other.renderPass_ = VK_NULL_HANDLE;
        return *this;
    }

    // --- Framebuffer ---

    bool Framebuffer::create(VkDevice device, VkRenderPass renderPass,
                             const Swapchain &swapchain,
                             VkImageView depthImageView) {
        if (!framebuffers_.empty()) {
            destroy_();
        }
        device_ = device;
        framebuffers_.resize(swapchain.image_count());

        for (size_t i { 0 }; i < framebuffers_.size(); ++i) {
            std::vector<VkImageView> attachments { swapchain.image_view(i) };
            if (depthImageView != VK_NULL_HANDLE) {
                attachments.push_back(depthImageView);
            }

            VkFramebufferCreateInfo createInfo {
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
            };
            createInfo.renderPass = renderPass;
            createInfo.attachmentCount =
                static_cast<uint32_t>(attachments.size());
            createInfo.pAttachments = attachments.data();
            createInfo.width = swapchain.extent().width;
            createInfo.height = swapchain.extent().height;
            createInfo.layers = 1;

            VkResult result = vkCreateFramebuffer(device, &createInfo, nullptr,
                                                  &framebuffers_[i]);
            if (result != VK_SUCCESS) {
                destroy_();
                return false;
            }
        }
        return true;
    }

    bool
    Framebuffer::create_offscreen(VkDevice device, VkRenderPass renderPass,
                                  uint32_t width, uint32_t height,
                                  const std::vector<VkImageView> &attachments) {
        device_ = device;

        VkFramebufferCreateInfo createInfo {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layers = 1;

        framebuffers_.resize(1);
        VkResult result = vkCreateFramebuffer(device, &createInfo, nullptr,
                                              &framebuffers_[0]);
        return result == VK_SUCCESS;
    }

    void Framebuffer::destroy_() {
        for (auto fb : framebuffers_) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        framebuffers_.clear();
    }

    Framebuffer::~Framebuffer() { destroy_(); }

    Framebuffer::Framebuffer(Framebuffer &&other) noexcept
        : device_ { other.device_ },
          framebuffers_ { std::move(other.framebuffers_) } {
        other.device_ = VK_NULL_HANDLE;
    }

    Framebuffer &Framebuffer::operator=(Framebuffer &&other) noexcept {
        if (this == &other)
            return *this;
        destroy_();
        device_ = other.device_;
        framebuffers_ = std::move(other.framebuffers_);
        other.device_ = VK_NULL_HANDLE;
        return *this;
    }

} // namespace lumen::render
