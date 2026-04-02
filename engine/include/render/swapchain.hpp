/**
 * @file swapchain.hpp
 * @brief Swapchain 与帧同步：双/三缓冲、Fence、Semaphore
 */

#pragma once

#include <cstdint>
#include <vector>

#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Context;
class Framebuffer;
class FrameSync;
class RenderPass;

struct SwapchainConfig {
    uint32_t imageCount { 2 };
    vk::PresentModeKHR presentMode { vk::PresentModeKHR::eFifo };
    vk::Format imageFormat { vk::Format::eUndefined };
    vk::ColorSpaceKHR colorSpace { vk::ColorSpaceKHR::eSrgbNonlinear };
};

class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain &) = delete;
    Swapchain(Swapchain &&) = default;
    Swapchain &operator=(const Swapchain &) = delete;
    Swapchain &operator=(Swapchain &&) = default;
    ~Swapchain();

    bool create(const Context &ctx, vk::SurfaceKHR surface, uint32_t width,
                uint32_t height, const SwapchainConfig &config = {});

    bool resize(uint32_t width, uint32_t height);

    uint32_t acquire_next_image(vk::Semaphore imageAvailableSemaphore,
                                vk::Fence fence = {},
                                uint64_t timeoutNs = UINT64_MAX);

    vk::Result present(vk::Queue queue, uint32_t imageIndex,
                       vk::Semaphore renderFinishedSemaphore);

    [[nodiscard]] vk::SwapchainKHR handle() const { return swapchain_; }

    [[nodiscard]] vk::Format image_format() const { return imageFormat_; }

    [[nodiscard]] vk::Extent2D extent() const { return extent_; }

    [[nodiscard]] uint32_t image_count() const {
        return static_cast<uint32_t>(imageViews_.size());
    }

    [[nodiscard]] vk::Image image(uint32_t index) const {
        return index < images_.size() ? images_[index] : vk::Image {};
    }

    [[nodiscard]] vk::ImageView image_view(uint32_t index) const {
        return index < imageViews_.size() ? imageViews_[index] : vk::ImageView {};
    }

    [[nodiscard]] const std::vector<vk::ImageView> &image_views() const {
        return imageViews_;
    }

    [[nodiscard]] vk::Device device() const { return device_; }

    [[nodiscard]] bool is_valid() const { return static_cast<bool>(swapchain_); }

private:
    void destroy_();

    bool create_swapchain_(uint32_t width, uint32_t height,
                           const SwapchainConfig &config);

    bool create_image_views_();

    vk::Device device_ {};
    vk::PhysicalDevice physicalDevice_ {};
    vk::SurfaceKHR surface_ {};

    vk::SwapchainKHR swapchain_ {};

    vk::Format imageFormat_ { vk::Format::eUndefined };
    vk::Extent2D extent_ { 0, 0 };

    std::vector<vk::Image> images_;
    std::vector<vk::ImageView> imageViews_;

    SwapchainConfig config_ {};

    uint32_t graphicsQueueFamily_ { 0 };
    uint32_t presentQueueFamily_ { 0 };
};

bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, vk::RenderPass renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  vk::ImageView depthImageView = {});

bool recreate_swapchain_resources(const Context &ctx, Swapchain &swapchain,
                                  Framebuffer &framebuffers,
                                  FrameSync &frameSync, const RenderPass &renderPass,
                                  uint32_t width, uint32_t height,
                                  uint32_t framesInFlight,
                                  vk::ImageView depthImageView = {});

} // namespace render
} // namespace lumen
