/**
 * @file surface.hpp
 * @brief Vulkan Surface RAII 封装
 *
 * 负责 Surface 的生命周期管理，析构时自动销毁。
 * 销毁顺序须在 Swapchain 之后、Context (Instance) 之前。
 */

#pragma once

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

/**
 * @class Surface
 * @brief Vulkan Surface 的 RAII 封装
 *
 * 持有 Surface 句柄，析构时调用 vkDestroySurfaceKHR。
 * 构造时需传入 Instance（用于析构），Surface 须在 Instance 销毁前销毁。
 */
class Surface {
public:
    Surface() = default;

    /**
     * @brief 接管已创建的 Surface 句柄
     * @param instance 关联的 VkInstance（用于析构时销毁）
     * @param surface 从 Window::create_vulkan_surface 获得的句柄，可为 VK_NULL_HANDLE
     */
    Surface(VkInstance instance, VkSurfaceKHR surface) noexcept
        : instance_(instance), surface_(surface) {}

    Surface(const Surface&) = delete;
    Surface(Surface&& other) noexcept
        : instance_(other.instance_), surface_(other.surface_) {
        other.instance_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
    }
    Surface& operator=(const Surface&) = delete;
    Surface& operator=(Surface&& other) noexcept {
        if (this != &other) {
            destroy_();
            instance_ = other.instance_;
            surface_ = other.surface_;
            other.instance_ = VK_NULL_HANDLE;
            other.surface_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ~Surface() { destroy_(); }

    /// Surface 句柄
    [[nodiscard]] VkSurfaceKHR handle() const { return surface_; }
    [[nodiscard]] explicit operator VkSurfaceKHR() const { return surface_; }
    [[nodiscard]] bool is_valid() const { return surface_ != VK_NULL_HANDLE; }

private:
    void destroy_() {
        if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
    }

    VkInstance instance_ { VK_NULL_HANDLE };
    VkSurfaceKHR surface_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
