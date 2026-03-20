/**
 * @file render_pass.hpp
 * @brief 渲染通道与 Framebuffer
 *
 * 定义 RenderPass（附件、子通道、依赖）与 Framebuffer。
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Swapchain;

/// 颜色附件描述
struct ColorAttachmentDesc {
    VkFormat format { VK_FORMAT_R8G8B8A8_SRGB };
    VkSampleCountFlagBits samples { VK_SAMPLE_COUNT_1_BIT };
    VkAttachmentLoadOp loadOp { VK_ATTACHMENT_LOAD_OP_CLEAR };
    VkAttachmentStoreOp storeOp { VK_ATTACHMENT_STORE_OP_STORE };
    VkImageLayout initialLayout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout finalLayout { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
};

/// 深度附件描述
struct DepthAttachmentDesc {
    VkFormat format { VK_FORMAT_D32_SFLOAT };
    VkSampleCountFlagBits samples { VK_SAMPLE_COUNT_1_BIT };
    VkAttachmentLoadOp loadOp { VK_ATTACHMENT_LOAD_OP_CLEAR };
    VkAttachmentStoreOp storeOp { VK_ATTACHMENT_STORE_OP_DONT_CARE };
    VkImageLayout initialLayout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageLayout finalLayout { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
};

/// RenderPass 配置
struct RenderPassConfig {
    ColorAttachmentDesc colorAttachment {};
    bool useDepth { true };
    DepthAttachmentDesc depthAttachment {};
};

/**
 * @class RenderPass
 * @brief 渲染通道
 */
class RenderPass {
public:
    RenderPass() = default;
    RenderPass(const RenderPass&) = delete;
    RenderPass(RenderPass&& other) noexcept;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass& operator=(RenderPass&& other) noexcept;
    ~RenderPass();

    /**
     * @brief 创建 RenderPass
     * @param device VkDevice
     * @param config 配置
     * @return 成功返回 true
     */
    bool create(VkDevice device, const RenderPassConfig& config);

    [[nodiscard]] VkRenderPass handle() const { return renderPass_; }
    [[nodiscard]] bool is_valid() const {
        return renderPass_ != VK_NULL_HANDLE;
    }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkRenderPass renderPass_ { VK_NULL_HANDLE };
};

/**
 * @class Framebuffer
 * @brief 帧缓冲
 */
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer& operator=(Framebuffer&& other) noexcept;
    ~Framebuffer();

    /**
     * @brief 为 Swapchain 的每个 Image 创建 Framebuffer
     * @param device VkDevice
     * @param renderPass 渲染通道
     * @param swapchain Swapchain（提供 ImageViews 与 extent）
     * @param depthImageView 深度附件 ImageView，可为 VK_NULL_HANDLE
     * @return 成功返回 true
     */
    bool create(VkDevice device, VkRenderPass renderPass,
                const Swapchain& swapchain,
                VkImageView depthImageView = VK_NULL_HANDLE);

    /**
     * @brief 创建离屏 Framebuffer（自定义附件）
     */
    bool create_offscreen(VkDevice device, VkRenderPass renderPass,
                         uint32_t width, uint32_t height,
                         const std::vector<VkImageView>& attachments);

    /**
     * @brief 销毁 Framebuffer（Swapchain 重建前必须先调用，以释放对 ImageView 的引用）
     */
    void destroy();

    [[nodiscard]] uint32_t count() const {
        return static_cast<uint32_t>(framebuffers_.size());
    }
    [[nodiscard]] VkFramebuffer get(uint32_t index) const {
        return index < framebuffers_.size() ? framebuffers_[index]
                                            : VK_NULL_HANDLE;
    }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    std::vector<VkFramebuffer> framebuffers_;
};

} // namespace render
} // namespace lumen
