/**
 * @file swapchain.cpp
 * @brief Swapchain 实现
 *
 * 本文件实现 Vulkan WSI（窗口系统集成）核心逻辑：
 * - 查询 Surface 能力
 * - 创建 Swapchain
 * - 获取 Image + 创建 ImageView
 * - acquire / present 流程
 * - resize / 重建
 *
 * 📌 核心模型：
 * Swapchain 本质是一个“图像队列”：
 *   acquire → 渲染 → present → 归还队列 :contentReference[oaicite:0]{index=0}
 *
 * 📌 渲染循环：
 *   vkAcquireNextImageKHR
 *        ↓
 *   vkQueueSubmit（渲染）
 *        ↓
 *   vkQueuePresentKHR（归还图像） :contentReference[oaicite:1]{index=1}
 */

#include "render/swapchain.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"

#include <algorithm>
#include <limits>

namespace lumen {
namespace render {

namespace {

/**
 * @brief Swapchain 支持信息
 *
 * 包含：
 * - Surface 能力（extent / imageCount / transform）
 * - 支持的格式
 * - 支持的 present mode
 */
struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

/**
 * @brief 查询物理设备对 Surface 的支持情况
 *
 * 📌 Vulkan 特点：
 * - WSI 不是 core，需要单独查询
 * - 不同 GPU / 平台差异很大
 *
 * ⚠️ 必须检查：
 * - formats 不为空
 * - presentModes 不为空
 */
SwapchainSupportDetails query_swapchain_support(VkPhysicalDevice physical,
                                                VkSurfaceKHR surface) {
    SwapchainSupportDetails details {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface,
                                              &details.capabilities);

    uint32_t formatCount { 0 };
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount,
                                         nullptr);
    if (formatCount) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount,
                                             details.formats.data());
    }

    uint32_t presentCount { 0 };
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &presentCount,
                                              nullptr);
    if (presentCount) {
        details.presentModes.resize(presentCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physical, surface, &presentCount, details.presentModes.data());
    }
    return details;
}

/**
 * @brief 选择 Surface 格式
 *
 * 优先使用：
 * - 指定 format（如果用户提供）
 * - SRGB 色彩空间（常见默认）
 *
 * fallback：
 * - 返回第一个可用格式
 *
 * 📌 常见最佳选择：
 *   VK_FORMAT_B8G8R8A8_SRGB
 */
VkSurfaceFormatKHR
choose_surface_format(const std::vector<VkSurfaceFormatKHR> &formats,
                      VkFormat preferFormat, VkColorSpaceKHR preferColorSpace) {
    for (const auto &f : formats) {
        if ((preferFormat == VK_FORMAT_UNDEFINED || f.format == preferFormat) &&
            f.colorSpace == preferColorSpace) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR {} : formats[0];
}

/**
 * @brief 选择 Present Mode
 *
 * 优先：
 * - MAILBOX（低延迟 + 无撕裂）
 *
 * fallback：
 * - FIFO（Vulkan 保证支持，类似 VSync） :contentReference[oaicite:2]{index=2}
 *
 * 📌 模式说明：
 * FIFO：
 *   队列模式 → 垂直同步
 *
 * MAILBOX：
 *   覆盖旧帧 → 低延迟
 */
VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR> &modes,
                                     VkPresentModeKHR prefer) {
    auto it = std::ranges::find(modes, prefer);
    return it != modes.end() ? prefer : VK_PRESENT_MODE_FIFO_KHR;
}

/**
 * @brief 选择 Swapchain extent（分辨率）
 *
 * 📌 两种情况：
 * 1. 平台固定（如 Android）
 *    → 使用 caps.currentExtent
 *
 * 2. 可变（如桌面）
 *    → clamp 到合法范围
 *
 * ⚠️ width / height 可能不合法（窗口最小化）
 */
VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR &caps, uint32_t width,
                         uint32_t height) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D actual { width, height };
    actual.width = std::clamp(actual.width, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
    return actual;
}

} // namespace

/**
 * @brief 创建 Swapchain
 *
 * 步骤：
 * 1. 保存 Context 信息（device / queues）
 * 2. 查询支持能力
 * 3. 创建 Swapchain
 * 4. 获取 images
 * 5. 创建 image views
 *
 * 📌 注意：
 * - images 由 Vulkan 分配（不可手动释放）
 * - imageViews 由我们创建（必须手动销毁）
 */
