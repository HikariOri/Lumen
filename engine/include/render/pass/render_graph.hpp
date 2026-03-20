/**
 * @file render_graph.hpp
 * @brief RenderGraph：用资源读写依赖描述渲染流程，自动推导执行顺序与同步
 *
 * 核心：声明 reads/writes，系统自动完成拓扑排序、PipelineBarrier、Layout 转换。
 * 参考 docs/RenderGraph.md
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "render/resource/image.hpp"
#include "render/swapchain.hpp"

namespace lumen {
namespace render {

class Context;
class Framebuffer;

// -----------------------------------------------------------------------------
// RGImage - 渲染图资源
// -----------------------------------------------------------------------------

/// RGImage 资源类型
enum class RGImageType {
    Texture,   ///< 离屏纹理（单图）
    Swapchain, ///< Swapchain 图像（多图，按 index 选择）
};

/**
 * @struct RGImage
 * @brief 渲染图图像资源，封装 VkImage/View 及当前 Layout
 *
 * 支持离屏纹理（来自 Image）与 Swapchain 图像。
 */
struct RGImage {
    RGImageType type { RGImageType::Texture };
    VkImage image { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    VkFormat format { VK_FORMAT_UNDEFINED };
    VkExtent2D extent { 0, 0 };
    bool isDepth { false };

    /// 当前 Layout（Texture 用；Swapchain 用 perIndexLayouts_）
    VkImageLayout currentLayout { VK_IMAGE_LAYOUT_UNDEFINED };

    /// Swapchain 模式：每张图的 layout（仅 type==Swapchain 时有效）
    std::vector<VkImageLayout> perIndexLayouts_;

    /// Swapchain 模式：每张图的 image/view（仅 type==Swapchain 时有效）
    const Swapchain *swapchain_ { nullptr };

    /**
     * @brief 从 Image 创建离屏 RGImage
     */
    static RGImage from_texture(const Image &img, bool asDepth = false);

    /**
     * @brief 从 Swapchain 创建 RGImage（按 index 选择当前帧图像）
     */
    static RGImage from_swapchain(const Swapchain &swapchain);

    /// 获取指定索引的 image（Swapchain 需传 index）
    [[nodiscard]] VkImage image_at(uint32_t index = 0) const;

    /// 获取指定索引的 view
    [[nodiscard]] VkImageView view_at(uint32_t index = 0) const;

    /// 获取指定索引的 layout
    [[nodiscard]] VkImageLayout layout_at(uint32_t index) const;

    /// 设置 layout（Texture 用 index=0）
    void set_layout(uint32_t index, VkImageLayout layout);

    [[nodiscard]] bool is_swapchain() const {
        return type == RGImageType::Swapchain;
    }
    [[nodiscard]] uint32_t image_count() const;
};

// -----------------------------------------------------------------------------
// RGPass - 渲染节点
// -----------------------------------------------------------------------------

using RGPassExecuteFn =
    std::function<void(VkCommandBuffer cmd, uint32_t swapchainImageIndex)>;

/**
 * @struct RGPass
 * @brief 渲染图节点：声明 reads/writes，提供 execute 回调
 */
struct RGPass {
    std::string name;
    std::vector<RGImage *> reads;
    std::vector<RGImage *> writes;
    RGPassExecuteFn execute;
};

// -----------------------------------------------------------------------------
// RenderGraph
// -----------------------------------------------------------------------------

/**
 * @class RenderGraph
 * @brief 渲染图：addPass 声明依赖，execute 自动排序 + barrier + 执行
 */
class RenderGraph {
public:
    RenderGraph() = default;
    explicit RenderGraph(const Context *ctx);

    void set_context(const Context *ctx) { ctx_ = ctx; }

    /**
     * @brief 添加 Pass
     */
    void add_pass(const RGPass &pass);

    /**
     * @brief 执行渲染图
     * @param cmd 命令缓冲
     * @param swapchainImageIndex 当前 Swapchain 图像索引（用于 write 到
     * swapchain 的 Pass）
     */
    void execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex = 0);

    /// 清空所有 Pass
    void clear();

    [[nodiscard]] bool is_valid() const { return ctx_ != nullptr; }

private:
    void transition_image_(VkCommandBuffer cmd, RGImage *img,
                           VkImageLayout newLayout, uint32_t index);
    void pipeline_barrier_(VkCommandBuffer cmd, RGImage *img,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t index);
    std::vector<size_t> topo_sort_() const;

    const Context *ctx_ { nullptr };
    std::vector<RGPass> passes_;
};

} // namespace render
} // namespace lumen
