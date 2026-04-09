/**
 * @file render_target_bundle.cpp
 * @brief `vulkan::RenderTargetBundle` 实现。
 */

#include "vulkan/render_target_bundle.hpp"

namespace vulkan {

bool RenderTargetBundle::is_output_to_swapchain() const noexcept {
    for (const RenderTarget &rt : color_targets_) {
        if (rt.is_swapchain_target) {
            return true;
        }
    }
    return false;
}

RenderTargetBundle::~RenderTargetBundle() {
    if (framebuffer_device_ != VK_NULL_HANDLE && !framebuffers_.empty()) {
        for (const auto &[_, framebuffer] : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(framebuffer_device_, framebuffer, nullptr);
            }
        }
        framebuffers_.clear();
    }
}

bool RenderTargetBundle::set_extent_or_match_(
    const std::uint32_t width, const std::uint32_t height) noexcept {
    if (width == 0U || height == 0U) {
        return false;
    }
    if (width_ == 0U) {
        width_ = width;
        height_ = height;
        return true;
    }
    return width_ == width && height_ == height;
}

bool RenderTargetBundle::add_color_target(const RenderTarget &rt) {
    if (!rt.is_valid()) {
        return false;
    }
    if (!set_extent_or_match_(rt.width, rt.height)) {
        return false;
    }
    color_targets_.push_back(rt);
    return true;
}

bool RenderTargetBundle::set_depth_target(const RenderTarget &rt) {
    if (rt.is_valid()) {
        if (!set_extent_or_match_(rt.width, rt.height)) {
            return false;
        }
    }
    depth_target_ = rt;
    return true;
}

std::expected<VkFramebuffer, std::string>
RenderTargetBundle::get_framebuffer(const VkDevice device,
                                    const VkRenderPass render_pass) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("RenderTargetBundle::get_framebuffer: null device"));
    }
    if (render_pass == VK_NULL_HANDLE) {
        return std::unexpected(std::string(
            "RenderTargetBundle::get_framebuffer: null render_pass"));
    }
    if (width_ == 0U || height_ == 0U) {
        return std::unexpected(std::string(
            "RenderTargetBundle::get_framebuffer: width/height not set"));
    }
    if (color_targets_.empty() && !depth_target_.is_valid()) {
        return std::unexpected(
            std::string("RenderTargetBundle::get_framebuffer: no attachments"));
    }

    if (framebuffer_device_ == VK_NULL_HANDLE) {
        framebuffer_device_ = device;
    }
    if (framebuffer_device_ != device) {
        return std::unexpected(std::string(
            "RenderTargetBundle::get_framebuffer: device mismatch"));
    }

    const auto found = framebuffers_.find(render_pass);
    if (found != framebuffers_.end()) {
        return found->second;
    }

    std::vector<VkImageView> attachments;
    attachments.reserve(color_targets_.size() +
                        (depth_target_.is_valid() ? 1U : 0U));
    for (const RenderTarget &ct : color_targets_) {
        attachments.push_back(ct.view);
    }
    if (depth_target_.is_valid()) {
        attachments.push_back(depth_target_.view);
    }

    VkFramebufferCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
    };
    info.renderPass = render_pass;
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.width = width_;
    info.height = height_;
    info.layers = 1;

    VkFramebuffer fb { VK_NULL_HANDLE };
    const VkResult res = vkCreateFramebuffer(device, &info, nullptr, &fb);
    if (res != VK_SUCCESS) {
        return std::unexpected(
            std::string(
                "RenderTargetBundle::get_framebuffer: vkCreateFramebuffer "
                "failed ec=") +
            std::to_string(static_cast<int>(res)));
    }
    framebuffers_[render_pass] = fb;
    return fb;
}

void RenderTargetBundle::destroy(const VkDevice device) noexcept {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (const auto &entry : framebuffers_) {
        if (entry.second != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, entry.second, nullptr);
        }
    }
    framebuffers_.clear();
    framebuffer_device_ = VK_NULL_HANDLE;
}

void RenderTargetBundle::reset(const VkDevice device) noexcept {
    destroy(device);
    color_targets_.clear();
    depth_target_ = {};
    width_ = 0;
    height_ = 0;
}

} // namespace vulkan
