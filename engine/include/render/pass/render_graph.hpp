/**
 * @file render_graph.hpp
 * @brief 基于资源依赖的 RenderGraph 渲染调度系统
 *
 * RenderGraph 使用“资源读写关系”描述渲染流程，
 * 自动推导执行顺序与 Vulkan 同步（Layout + Barrier）。
 *
 * ---
 * @section rg_pipeline 三阶段模型
 *
 * 1. Setup：
 *    - 使用 add_pass() 注册 Pass
 *    - 声明 reads / writes
 *
 * 2. Compile：
 *    - 构建依赖图（写 → 被读）
 *    - 拓扑排序生成执行顺序
 *    - 若存在环则失败
 *
 * 3. Execute：
 *    - 自动插入：
 *      - VkImageLayout 转换
 *      - Pipeline Barrier
 *    - 按顺序执行 Pass
 *
 * ---
 * @section rg_features 特性
 * - 自动依赖分析
 * - 自动同步（Barrier + Layout）
 * - 支持 Swapchain / 离屏纹理
 *
 * ---
 * @section rg_limitations 限制
 * - 不支持多线程调度
 * - 不支持 Async Compute
 * - 不支持 Resource Aliasing
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
// RGImage
// -----------------------------------------------------------------------------

/**
 * @enum RGImageType
 * @brief RenderGraph 图像类型
 */
enum class RGImageType {
    Texture,   ///< 离屏纹理（单图）
    Swapchain, ///< Swapchain 图像（多图）
};

/**
 * @struct RGImage
 * @brief RenderGraph 图像资源抽象
 *
 * 统一封装：
 * - Texture（Image）
 * - Swapchain
 *
 * ---
 * @section rgimage_layout Layout 管理
 *
 * - Texture：
 *   使用 currentLayout
 *
 * - Swapchain：
 *   使用 perIndexLayouts_
 *
 * ---
 * @warning
 * Layout 必须由 RenderGraph 维护，外部修改会破坏同步正确性
 */
struct RGImage {

    RGImageType type { RGImageType::Texture };

    /// Texture 模式使用
    VkImage image { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };

    VkFormat format { VK_FORMAT_UNDEFINED };
    VkExtent2D extent { 0, 0 };

    /// 是否为深度资源
    bool isDepth { false };

    /// 当前 Layout（仅 Texture）
    VkImageLayout currentLayout { VK_IMAGE_LAYOUT_UNDEFINED };

    /// Swapchain：每张图的 Layout
    std::vector<VkImageLayout> perIndexLayouts_;

    /// Swapchain 引用（非 owning）
    const Swapchain *swapchain_ { nullptr };

    /**
     * @brief 从离屏纹理创建 RGImage
     */
    static RGImage from_texture(const Image &img, bool asDepth = false);

    /**
     * @brief 从 Swapchain 创建 RGImage
     */
    static RGImage from_swapchain(const Swapchain &swapchain);

    /**
     * @brief 获取 VkImage
     */
    [[nodiscard]] VkImage image_at(uint32_t index = 0) const;

    /**
     * @brief 获取 VkImageView
     */
    [[nodiscard]] VkImageView view_at(uint32_t index = 0) const;

    /**
     * @brief 获取当前 Layout
     */
    [[nodiscard]] VkImageLayout layout_at(uint32_t index) const;

    /**
     * @brief 设置 Layout
     */
    void set_layout(uint32_t index, VkImageLayout layout);

    /**
     * @brief 是否为 Swapchain 资源
     */
    [[nodiscard]] bool is_swapchain() const {
        return type == RGImageType::Swapchain;
    }

    /**
     * @brief 图像数量
     */
    [[nodiscard]] uint32_t image_count() const;
};

// -----------------------------------------------------------------------------
// RGPass
// -----------------------------------------------------------------------------

/**
 * @typedef RGPassExecuteFn
 * @brief Pass 执行函数
 *
 * @param cmd Vulkan 命令缓冲
 * @param swapchainImageIndex 当前帧索引
 */
using RGPassExecuteFn =
    std::function<void(VkCommandBuffer cmd, uint32_t swapchainImageIndex)>;

/**
 * @struct RGPass
 * @brief RenderGraph 节点（渲染 Pass）
 *
 * ---
 * @section rgpass_dependency 依赖规则
 *
 * - 写 → 读：建立执行顺序
 * - 写 → 写：避免写冲突
 *
 * ---
 * @note
 * 这里只描述逻辑依赖，同步由 RenderGraph 自动生成
 */
struct RGPass {

    /// Pass 名称（调试用）
    std::string name;

    /// 读取资源
    std::vector<RGImage *> reads;

    /// 写入资源
    std::vector<RGImage *> writes;

    /// 执行函数
    RGPassExecuteFn execute;
};

// -----------------------------------------------------------------------------
// RenderGraph
// -----------------------------------------------------------------------------

/**
 * @class RenderGraph
 * @brief 渲染图调度器
 *
 * ---
 * @section rg_usage 使用方式
 *
 * @code
 * RenderGraph rg(ctx);
 *
 * rg.add_pass(...);
 * rg.compile();
 * rg.execute(cmd, index);
 * @endcode
 *
 * ---
 * @section rg_sync 自动同步
 *
 * 自动处理：
 * - Image Layout 转换
 * - Pipeline Barrier
 */
class RenderGraph {
public:
    RenderGraph() = default;

    /**
     * @brief 构造函数
     */
    explicit RenderGraph(const Context *ctx);

    /**
     * @brief 设置 Context
     */
    void set_context(const Context *ctx) { ctx_ = ctx; }

    /**
     * @brief 添加 Pass
     *
     * @note 会使 compile 失效
     */
    void add_pass(const RGPass &pass);

    /**
     * @brief 编译 RenderGraph
     *
     * @retval true 成功
     * @retval false 存在循环依赖
     */
    [[nodiscard]] bool compile();

    /**
     * @brief 是否已编译
     */
    [[nodiscard]] bool is_compiled() const {
        return compile_ok_ && !compile_stale_;
    }

    /**
     * @brief 执行 RenderGraph
     *
     * @note
     * - compile 过期会自动重新编译
     * - compile 失败不会执行任何 Pass
     */
    void execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex = 0);

    /**
     * @brief 清空所有 Pass
     */
    void clear();

    /**
     * @brief 是否有效
     */
    [[nodiscard]] bool is_valid() const { return ctx_ != nullptr; }

private:
    /**
     * @brief Layout 转换
     */
    void transition_image_(VkCommandBuffer cmd, RGImage *img,
                           VkImageLayout newLayout, uint32_t index);

    /**
     * @brief 插入 Pipeline Barrier
     */
    void pipeline_barrier_(VkCommandBuffer cmd, RGImage *img,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t index);

    /**
     * @brief 拓扑排序
     *
     * @note 若存在环，返回 size < passes_.size()
     */
    [[nodiscard]] std::vector<size_t> topo_sort_() const;

private:
    const Context *ctx_ { nullptr };

    std::vector<RGPass> passes_;
    std::vector<size_t> execution_order_;

    bool compile_stale_ { true };
    bool compile_ok_ { false };
};

} // namespace render
} // namespace lumen
