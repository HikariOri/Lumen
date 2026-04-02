/**
 * @file swapchain.cpp
 * @brief Swapchain 实现（Vulkan-Hpp）
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

struct SwapchainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

SwapchainSupportDetails query_swapchain_support(vk::PhysicalDevice physical,
                                                vk::SurfaceKHR surface) {
    SwapchainSupportDetails details {};
    physical.getSurfaceCapabilitiesKHR(surface, &details.capabilities);

    uint32_t formatCount { 0 };
    physical.getSurfaceFormatsKHR(surface, &formatCount, nullptr);
    if (formatCount) {
        details.formats.resize(formatCount);
        physical.getSurfaceFormatsKHR(surface, &formatCount,
                                      details.formats.data());
    }

    uint32_t presentCount { 0 };
    physical.getSurfacePresentModesKHR(surface, &presentCount, nullptr);
    if (presentCount) {
        details.presentModes.resize(presentCount);
        physical.getSurfacePresentModesKHR(surface, &presentCount,
                                           details.presentModes.data());
    }
    return details;
}

vk::SurfaceFormatKHR
choose_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats,
                      vk::Format preferFormat,
                      vk::ColorSpaceKHR preferColorSpace) {
    for (const auto &f : formats) {
        if ((preferFormat == vk::Format::eUndefined || f.format == preferFormat) &&
            f.colorSpace == preferColorSpace) {
            return f;
        }
    }
    return formats.empty() ? vk::SurfaceFormatKHR {} : formats[0];
}

vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR> &modes,
                                       vk::PresentModeKHR prefer) {
    auto it = std::ranges::find(modes, prefer);
    return it != modes.end() ? prefer : vk::PresentModeKHR::eFifo;
}

vk::Extent2D choose_extent(const vk::SurfaceCapabilitiesKHR &caps, uint32_t width,
                           uint32_t height) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    vk::Extent2D actual { width, height };
    actual.width = std::clamp(actual.width, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
    return actual;
}

} // namespace

bool Swapchain::create(const Context &ctx, vk::SurfaceKHR surface, uint32_t width,
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

bool Swapchain::create_swapchain_(uint32_t width, uint32_t height,
                                  const SwapchainConfig &config) {
    auto support = query_swapchain_support(physicalDevice_, surface_);
    if (support.formats.empty() || support.presentModes.empty()) {
        LUMEN_LOG_ERROR("Swapchain 不支持: formats = {} presentModes = {}",
                        support.formats.size(), support.presentModes.size());
        return false;
    }

    vk::SurfaceFormatKHR format = choose_surface_format(
        support.formats, config.imageFormat, config.colorSpace);
    vk::PresentModeKHR presentMode =
        choose_present_mode(support.presentModes, config.presentMode);
    extent_ = choose_extent(support.capabilities, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;

    imageCount = std::max(imageCount, config.imageCount);

    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo {};
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = format.format;
    createInfo.imageColorSpace = format.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = { graphicsQueueFamily_,
                                      presentQueueFamily_ };
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = vk::True;

    const vk::Result result =
        device_.createSwapchainKHR(&createInfo, nullptr, &swapchain_);
    if (result != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("createSwapchainKHR 失败: {}",
                        static_cast<int>(result));
        return false;
    }

    imageFormat_ = format.format;

    uint32_t count { 0 };
    device_.getSwapchainImagesKHR(swapchain_, &count, nullptr);
    images_.resize(count);
    device_.getSwapchainImagesKHR(swapchain_, &count, images_.data());
    return true;
}

bool Swapchain::create_image_views_() {
    imageViews_.resize(images_.size());
    for (size_t i { 0 }; i < images_.size(); ++i) {
        vk::ImageViewCreateInfo viewInfo {};
        viewInfo.image = images_[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = imageFormat_;
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        const vk::Result result =
            device_.createImageView(&viewInfo, nullptr, &imageViews_[i]);
        if (result != vk::Result::eSuccess) {
            LUMEN_LOG_DEBUG("index = {} 的 ImageView 创建失败", i);
            for (size_t j { 0 }; j < i; ++j) {
                device_.destroyImageView(imageViews_[j], nullptr);
            }
            imageViews_.clear();
            return false;
        }
    }
    return true;
}

uint32_t Swapchain::acquire_next_image(vk::Semaphore imageAvailableSemaphore,
                                       vk::Fence fence, uint64_t timeoutNs) {
    uint32_t index { 0 };
    const vk::Result result = device_.acquireNextImageKHR(
        swapchain_, timeoutNs, imageAvailableSemaphore, fence, &index);
    return result == vk::Result::eSuccess ? index : UINT32_MAX;
}

vk::Result Swapchain::present(vk::Queue queue, uint32_t imageIndex,
                              vk::Semaphore renderFinishedSemaphore) {
    vk::PresentInfoKHR presentInfo {};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    return queue.presentKHR(presentInfo);
}

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

void Swapchain::destroy_() {
    if (!imageViews_.empty() || swapchain_) {
        LUMEN_LOG_DEBUG("销毁 Swapchain (imageViews={})", imageViews_.size());
    }
    for (auto view : imageViews_) {
        if (view) {
            device_.destroyImageView(view, nullptr);
        }
    }
    imageViews_.clear();
    images_.clear();
    if (swapchain_) {
        device_.destroySwapchainKHR(swapchain_, nullptr);
        swapchain_ = nullptr;
    }
}

Swapchain::~Swapchain() { destroy_(); }

bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, vk::RenderPass renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  vk::ImageView depthImageView) {
    ctx.wait_idle();
    if (width == 0 || height == 0) {
        return false;
    }
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
                                  vk::ImageView depthImageView) {
    return recreate_swapchain_resources(ctx, swapchain, framebuffers, frameSync,
                                        renderPass.handle(), width, height,
                                        framesInFlight, depthImageView);
}

} // namespace render
} // namespace lumen
