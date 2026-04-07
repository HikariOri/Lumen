#include "rhi/swapchian.hpp"

#include "rhi/device.hpp"

#include "core/log/logger.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace rhi {

namespace {

[[nodiscard]] vk::SurfaceFormatKHR
choose_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats) {
    if (formats.empty()) {
        return { vk::Format::eB8G8R8A8Srgb,
                 vk::ColorSpaceKHR::eSrgbNonlinear };
    }
    for (const vk::SurfaceFormatKHR &f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Srgb &&
            f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return f;
        }
    }
    for (const vk::SurfaceFormatKHR &f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Unorm &&
            f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return f;
        }
    }
    return formats[0];
}

[[nodiscard]] vk::PresentModeKHR
choose_present_mode(const std::vector<vk::PresentModeKHR> &modes) {
    for (vk::PresentModeKHR m : modes) {
        if (m == vk::PresentModeKHR::eMailbox) {
            return m;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

[[nodiscard]] vk::Extent2D
choose_extent(const vk::SurfaceCapabilitiesKHR &caps, std::uint32_t req_w,
              std::uint32_t req_h) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    vk::Extent2D e {};
    e.width = std::clamp(req_w, caps.minImageExtent.width,
                         caps.maxImageExtent.width);
    e.height = std::clamp(req_h, caps.minImageExtent.height,
                          caps.maxImageExtent.height);
    return e;
}

[[nodiscard]] vk::CompositeAlphaFlagBitsKHR
choose_composite_alpha(vk::CompositeAlphaFlagsKHR supported) {
    using vk::CompositeAlphaFlagBitsKHR;
    if (supported & CompositeAlphaFlagBitsKHR::eOpaque) {
        return CompositeAlphaFlagBitsKHR::eOpaque;
    }
    if (supported & CompositeAlphaFlagBitsKHR::ePreMultiplied) {
        return CompositeAlphaFlagBitsKHR::ePreMultiplied;
    }
    if (supported & CompositeAlphaFlagBitsKHR::ePostMultiplied) {
        return CompositeAlphaFlagBitsKHR::ePostMultiplied;
    }
    return CompositeAlphaFlagBitsKHR::eInherit;
}

} // namespace

bool Swapchain::init(Device *device, const vk::SurfaceKHR surface,
                     const std::uint32_t width, const std::uint32_t height) {
    destroy();
    device_ = device;
    surface_ = surface;
    width_req_ = width;
    height_req_ = height;

    if (device_ == nullptr || !device_->vk_device() ||
        !device_->physical_device() || !surface_) {
        LUMEN_LOG_ERROR("Swapchain::init: device / surface 无效");
        device_ = nullptr;
        surface_ = nullptr;
        return false;
    }

    if (!create_swapchain_(nullptr)) {
        device_ = nullptr;
        surface_ = nullptr;
        return false;
    }
    LUMEN_LOG_DEBUG("Swapchain 创建成功 {}x{}", extent_.width, extent_.height);
    return true;
}

void Swapchain::destroy() {
    if (device_ == nullptr || !device_->vk_device()) {
        image_views_.clear();
        images_.clear();
        swapchain_ = nullptr;
        device_ = nullptr;
        surface_ = nullptr;
        return;
    }
    vk::Device dev = device_->vk_device();
    destroy_image_views_();
    images_.clear();
    if (swapchain_) {
        dev.destroySwapchainKHR(swapchain_, nullptr);
        swapchain_ = nullptr;
    }
    device_ = nullptr;
    surface_ = nullptr;
}

void Swapchain::destroy_image_views_() {
    if (device_ == nullptr || !device_->vk_device()) {
        image_views_.clear();
        return;
    }
    vk::Device dev = device_->vk_device();
    for (vk::ImageView v : image_views_) {
        if (v) {
            dev.destroyImageView(v, nullptr);
        }
    }
    image_views_.clear();
}

bool Swapchain::create_swapchain_(const vk::SwapchainKHR old_swapchain) {
    vk::PhysicalDevice gpu = device_->physical_device();
    vk::Device dev = device_->vk_device();

    vk::SurfaceCapabilitiesKHR caps {};
    const vk::Result cr =
        gpu.getSurfaceCapabilitiesKHR(surface_, &caps);
    if (cr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("getSurfaceCapabilitiesKHR 失败 {}",
                        static_cast<int>(cr));
        return false;
    }

    std::uint32_t format_count = 0;
    static_cast<void>(
        gpu.getSurfaceFormatsKHR(surface_, &format_count, nullptr));
    std::vector<vk::SurfaceFormatKHR> formats(format_count);
    if (format_count > 0) {
        static_cast<void>(gpu.getSurfaceFormatsKHR(surface_, &format_count,
                                                   formats.data()));
    }

    std::uint32_t mode_count = 0;
    static_cast<void>(
        gpu.getSurfacePresentModesKHR(surface_, &mode_count, nullptr));
    std::vector<vk::PresentModeKHR> modes(mode_count);
    if (mode_count > 0) {
        static_cast<void>(gpu.getSurfacePresentModesKHR(surface_, &mode_count,
                                                        modes.data()));
    }

    surface_format_ = choose_surface_format(formats);
    const vk::PresentModeKHR present_mode = choose_present_mode(modes);
    extent_ = choose_extent(caps, width_req_, height_req_);

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR ci {};
    ci.surface = surface_;
    ci.minImageCount = image_count;
    ci.imageFormat = surface_format_.format;
    ci.imageColorSpace = surface_format_.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
    ci.presentMode = present_mode;
    ci.clipped = vk::True;
    ci.oldSwapchain = old_swapchain;

    const std::uint32_t gfx = device_->graphics_queue_family();
    const std::uint32_t present_fam = device_->present_queue_family();
    const std::array<std::uint32_t, 2> families { gfx, present_fam };
    if (gfx != present_fam) {
        ci.imageSharingMode = vk::SharingMode::eConcurrent;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families.data();
    } else {
        ci.imageSharingMode = vk::SharingMode::eExclusive;
    }

    vk::SwapchainKHR new_sc {};
    const vk::Result r = dev.createSwapchainKHR(&ci, nullptr, &new_sc);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("createSwapchainKHR 失败 {}", static_cast<int>(r));
        return false;
    }

    if (old_swapchain) {
        dev.destroySwapchainKHR(old_swapchain, nullptr);
    }
    swapchain_ = new_sc;

    std::uint32_t count = 0;
    static_cast<void>(
        dev.getSwapchainImagesKHR(swapchain_, &count, nullptr));
    images_.resize(count);
    if (count > 0) {
        const vk::Result ir =
            dev.getSwapchainImagesKHR(swapchain_, &count, images_.data());
        if (ir != vk::Result::eSuccess) {
            LUMEN_LOG_ERROR("getSwapchainImagesKHR 失败 {}",
                            static_cast<int>(ir));
            dev.destroySwapchainKHR(swapchain_, nullptr);
            swapchain_ = nullptr;
            images_.clear();
            return false;
        }
    }

    image_views_.resize(images_.size());
    for (std::size_t i = 0; i < images_.size(); ++i) {
        vk::ImageViewCreateInfo vci {};
        vci.image = images_[i];
        vci.viewType = vk::ImageViewType::e2D;
        vci.format = surface_format_.format;
        vci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        const vk::Result vr =
            dev.createImageView(&vci, nullptr, &image_views_[i]);
        if (vr != vk::Result::eSuccess) {
            LUMEN_LOG_ERROR("createImageView(swapchain) 失败 {}",
                            static_cast<int>(vr));
            destroy_image_views_();
            dev.destroySwapchainKHR(swapchain_, nullptr);
            swapchain_ = nullptr;
            images_.clear();
            return false;
        }
    }

    return true;
}

