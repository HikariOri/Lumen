/**
 * @file swapchain.hpp
 * @brief Swapchain 与帧同步：双/三缓冲、Fence、Semaphore
 *
 * Swapchain 是 Vulkan WSI（Window System Integration）的核心组件：
 * - 管理“可呈现图像”（presentable images）
 * - 控制 CPU / GPU / 显示器之间的同步
 *
 * 主要职责：
 * - 创建 VkSwapchainKHR
 * - 管理 Swapchain Images 与 Image Views
 * - 提供 acquire / present 接口
 * - 配合 Semaphore / Fence 实现帧同步
 *
 * 📌 渲染流程（核心）：
 *
 *   acquire_next_image()
 *        ↓
 *   vkQueueSubmit（等待 imageAvailableSemaphore）
 *        ↓
 *   渲染命令执行
 *        ↓
 *   vkQueuePresentKHR（等待 renderFinishedSemaphore）
 *
 * ⚠️ Swapchain 生命周期：
 *   Surface → Swapchain → ImageView → Framebuffer
 *
 * ⚠️ Resize / OUT_OF_DATE：
 *   必须销毁并重建 Swapchain 及所有依赖资源
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;
class Framebuffer;
class FrameSync;

/**
 * @struct SwapchainConfig
 * @brief Swapchain 创建配置
 *
 * 用于控制缓冲策略、显示模式和图像格式选择。
 */
struct SwapchainConfig {
    /// 期望的帧缓冲数量：
    /// - 2 = 双缓冲（低延迟）
    /// - 3 = 三缓冲（更流畅，减少卡顿）
    uint32_t imageCount { 2 };

    /// 呈现模式：
    /// - FIFO   = 垂直同步（VSync，始终可用）
    /// - MAILBOX = 低延迟 + 不撕裂（推荐）
    /// - IMMEDIATE = 无 VSync（可能撕裂）
    VkPresentModeKHR presentMode { VK_PRESENT_MODE_FIFO_KHR };

    /// 图像格式（VK_FORMAT_UNDEFINED 表示自动选择）
    VkFormat imageFormat { VK_FORMAT_UNDEFINED };

