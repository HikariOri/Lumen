#pragma once

#include "rhi/vulkan.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace rhi {

class Device;

/// `acquire_next_image` 返回值；`eSuboptimalKHR` / `eErrorOutOfDateKHR` 需由调用方处理（通常 recreate）。
struct SwapchainAcquireResult {
    vk::Result result { vk::Result::eSuccess };
    std::uint32_t image_index { 0 };
};

[[nodiscard]] inline bool swapchain_acquire_needs_recreate(vk::Result r) noexcept {
    return r == vk::Result::eErrorOutOfDateKHR;
}

[[nodiscard]] inline bool swapchain_acquire_suboptimal(vk::Result r) noexcept {
    return r == vk::Result::eSuboptimalKHR;
}

[[nodiscard]] inline bool swapchain_present_needs_recreate(vk::Result r) noexcept {
    return r == vk::Result::eErrorOutOfDateKHR;
}

/// 交换链：非 RAII，由调用方保证在 `Device` 与 `VkSurfaceKHR` 存活期内使用。
/// 图像归 swapchain 所有，**勿用 VMA**；仅管理 `ImageView`。
class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;
    Swapchain(Swapchain &&) = delete;
    Swapchain &operator=(Swapchain &&) = delete;
    ~Swapchain() = default;

    /// `surface` 须由 `rhi::Context` / 平台创建且与 `device` 同属一实例。
    [[nodiscard]] bool init(Device *device, vk::SurfaceKHR surface,
                            std::uint32_t width, std::uint32_t height);

    void destroy();

    /// 窗口尺寸变化等；内部 `waitIdle` 后带 `oldSwapchain` 重建。
    [[nodiscard]] bool recreate(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] SwapchainAcquireResult
    acquire_next_image(vk::Semaphore signal_semaphore,
                       std::uint64_t timeout_ns =
                           std::numeric_limits<std::uint64_t>::max()) const;

    /// `wait_semaphores` 可为空；返回 `eSuboptimalKHR` / `eErrorOutOfDateKHR` 时应 recreate。
    [[nodiscard]] vk::Result
    present(std::uint32_t image_index,
            vk::ArrayProxy<const vk::Semaphore> wait_semaphores = {}) const;

    [[nodiscard]] vk::Format format() const { return surface_format_.format; }
    [[nodiscard]] vk::ColorSpaceKHR color_space() const {
        return surface_format_.colorSpace;
    }
    [[nodiscard]] vk::Extent2D extent() const { return extent_; }
    [[nodiscard]] std::uint32_t image_count() const {
        return static_cast<std::uint32_t>(images_.size());
    }

    [[nodiscard]] vk::Image image(std::uint32_t i) const;
    [[nodiscard]] vk::ImageView image_view(std::uint32_t i) const;

    [[nodiscard]] vk::SwapchainKHR handle() const { return swapchain_; }
    [[nodiscard]] bool valid() const { return static_cast<bool>(swapchain_); }

private:
    [[nodiscard]] bool create_swapchain_(vk::SwapchainKHR old_swapchain);
    void destroy_image_views_();

    Device *device_ { nullptr };
    vk::SurfaceKHR surface_ {};

    vk::SwapchainKHR swapchain_ {};
    std::vector<vk::Image> images_;
    std::vector<vk::ImageView> image_views_;

    vk::SurfaceFormatKHR surface_format_ {};
    vk::Extent2D extent_ {};

    std::uint32_t width_req_ { 0 };
    std::uint32_t height_req_ { 0 };
};

} // namespace rhi