bool Swapchain::recreate(const std::uint32_t width,
                         const std::uint32_t height) {
    width_req_ = width;
    height_req_ = height;
    if (device_ == nullptr || !device_->vk_device() || !swapchain_) {
        return false;
    }
    static_cast<void>(device_->vk_device().waitIdle());
    destroy_image_views_();
    images_.clear();

    const vk::SwapchainKHR old = swapchain_;
    swapchain_ = nullptr;

    if (!create_swapchain_(old)) {
        LUMEN_LOG_ERROR("Swapchain::recreate 失败");
        swapchain_ = nullptr;
        return false;
    }
    return true;
}

SwapchainAcquireResult
Swapchain::acquire_next_image(const vk::Semaphore signal_semaphore,
                              const std::uint64_t timeout_ns) const {
    SwapchainAcquireResult out {};
    if (device_ == nullptr || !device_->vk_device() || !swapchain_) {
        out.result = vk::Result::eErrorInitializationFailed;
        return out;
    }
    std::uint32_t idx = 0;
    const vk::Result r = device_->vk_device().acquireNextImageKHR(
        swapchain_, timeout_ns, signal_semaphore, nullptr, &idx);
    out.result = r;
    out.image_index = idx;
    return out;
}

vk::Result
Swapchain::present(const std::uint32_t image_index,
                    const vk::ArrayProxy<const vk::Semaphore> wait_semaphores) const {
    if (device_ == nullptr || !swapchain_) {
        return vk::Result::eErrorInitializationFailed;
    }
    vk::PresentInfoKHR pi {};
    pi.waitSemaphoreCount =
        static_cast<std::uint32_t>(wait_semaphores.size());
    pi.pWaitSemaphores = wait_semaphores.data();
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image_index;
    return device_->present_queue().presentKHR(pi);
}

