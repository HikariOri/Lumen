/**
 * @file render_pass.cpp
 * @brief `vulkan::RenderPass` 实现。
 */

#include "vulkan/render_pass.hpp"

#include <vector>

namespace vulkan {

RenderPass::RenderPass(const VkDevice device, const VkRenderPass render_pass) noexcept
    : device_(device), vk_render_pass_(render_pass) {}

void RenderPass::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && vk_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, vk_render_pass_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    vk_render_pass_ = VK_NULL_HANDLE;
}

RenderPass::~RenderPass() {
    destroy();
}

RenderPass::RenderPass(RenderPass &&other) noexcept
    : device_(other.device_), vk_render_pass_(other.vk_render_pass_) {
    other.device_ = VK_NULL_HANDLE;
    other.vk_render_pass_ = VK_NULL_HANDLE;
}

RenderPass &RenderPass::operator=(RenderPass &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        vk_render_pass_ = other.vk_render_pass_;
        other.device_ = VK_NULL_HANDLE;
        other.vk_render_pass_ = VK_NULL_HANDLE;
    }
    return *this;
}

std::expected<RenderPass, std::string>
RenderPass::create(const VkDevice device, const RenderTargetBundle &bundle,
                   const bool color_final_present_src) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("RenderPass::create: null device"));
    }
    if (bundle.color_targets().empty() && !bundle.has_depth()) {
        return std::unexpected(
            std::string("RenderPass::create: bundle has no attachments"));
    }

    for (const RenderTarget &ct : bundle.color_targets()) {
        if (!ct.is_valid()) {
            return std::unexpected(
                std::string("RenderPass::create: invalid color RenderTarget"));
        }
        if (ct.format == VK_FORMAT_UNDEFINED) {
            return std::unexpected(std::string(
                "RenderPass::create: color RenderTarget format is UNDEFINED"));
        }
    }
    if (bundle.has_depth()) {
        const RenderTarget &dt = bundle.depth_target();
        if (dt.format == VK_FORMAT_UNDEFINED) {
            return std::unexpected(std::string(
                "RenderPass::create: depth RenderTarget format is UNDEFINED"));
        }
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;
    attachments.reserve(bundle.color_targets().size() +
                        (bundle.has_depth() ? 1U : 0U));
    color_refs.reserve(bundle.color_targets().size());

    for (std::size_t i { 0 }; i < bundle.color_targets().size(); ++i) {
        const RenderTarget &rt = bundle.color_targets()[i];
        VkAttachmentDescription att {};
        att.format = rt.format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = color_final_present_src
                              ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                              : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);
        color_refs.push_back({ static_cast<std::uint32_t>(i),
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
    }

    VkAttachmentReference depth_ref {};
    const bool has_depth = bundle.has_depth();
    if (has_depth) {
        const RenderTarget &rt = bundle.depth_target();
        VkAttachmentDescription att {};
        att.format = rt.format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);
        depth_ref.attachment = static_cast<std::uint32_t>(color_refs.size());
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount =
        static_cast<std::uint32_t>(color_refs.size());
    subpass.pColorAttachments =
        color_refs.empty() ? nullptr : color_refs.data();
    subpass.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;

    VkPipelineStageFlags dst_stages { 0 };
    VkAccessFlags dst_access { 0 };
    if (!color_refs.empty()) {
        dst_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (has_depth) {
        dst_stages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dst_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkSubpassDependency dep {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    if (color_final_present_src && color_refs.size() == 1U && !has_depth) {
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else {
        dep.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dep.dstStageMask = dst_stages;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = dst_access;
    }

    VkRenderPassCreateInfo info { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    VkRenderPass rp { VK_NULL_HANDLE };
    const VkResult res =
        vkCreateRenderPass(device, &info, nullptr, &rp);
    if (res != VK_SUCCESS) {
        return std::unexpected(
            std::string("RenderPass::create: vkCreateRenderPass failed ec=") +
            std::to_string(static_cast<int>(res)));
    }
    return RenderPass(device, rp);
}

} // namespace vulkan
