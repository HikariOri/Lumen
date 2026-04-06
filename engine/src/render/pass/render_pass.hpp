/**
 * @file render_pass.hpp
 * @brief 渲染通道（RenderPass）与帧缓冲（Framebuffer）封装
 *
 * @details
 * 本文件对 Vulkan 渲染通道与帧缓冲进行了封装，主要包含：
 * - 附件描述（颜色 / 深度）
 * - RenderPass 创建（附件、子通道、依赖）
 * - Framebuffer 创建（交换链 / 离屏渲染）
 *
 * 设计目标：
 * - 简化 Vulkan RenderPass 配置流程
 * - 提供清晰的生命周期管理（RAII）
 * - 支持扩展（多附件 / MSAA / 延迟渲染）
 *
 * -----------------------------------------------------------------------------
 * @section renderpass_overview RenderPass 概念说明
 *
 * Vulkan 渲染流程核心：
 *
 * RenderPass = 渲染“规则”
 * Framebuffer = 渲染“目标”
 *
 * 二者关系：
 *
 *   RenderPass（描述）
 *        ↓
 *   Framebuffer（绑定 ImageView）
 *        ↓
 *   CommandBuffer 中执行绘制
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
 *     ColorAttachment [label="Color Attachment\n(Present)"];
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
 *   → COLOR_ATTACHMENT_OPTIMAL
 *   → PRESENT_SRC_KHR
 *
 * 深度附件：
 * UNDEFINED
 *   → DEPTH_STENCIL_ATTACHMENT_OPTIMAL
 *
 * -----------------------------------------------------------------------------
 * @section renderpass_ext 扩展方向
 *
 * 当前设计可扩展为：
 * - 多颜色附件（GBuffer）
 * - MSAA + Resolve Attachment
 * - 多 Subpass（Deferred Rendering）
 *
 * @ingroup Render
 *
 * @todo 将 RenderPass 和 Framebuffer 进行拆分
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Swapchain;

/**
 * @brief 颜色附件描述
 *
 * @details
 * 描述一个颜色附件在 RenderPass 中的行为，包括：
 * - 加载/存储方式
 * - 图像布局转换
 * - 多重采样设置
 *
 * @note
 * 常见配置：
 * - loadOp = CLEAR（每帧清屏）
 * - storeOp = STORE（用于呈现）
 */
struct ColorAttachmentDesc {
    /// 图像格式（通常与 Swapchain 一致）
    vk::Format format { vk::Format::eR8G8B8A8Srgb };

    /// 采样数（用于 MSAA）
    vk::SampleCountFlagBits samples { vk::SampleCountFlagBits::e1 };

    /// 渲染开始时的操作（清除 / 保留）
    vk::AttachmentLoadOp loadOp { vk::AttachmentLoadOp::eClear };

    /// 渲染结束时的操作（是否写回）
    vk::AttachmentStoreOp storeOp { vk::AttachmentStoreOp::eStore };

    /// RenderPass 开始前的布局
    vk::ImageLayout initialLayout { vk::ImageLayout::eUndefined };

    /// RenderPass 结束后的布局（用于呈现）
    vk::ImageLayout finalLayout { vk::ImageLayout::ePresentSrcKHR };
};

/**
 * @brief 深度附件描述
 *
 * @details
 * 定义深度（或深度模板）缓冲在 RenderPass 中的行为。
 *
 * @note
 * 常见配置：
 * - loadOp = CLEAR
 * - storeOp = DONT_CARE（性能优化）
 */
struct DepthAttachmentDesc {
    vk::Format format { vk::Format::eD32Sfloat };
    vk::SampleCountFlagBits samples { vk::SampleCountFlagBits::e1 };
    vk::AttachmentLoadOp loadOp { vk::AttachmentLoadOp::eClear };
    vk::AttachmentStoreOp storeOp { vk::AttachmentStoreOp::eDontCare };

    /// 初始布局
    vk::ImageLayout initialLayout { vk::ImageLayout::eUndefined };

    /// 一般保持为深度最优布局
    vk::ImageLayout finalLayout {
        vk::ImageLayout::eDepthStencilAttachmentOptimal
    };
};

/**
 * @brief RenderPass 创建配置
 *
 * @details
 * 将颜色附件与深度附件组合，用于构建 RenderPass。
 *
 * @note
 * 当前仅支持：
 * - 单颜色附件
 * - 可选深度附件
 */