bool Swapchain::create(const Context &ctx, VkSurfaceKHR surface, uint32_t width,
                       uint32_t height, const SwapchainConfig &config) {
    device_ = ctx.device();
    physicalDevice_ = ctx.physical_device();
    surface_ = surface;
    config_ = config;
    graphicsQueueFamily_ = ctx.graphics_queue_family();
    presentQueueFamily_ = ctx.present_queue_family();

    bool ok = create_swapchain_(width, height, config) && create_image_views_();
    if (ok) {
        LUMEN_LOG_DEBUG("Swapchain 创建成功 {}x{} imageCount={}", extent_.width,
                        extent_.height, static_cast<uint32_t>(images_.size()));
    } else {
        LUMEN_LOG_ERROR("Swapchain 创建失败");
    }
    return ok;
}

/**
 * @brief 内部：创建 VkSwapchainKHR
 *
 * 📌 核心参数解释：
 *
 * minImageCount：
 *   实际 image 数量 = min + 1（通常 triple buffering）
 *
 * imageUsage：
 *   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
 *
 * sharingMode：
 *   - EXCLUSIVE（性能最好）
 *   - CONCURRENT（跨 queue family）
 *
 * 📌 imageCount 规则：
 *   必须 ≥ minImageCount
 *   且 ≤ maxImageCount
 */
bool Swapchain::create_swapchain_(uint32_t width, uint32_t height,
                                  const SwapchainConfig &config) {
    auto support = query_swapchain_support(physicalDevice_, surface_);
    if (support.formats.empty() || support.presentModes.empty()) {
        LUMEN_LOG_ERROR("Swapchain 不支持: formats = {} presentModes = {}",
                        support.formats.size(), support.presentModes.size());
        return false;
    }

    VkSurfaceFormatKHR format = choose_surface_format(
        support.formats, config.imageFormat, config.colorSpace);
    VkPresentModeKHR presentMode =
        choose_present_mode(support.presentModes, config.presentMode);
    extent_ = choose_extent(support.capabilities, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;

    imageCount = std::max(imageCount, config.imageCount);

    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = { graphicsQueueFamily_,
                                      presentQueueFamily_ };
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    VkResult result =
        vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vkCreateSwapchainKHR 失败: {}",
                        static_cast<int>(result));
        return false;
    }

    imageFormat_ = format.format;

    uint32_t count { 0 };
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    return true;
}

/**
 * @brief 为每个 Swapchain Image 创建 ImageView
 *
 * 📌 原因：
 * - VkImage 不能直接用于渲染
 * - 必须通过 VkImageView 描述访问方式
 *
 * 📌 aspectMask：
 * - COLOR_ATTACHMENT → VK_IMAGE_ASPECT_COLOR_BIT
 *
 * ⚠️ 错误处理：
 * - 失败时回滚已创建的 view
 */
bool Swapchain::create_image_views_() {
    imageViews_.resize(images_.size());
    for (size_t i { 0 }; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageFormat_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkResult result =
            vkCreateImageView(device_, &viewInfo, nullptr, &imageViews_[i]);
        if (result != VK_SUCCESS) {
            LUMEN_LOG_DEBUG("index = {} 的 ImageView 创建失败", i);
            for (size_t j { 0 }; j < i; ++j) {
                vkDestroyImageView(device_, imageViews_[j], nullptr);
            }
            imageViews_.clear();
            return false;
        }
    }
    return true;
}

/**
 * @brief 获取下一张可渲染图像
 *
 * @param imageAvailableSemaphore
 *        GPU signal：图像可用
 *
 * 📌 本质：
 * 从 Swapchain 队列中“取出一张图像”
 *
 * 📌 同步：
 * - 不能直接使用 image！
 * - 必须等待 semaphore
 *
 * ⚠️ 关键语义：
 * - acquire 只是“获得 index”
 * - 不保证 GPU 已经可以写！
 *
 * 👉 必须：
 *   vkQueueSubmit 等待 semaphore
 *
 * 📌 返回值：
 * - VK_SUCCESS → 正常
 * - VK_SUBOPTIMAL_KHR → 可以用但不理想
 * - VK_ERROR_OUT_OF_DATE_KHR → 必须重建
 *
 * ❗ 当前问题（你的实现）：
 * - 丢失 VkResult（严重建议修复）
 */
