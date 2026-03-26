/**
 * @file render_pass.cpp
 * @brief RenderPass 与 Framebuffer 实现
 *
 * @details
 * 本文件实现 Vulkan 渲染通道（RenderPass）与帧缓冲（Framebuffer）。
 *
 * 核心职责：
 * - RenderPass：定义渲染流程（Attachment / Subpass / Dependency）
 * - Framebuffer：绑定实际图像资源（ImageView）
 *
 * 关键理解：
 * - RenderPass = “渲染规则”
 * - Framebuffer = “渲染目标”
 *
 * Vulkan 中所有绘制必须在 RenderPass 内进行
 * :contentReference[oaicite:0]{index=0}
 *
 * -----------------------------------------------------------------------------
 * @section renderpass_flow RenderPass 数据流图（单 Subpass）
 *
 * @dot
 * digraph RenderPassFlow {
 *     rankdir=LR;
 *
 *     node [shape=box, style=filled, color=lightblue];
 *
 *     External [label="External\n(交换链图像)"];
 *     Subpass0 [label="Subpass 0\n(颜色输出 + 深度测试)"];
 *     ColorAttachment [label="Color Attachment\n(PRESENT)"];
 *     DepthAttachment [label="Depth Attachment"];
 *
 *     External -> Subpass0 [label="VK_SUBPASS_EXTERNAL\n同步 + layout 过渡"];
 *     Subpass0 -> ColorAttachment [label="COLOR_ATTACHMENT_WRITE"];
 *     Subpass0 -> DepthAttachment [label="DEPTH_WRITE"];
 * }
 * @enddot
 *
 * -----------------------------------------------------------------------------
 * @section renderpass_layout Layout 流转
 *
 * 颜色附件：
 * UNDEFINED
 *   → COLOR_ATTACHMENT_OPTIMAL（Subpass 使用）
 *   → PRESENT_SRC_KHR（用于显示）
 *
 * 深度附件：
 * UNDEFINED
 *   → DEPTH_STENCIL_ATTACHMENT_OPTIMAL
 *
 * -----------------------------------------------------------------------------
 * @section renderpass_dependency Subpass Dependency 说明
 *
 * 本实现使用：
 * - srcSubpass = VK_SUBPASS_EXTERNAL
 * - dstSubpass = 0
 *
 * 含义：
 * - 等待 RenderPass 外部操作（如 acquire image）
 * - 再进入 Subpass 0
 *
 * Subpass Dependency 本质类似 pipeline barrier，
 * 但作用域限制在 RenderPass 内 :contentReference[oaicite:1]{index=1}
 *
 * 如果没有正确设置，可能导致：
 * - 图像未准备好就开始写
 * - layout transition 错误
 * - validation layer 报错
 */

#include "render/pass/render_pass.hpp"
#include "core/logger.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

/**
 * @brief 创建 Vulkan RenderPass
 *
 * @details
 * 创建流程：
 * 1. 构建附件描述（颜色 + 深度）
 * 2. 定义 Subpass
 * 3. 设置 Subpass Dependency（同步 + layout 过渡）
 * 4. 调用 vkCreateRenderPass
 *
 * 当前实现：
 * - 单 Subpass
 * - 一个颜色附件
 * - 可选深度附件
 */