    /// 色彩空间（通常使用 SRGB）
    VkColorSpaceKHR colorSpace { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
};

/**
 * @class Swapchain
 * @brief Swapchain 与帧同步管理
 *
 * 封装 Vulkan Swapchain，负责：
 * - Swapchain 创建与销毁
 * - Image / ImageView 管理
 * - acquire / present 操作
 *
 * 📌 与 FrameSync 配合：
 * - Semaphore：GPU-GPU 同步
 * - Fence：CPU-GPU 同步
 *
 * 📌 frames in flight：
 * - 多帧并行（通常 2~3）
 * - 提高 GPU 利用率
 */
class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain &) = delete;
    Swapchain(Swapchain &&) = default;
    Swapchain &operator=(const Swapchain &) = delete;
    Swapchain &operator=(Swapchain &&) = default;
    ~Swapchain();

    /**
     * @brief 创建 Swapchain 及相关资源
     *
     * @param ctx 已初始化的 Context
     * @param surface 窗口 Surface（WSI 入口）
     * @param width 表面宽度（framebuffer size）
     * @param height 表面高度
     * @param config 创建配置
     *
     * @return true 成功
     *
     * 内部流程：
     * - 查询 Surface capabilities（format / extent / present mode）
     * - 创建 VkSwapchainKHR
     * - 获取 Swapchain Images
     * - 创建 Image Views
     *
     * ⚠️ 注意：
     * - width / height 必须有效（非 0）
     * - Surface 必须支持 present
     */
    bool create(const Context &ctx, VkSurfaceKHR surface, uint32_t width,
                uint32_t height, const SwapchainConfig &config = {});

    /**
     * @brief 窗口 Resize 时重建 Swapchain
     *
     * @param width 新宽度
     * @param height 新高度
     *
     * @return true 成功
     *
     * ⚠️ 必须在 device idle 后调用（或确保无资源在使用）
     *
     * 通常流程：
     *   vkDeviceWaitIdle()
     *   destroy old swapchain
     *   create new swapchain
     */
    bool resize(uint32_t width, uint32_t height);

    /**
     * @brief 获取下一帧可用图像（Acquire）
     *
     * @param imageAvailableSemaphore
     *        当图像可用时由 GPU signal 的 semaphore
     *
     * @param fence
     *        可选 fence（通常不使用）
     *
     * @param timeoutNs
     *        超时时间（纳秒）
     *
     * @return 图像索引，失败返回 UINT32_MAX
     *
     * 📌 作用：
     * - 从 Swapchain 获取一个“可渲染图像”
     *
     * 📌 同步关系：
     * - imageAvailableSemaphore → vkQueueSubmit 等待
     *
     * ⚠️ 返回错误情况：
     * - VK_ERROR_OUT_OF_DATE_KHR → 需要重建 Swapchain
     * - VK_SUBOPTIMAL_KHR → 可以继续，但建议重建
     */
    uint32_t acquire_next_image(VkSemaphore imageAvailableSemaphore,
                                VkFence fence = VK_NULL_HANDLE,
                                uint64_t timeoutNs = UINT64_MAX);

    /**
     * @brief 提交图像到屏幕（Present）
     *
     * @param queue 呈现队列（必须支持 present）
     * @param imageIndex 图像索引（来自 acquire）
     * @param renderFinishedSemaphore 渲染完成信号量
     *
     * @return VkResult
     *
     * 📌 同步关系：
     * - renderFinishedSemaphore → present 等待
     *
     * 📌 完整流程：
     *   acquire → submit → present
     *
     * ⚠️ 返回：
     * - VK_ERROR_OUT_OF_DATE_KHR → 必须重建 Swapchain
     * - VK_SUBOPTIMAL_KHR → 可选重建
     */
    VkResult present(VkQueue queue, uint32_t imageIndex,
                     VkSemaphore renderFinishedSemaphore);

    /// Swapchain 句柄
    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }

    /// 图像格式
    [[nodiscard]] VkFormat image_format() const { return imageFormat_; }

    /// 实际渲染分辨率
    [[nodiscard]] VkExtent2D extent() const { return extent_; }

    /// Swapchain 图像数量
    [[nodiscard]] uint32_t image_count() const {
        return static_cast<uint32_t>(imageViews_.size());
    }

    /// 获取图像句柄
    [[nodiscard]] VkImage image(uint32_t index) const {
        return index < images_.size() ? images_[index] : VK_NULL_HANDLE;
    }

    /// 获取图像视图
    [[nodiscard]] VkImageView image_view(uint32_t index) const {
        return index < imageViews_.size() ? imageViews_[index] : VK_NULL_HANDLE;
    }

    /// 获取所有 Image Views
    [[nodiscard]] const std::vector<VkImageView> &image_views() const {
        return imageViews_;
    }

    /// 获取设备（用于依赖资源创建）
    [[nodiscard]] VkDevice device() const { return device_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return swapchain_ != VK_NULL_HANDLE; }

private:
    /// 销毁 Swapchain 及 ImageViews
    void destroy_();

    /// 创建 Swapchain（内部实现）
    bool create_swapchain_(uint32_t width, uint32_t height,
                           const SwapchainConfig &config);

    /// 创建 Image Views
    bool create_image_views_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice_ { VK_NULL_HANDLE };
    VkSurfaceKHR surface_ { VK_NULL_HANDLE };

    VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };

    VkFormat imageFormat_ { VK_FORMAT_UNDEFINED };
    VkExtent2D extent_ { 0, 0 };

    /// Swapchain Images（由 Vulkan 管理）
    std::vector<VkImage> images_;

    /// 对应的 Image Views（由我们创建）
    std::vector<VkImageView> imageViews_;

    SwapchainConfig config_ {};

    /// 队列族（用于共享模式）
    uint32_t graphicsQueueFamily_ { 0 };
    uint32_t presentQueueFamily_ { 0 };
};

/**
 * @brief 重建 Swapchain 及其依赖资源
 *
 * @param ctx Vulkan 上下文
 * @param swapchain Swapchain（会被 resize）
 * @param framebuffers Framebuffer 集合（会重建）
 * @param frameSync 帧同步对象（会重建）
 * @param renderPass RenderPass
 * @param width 新宽度
 * @param height 新高度
 * @param framesInFlight 并发帧数
 * @param depthImageView 深度附件
 *
 * @return true 成功
 *
 * 📌 完整流程：
 *   1. ctx.wait_idle()
 *   2. swapchain.resize()
 *   3. framebuffers.create()
 *   4. frameSync.create()
 *
 * ⚠️ 必须在以下情况调用：
 * - 窗口 resize
 * - acquire/present 返回 OUT_OF_DATE
 *
 * ⚠️ 注意：
 * - 所有依赖 Swapchain 的资源必须重建：
 *   - Framebuffer
 *   - Render targets
 *   - Depth buffer（可选）
 */
bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, VkRenderPass renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  VkImageView depthImageView = VK_NULL_HANDLE);

} // namespace render
} // namespace lumen
