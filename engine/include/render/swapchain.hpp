/**
 * @file swapchain.hpp
 * @brief Swapchain 与帧同步：双/三缓冲、Fence、Semaphore
 *
 * 负责 Swapchain 创建、Image View、以及每帧的同步原语。
 */

#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/// Swapchain 配置
struct SwapchainConfig {
    /// 期望的帧缓冲数量（2=双缓冲，3=三缓冲）
    uint32_t imageCount { 2 };
    /// 期望的呈现模式：FIFO、MAILBOX 等
    VkPresentModeKHR presentMode { VK_PRESENT_MODE_FIFO_KHR };
    /// 图像格式，VK_FORMAT_UNDEFINED 表示自动选择
    VkFormat imageFormat { VK_FORMAT_UNDEFINED };
    /// 色彩空间
    VkColorSpaceKHR colorSpace { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
};

/**
 * @class Swapchain
 * @brief Swapchain 与帧同步管理
 *
 * 管理 Swapchain 生命周期、Image Views、每帧的 Fence 与 Semaphore。
 * 支持 Resize 时重建 Swapchain。
 */
class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = default;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain& operator=(Swapchain&&) = default;
    ~Swapchain();

    /**
     * @brief 创建 Swapchain 及同步对象
     * @param ctx 已初始化的 Context
     * @param surface 窗口 Surface
     * @param width 表面宽度
     * @param height 表面高度
     * @param config 可选配置
     * @return 成功返回 true
     */
    bool create(const Context& ctx, VkSurfaceKHR surface, uint32_t width,
                uint32_t height, const SwapchainConfig& config = {});

    /**
     * @brief 窗口 Resize 时重建 Swapchain
     * @param width 新宽度
     * @param height 新高度
     * @return 成功返回 true
     */
    bool resize(uint32_t width, uint32_t height);

    /**
     * @brief 获取下一帧图像索引
     * @param imageAvailableSemaphore 图像可用时将被 signal 的 semaphore
     * @param fence 可选，传入 VK_NULL_HANDLE 表示不使用 fence（推荐，fence 仅用于 vkQueueSubmit）
     * @param timeoutNs 超时纳秒，避免无限阻塞导致无法处理窗口事件；UINT64_MAX 表示无限等待
     * @return 成功返回图像索引，失败或超时返回 UINT32_MAX
     */
    uint32_t acquire_next_image(VkSemaphore imageAvailableSemaphore,
                                VkFence fence = VK_NULL_HANDLE,
                                uint64_t timeoutNs = UINT64_MAX);

    /**
     * @brief 呈现当前帧
     * @param queue 呈现队列
     * @param imageIndex 由 acquire_next_image 返回的索引
     * @param renderFinishedSemaphore 渲染完成信号量
     * @return VK_SUCCESS 表示成功
     */
    VkResult present(VkQueue queue, uint32_t imageIndex,
                     VkSemaphore renderFinishedSemaphore);

    /// Swapchain 句柄
    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }

    /// 图像格式
    [[nodiscard]] VkFormat image_format() const { return imageFormat_; }

    /// 图像 extent（实际渲染分辨率）
    [[nodiscard]] VkExtent2D extent() const { return extent_; }

    /// Swapchain 图像数量
    [[nodiscard]] uint32_t image_count() const {
        return static_cast<uint32_t>(imageViews_.size());
    }

    /// 获取指定索引的 Image View
    [[nodiscard]] VkImageView image_view(uint32_t index) const {
        return index < imageViews_.size() ? imageViews_[index]
                                         : VK_NULL_HANDLE;
    }

    /// 获取所有 Image Views
    [[nodiscard]] const std::vector<VkImageView>& image_views() const {
        return imageViews_;
    }

    /// 设备（用于创建依赖 Swapchain 的资源）
    [[nodiscard]] VkDevice device() const { return device_; }

    /// 是否已创建
    [[nodiscard]] bool is_valid() const { return swapchain_ != VK_NULL_HANDLE; }

private:
    void destroy_();
    bool create_swapchain_(uint32_t width, uint32_t height,
                          const SwapchainConfig& config);
    bool create_image_views_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice_ { VK_NULL_HANDLE };
    VkSurfaceKHR surface_ { VK_NULL_HANDLE };
    VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
    VkFormat imageFormat_ { VK_FORMAT_UNDEFINED };
    VkExtent2D extent_ { 0, 0 };
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    SwapchainConfig config_ {};
    uint32_t graphicsQueueFamily_ { 0 };
    uint32_t presentQueueFamily_ { 0 };
};

} // namespace render
} // namespace lumen