bool RenderPass::create(VkDevice device, const RenderPassConfig &config) {
    device_ = device;

    std::vector<VkAttachmentDescription> attachments;

    // =========================
    // 颜色附件
    // =========================
    /**
     * 控制：
     * - loadOp：是否清除
     * - storeOp：是否保存结果
     * - layout：自动过渡
     */
    VkAttachmentDescription colorAtt { 0 };
    colorAtt.format = config.colorAttachment.format;
    colorAtt.samples = config.colorAttachment.samples;
    colorAtt.loadOp = config.colorAttachment.loadOp;
    colorAtt.storeOp = config.colorAttachment.storeOp;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = config.colorAttachment.initialLayout;
    colorAtt.finalLayout = config.colorAttachment.finalLayout;
    attachments.emplace_back(colorAtt);

    // =========================
    // 深度附件（可选）
    // =========================
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

        attachments.emplace_back(depthAtt);
    }

    // =========================
    // Attachment 引用
    // =========================
    VkAttachmentReference colorRef { 0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkAttachmentReference depthRef {
        depthIndex, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    // =========================
    // Subpass
    // =========================
    /**
     * Subpass 表示一个渲染阶段：
     * - 写入颜色附件
     * - 可选深度测试
     *
     * RenderPass 至少包含一个 Subpass
     */
    VkSubpassDescription subpass { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = config.useDepth ? &depthRef : nullptr;

    // =========================
    // Subpass Dependency（核心）
    // =========================
    /**
     * 作用：
     * - 同步外部操作 → Subpass
     * - 控制 layout transition 时机
     * - 避免数据竞争
     */
    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    dependency.srcAccessMask = 0;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // =========================
    // 创建 RenderPass
    // =========================
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

    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("RenderPass 创建成功, useDepth={}", config.useDepth);
    }

    return result == VK_SUCCESS;
}

/**
 * @brief 销毁 RenderPass
 */
void RenderPass::destroy_() {
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

RenderPass::~RenderPass() { destroy_(); }

/**
 * @brief 移动构造（转移资源所有权）
 */
RenderPass::RenderPass(RenderPass &&other) noexcept
    : device_ { other.device_ }, renderPass_ { other.renderPass_ } {
    other.device_ = VK_NULL_HANDLE;
    other.renderPass_ = VK_NULL_HANDLE;
}

/**
 * @brief 移动赋值
 */
RenderPass &RenderPass::operator=(RenderPass &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    renderPass_ = other.renderPass_;

    other.device_ = VK_NULL_HANDLE;
    other.renderPass_ = VK_NULL_HANDLE;

    return *this;
}

// ============================================================================
// Framebuffer
// ============================================================================

/**
 * @brief 为 Swapchain 创建 Framebuffer
 *
 * @details
 * 每个 Swapchain Image 对应一个 Framebuffer：
 *
 * Framebuffer = RenderPass + ImageView[]
 */
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
            attachments.emplace_back(depthImageView);
        }

        VkFramebufferCreateInfo createInfo {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = swapchain.extent().width;
        createInfo.height = swapchain.extent().height;
        createInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(device, &createInfo, nullptr,
                                              &framebuffers_[i]);

        if (result != VK_SUCCESS) {
            LUMEN_LOG_ERROR("Framebuffer 创建失败 index={}", i);
            destroy_();
            return false;
        }
    }

    LUMEN_LOG_DEBUG("Framebuffer 创建成功 count={} {}x{}", framebuffers_.size(),
                    swapchain.extent().width, swapchain.extent().height);

    return true;
}

/**
 * @brief 创建离屏 Framebuffer
 */
bool Framebuffer::create_offscreen(
    VkDevice device, VkRenderPass renderPass, uint32_t width, uint32_t height,
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

    VkResult result =
        vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffers_[0]);

    return result == VK_SUCCESS;
}

/**
 * @brief 销毁 Framebuffer
 */
void Framebuffer::destroy() { destroy_(); }

/**
 * @brief 内部销毁实现
 *
 * @warning
 * 必须在 Swapchain 重建前调用
 */
void Framebuffer::destroy_() {
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();
}

Framebuffer::~Framebuffer() { destroy_(); }

/**
 * @brief 移动构造
 */
Framebuffer::Framebuffer(Framebuffer &&other) noexcept
    : device_ { other.device_ },
      framebuffers_ { std::move(other.framebuffers_) } {
    other.device_ = VK_NULL_HANDLE;
}

/**
 * @brief 移动赋值
 */
Framebuffer &Framebuffer::operator=(Framebuffer &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    framebuffers_ = std::move(other.framebuffers_);

    other.device_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace lumen::render