uint32_t Swapchain::acquire_next_image(VkSemaphore imageAvailableSemaphore,
                                       VkFence fence, uint64_t timeoutNs) {
    uint32_t index { 0 };
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, timeoutNs, imageAvailableSemaphore, fence, &index);
    return result == VK_SUCCESS ? index : UINT32_MAX;
}

/**
 * @brief 提交图像到显示系统
 *
 * @param renderFinishedSemaphore
 *        GPU signal：渲染完成
 *
 * 📌 本质：
 * - 将图像“归还”给 Swapchain
 * - 并提交给显示系统
 *
 * 📌 同步：
 * - present 会等待 renderFinishedSemaphore
 *
 * 📌 关键语义：
 * - 调用 present 后 → 图像不再属于应用
 *
 * ⚠️ 返回值：
 * - VK_ERROR_OUT_OF_DATE_KHR → 必须重建
 * - VK_SUBOPTIMAL_KHR → 建议重建
 */
VkResult Swapchain::present(VkQueue queue, uint32_t imageIndex,
                            VkSemaphore renderFinishedSemaphore) {
    VkPresentInfoKHR presentInfo { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(queue, &presentInfo);
}

/**
 * @brief 重建 Swapchain
 *
 * 步骤：
 * 1. destroy old swapchain
 * 2. create new swapchain
 *
 * ⚠️ 必须保证：
 * - GPU 不再使用旧资源
 *
 * 👉 通常：
 *   vkDeviceWaitIdle()
 */
bool Swapchain::resize(uint32_t width, uint32_t height) {
    LUMEN_LOG_DEBUG("Swapchain 调整大小 {}x{}", width, height);
    destroy_();
    bool ok =
        create_swapchain_(width, height, config_) && create_image_views_();
    if (ok) {
        LUMEN_LOG_DEBUG("Swapchain resize 完成 {}x{}", extent_.width,
                        extent_.height);
    }
    return ok;
}

/**
 * @brief 销毁 Swapchain
 *
 * 顺序：
 * 1. ImageViews（我们创建）
 * 2. Swapchain（Vulkan 对象）
 *
 * 📌 images 不需要销毁：
 * - 由 Swapchain 管理
 */
void Swapchain::destroy_() {
    if (!imageViews_.empty() || swapchain_ != VK_NULL_HANDLE) {
        LUMEN_LOG_DEBUG("销毁 Swapchain (imageViews={})", imageViews_.size());
    }
    for (auto view : imageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    imageViews_.clear();
    images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

Swapchain::~Swapchain() { destroy_(); }

/**
 * @brief 完整重建流程（工业级写法 ✔）
 *
 * 顺序：
 *   wait_idle
 *   ↓
 *   destroy framebuffers（重要！）
 *   ↓
 *   resize swapchain
 *   ↓
 *   recreate framebuffers
 *   ↓
 *   recreate sync objects
 *
 * ⚠️ 关键点：
 * - Framebuffer 必须先销毁（你写对了！）
 * - 否则 ImageView 仍被引用 → validation error
 */
bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, VkRenderPass renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  VkImageView depthImageView) {
    ctx.wait_idle();
    if (width == 0 || height == 0) {
        return false;
    }
    // 必须先销毁 Framebuffer，否则 swapchain 的 ImageView
    // 仍被引用，vkDestroyImageView 会报错
    framebuffers.destroy();
    if (!swapchain.resize(width, height)) {
        return false;
    }
    if (!framebuffers.create(ctx.device(), renderPass, swapchain,
                             depthImageView)) {
        return false;
    }
    if (!frameSync.create(ctx.device(), swapchain.image_count(),
                          framesInFlight)) {
        return false;
    }
    LUMEN_LOG_DEBUG("Swapchain 已重建 {}x{}", width, height);
    return true;
}

bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, const RenderPass &renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  VkImageView depthImageView) {
    return recreate_swapchain_resources(
        ctx, swapchain, framebuffers, frameSync, renderPass.handle(), width, height,
        framesInFlight, depthImageView);
}

} // namespace render
} // namespace lumen