vk::Image Swapchain::image(const std::uint32_t i) const {
    if (i >= images_.size()) {
        return nullptr;
    }
    return images_[i];
}

vk::ImageView Swapchain::image_view(const std::uint32_t i) const {
    if (i >= image_views_.size()) {
        return nullptr;
    }
    return image_views_[i];
}

bool rebuild_swapchain_present_framebuffers(
    const vk::Device device, const Swapchain &swapchain,
    const vk::RenderPass render_pass,
    std::vector<vk::Framebuffer> &out_framebuffers) {
    if (!render_pass) {
        LUMEN_LOG_ERROR(
            "rebuild_swapchain_present_framebuffers: RenderPass 无效");
        return false;
    }
    if (!swapchain.valid()) {
        LUMEN_LOG_ERROR(
            "rebuild_swapchain_present_framebuffers: Swapchain 无效");
        return false;
    }
    for (vk::Framebuffer fb : out_framebuffers) {
        if (fb) {
            device.destroyFramebuffer(fb, nullptr);
        }
    }
    out_framebuffers.clear();
    const vk::Extent2D ext = swapchain.extent();
    const std::uint32_t n = swapchain.image_count();
    out_framebuffers.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        vk::ImageView iv = swapchain.image_view(i);
        vk::FramebufferCreateInfo fci {};
        fci.renderPass = render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &iv;
        fci.width = ext.width;
        fci.height = ext.height;
        fci.layers = 1;
        const vk::Result fr =
            device.createFramebuffer(&fci, nullptr, &out_framebuffers[i]);
        if (fr != vk::Result::eSuccess) {
            LUMEN_LOG_ERROR("rebuild_swapchain_present_framebuffers: "
                            "createFramebuffer 失败 i={} vk::Result={} "
                            "extent={}x{}",
                            i, static_cast<int>(fr), ext.width, ext.height);
            for (std::uint32_t j = 0; j < i; ++j) {
                device.destroyFramebuffer(out_framebuffers[j], nullptr);
            }
            out_framebuffers.clear();
            return false;
        }
    }
    LUMEN_LOG_DEBUG("rebuild_swapchain_present_framebuffers: {} 个 {}x{}", n,
                    ext.width, ext.height);
    return true;
}

bool recreate_swapchain_and_present_framebuffers(
    Swapchain &swapchain, const std::uint32_t width,
    const std::uint32_t height, const vk::Device device,
    const vk::RenderPass render_pass,
    std::vector<vk::Framebuffer> &out_framebuffers) {
    if (!swapchain.recreate(width, height)) {
        return false;
    }
    return rebuild_swapchain_present_framebuffers(device, swapchain, render_pass,
                                                  out_framebuffers);
}

} // namespace rhi
