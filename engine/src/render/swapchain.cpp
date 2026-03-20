/**
 * @file swapchain.cpp
 * @brief Swapchain 实现
 */

#include "render/swapchain.hpp"
#include "render/context.hpp"
#include "core/logger.hpp"

#include <algorithm>
#include <limits>

namespace lumen {
namespace render {

namespace {

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

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

VkSurfaceFormatKHR choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& formats, VkFormat preferFormat,
    VkColorSpaceKHR preferColorSpace) {
    for (const auto& f : formats) {
        if ((preferFormat == VK_FORMAT_UNDEFINED || f.format == preferFormat) &&
            f.colorSpace == preferColorSpace) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR {} : formats[0];
}

VkPresentModeKHR choose_present_mode(
    const std::vector<VkPresentModeKHR>& modes, VkPresentModeKHR prefer) {
    auto it = std::find(modes.begin(), modes.end(), prefer);
    return it != modes.end() ? prefer : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width,
                         uint32_t height) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D actual { width, height };
    actual.width =
        std::clamp(actual.width, caps.minImageExtent.width,
                   caps.maxImageExtent.width);
    actual.height =
        std::clamp(actual.height, caps.minImageExtent.height,
                   caps.maxImageExtent.height);
    return actual;
}

} // namespace

bool Swapchain::create(const Context& ctx, VkSurfaceKHR surface, uint32_t width,
                       uint32_t height, const SwapchainConfig& config) {
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

bool Swapchain::create_swapchain_(uint32_t width, uint32_t height,
                                 const SwapchainConfig& config) {
    auto support = query_swapchain_support(physicalDevice_, surface_);
    if (support.formats.empty() || support.presentModes.empty()) {
        LUMEN_LOG_ERROR("Swapchain 不支持: formats={} presentModes={}",
                        support.formats.size(), support.presentModes.size());
        return false;
    }

    VkSurfaceFormatKHR format = choose_surface_format(
        support.formats, config.imageFormat, config.colorSpace);
    VkPresentModeKHR presentMode =
        choose_present_mode(support.presentModes, config.presentMode);
    extent_ = choose_extent(support.capabilities, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }
    imageCount = std::max(imageCount, config.imageCount);

    VkSwapchainCreateInfoKHR createInfo {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
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
        LUMEN_LOG_ERROR("vkCreateSwapchainKHR 失败: {}", static_cast<int>(result));
        return false;
    }

    imageFormat_ = format.format;

    uint32_t count { 0 };
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());
    return true;
}

bool Swapchain::create_image_views_() {
    imageViews_.resize(images_.size());
    for (size_t i { 0 }; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
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
            for (size_t j { 0 }; j < i; ++j) {
                vkDestroyImageView(device_, imageViews_[j], nullptr);
            }
            imageViews_.clear();
            return false;
        }
    }
    return true;
}

uint32_t Swapchain::acquire_next_image(VkSemaphore imageAvailableSemaphore,
                                       VkFence fence, uint64_t timeoutNs) {
    uint32_t index { 0 };
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, timeoutNs, imageAvailableSemaphore, fence,
        &index);
    return result == VK_SUCCESS ? index : UINT32_MAX;
}

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

bool Swapchain::resize(uint32_t width, uint32_t height) {
    LUMEN_LOG_DEBUG("Swapchain 调整大小 {}x{}", width, height);
    destroy_();
    bool ok = create_swapchain_(width, height, config_) && create_image_views_();
    if (ok) {
        LUMEN_LOG_DEBUG("Swapchain resize 完成 {}x{}", extent_.width, extent_.height);
    }
    return ok;
}

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

} // namespace render
} // namespace lumen