struct RenderPassConfig {
    /// 颜色附件配置
    ColorAttachmentDesc colorAttachment {};

    /// 是否启用深度附件
    bool useDepth { true };

    /// 深度附件配置
    DepthAttachmentDesc depthAttachment {};
};

/**
 * @class RenderPass
 * @brief Vulkan 渲染通道封装
 *
 * @details
 * 封装 `vk::RenderPass` 的创建与销毁：
 * - 定义附件（Attachment）
 * - 定义子通道（Subpass）
 * - 设置依赖关系（Dependency）
 *
 * @note
 * 生命周期：
 * - create()
 * - 使用（绑定到 Framebuffer）
 * - 自动销毁（析构）
 *
 * @warning
 * 必须在 `vk::Device` 销毁前释放
 */
class RenderPass {
public:
    RenderPass() = default;
    RenderPass(const RenderPass &) = delete;
    RenderPass(RenderPass &&other) noexcept;
    RenderPass &operator=(const RenderPass &) = delete;
    RenderPass &operator=(RenderPass &&other) noexcept;
    ~RenderPass();

public:
    /**
     * @brief 创建 RenderPass
     *
     * @param device Vulkan 逻辑设备
     * @param config RenderPass 配置
     * @return 创建成功返回 true
     *
     * @details
     * 内部完成：
     * - Attachment 描述构建
     * - Subpass 设置
     * - Dependency 同步配置
     */
    bool create(vk::Device device, const RenderPassConfig &config);

    /// 获取底层 `vk::RenderPass`
    [[nodiscard]] vk::RenderPass handle() const { return renderPass_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const {
        return static_cast<bool>(renderPass_);
    }

private:
    void destroy_();

    vk::Device device_ {};
    vk::RenderPass renderPass_ {};
};

/**
 * @class Framebuffer
 * @brief Vulkan 帧缓冲封装
 *
 * @details
 * 管理多个 `vk::Framebuffer`（通常与 Swapchain 图像一一对应）：
 *
 * Framebuffer = RenderPass + ImageView[]
 *
 * @note
 * 每个 Swapchain Image 对应一个 Framebuffer
 */
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(const Framebuffer &) = delete;
    Framebuffer(Framebuffer &&other) noexcept;
    Framebuffer &operator=(const Framebuffer &) = delete;
    Framebuffer &operator=(Framebuffer &&other) noexcept;
    ~Framebuffer();

    /**
     * @brief 为 Swapchain 创建 Framebuffer
     *
     * @param device Vulkan 逻辑设备
     * @param renderPass RenderPass
     * @param swapchain 提供 ImageView 与尺寸
     * @param depthImageView 深度图像视图（可选）
     * @return 成功返回 true
     *
     * @note
     * Framebuffer 数量 = Swapchain image 数量
     */
    bool create(vk::Device device, vk::RenderPass renderPass,
                const Swapchain &swapchain,
                vk::ImageView depthImageView = {});

    /**
     * @brief 创建离屏 Framebuffer
     *
     * @details
     * 用于：
     * - Shadow Map
     * - GBuffer
     * - 后处理（Post Process）
     */
    bool create_offscreen(vk::Device device, vk::RenderPass renderPass,
                          uint32_t width, uint32_t height,
                          std::span<const vk::ImageView> attachments);

    bool create_offscreen(vk::Device device, vk::RenderPass renderPass,
                          uint32_t width, uint32_t height,
                          const std::vector<vk::ImageView> &attachments) {
        return create_offscreen(device, renderPass, width, height,
                                std::span<const vk::ImageView>(
                                    attachments.data(), attachments.size()));
    }

    /**
     * @brief 销毁 Framebuffer
     *
     * @details
     * 在以下情况必须调用：
     * - Swapchain 重建
     * - 窗口 resize
     *
     * @warning
     * 不销毁会导致：
     * - 使用失效 ImageView
     * - Vulkan validation error
     */
    void destroy();

    /// Framebuffer 数量
    [[nodiscard]] uint32_t count() const {
        return static_cast<uint32_t>(framebuffers_.size());
    }

    /// 获取指定 Framebuffer
    [[nodiscard]] vk::Framebuffer get(uint32_t index) const {
        return index < framebuffers_.size() ? framebuffers_[index]
                                            : vk::Framebuffer {};
    }

private:
    void destroy_();

    vk::Device device_ {};
    std::vector<vk::Framebuffer> framebuffers_;
};

} // namespace render
} // namespace lumen
