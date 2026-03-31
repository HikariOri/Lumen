/**
 * @file surface.hpp
 * @brief Vulkan Surface RAII 封装
 *
 * Surface 表示 Vulkan 与平台窗口系统（WSI）的连接桥梁：
 * - Window（GLFW / SDL / Win32 / Wayland）
 * - → VkSurfaceKHR
 *
 * 用于：
 * - 查询 Present 支持
 * - 创建 Swapchain
 *
 * 生命周期要求（非常重要）：
 * 1. 所有基于 Surface 创建的 Swapchain 必须先销毁
 * 2. Surface 必须在 Instance 销毁之前销毁
 *
 * 原因：
 * - Surface 是 Instance 的子对象
 * - Vulkan 要求父对象销毁前必须销毁所有子对象
 *
 * ⚠️ 销毁顺序（必须遵守）：
 *   Swapchain → Surface → Instance
 */

#pragma once

#include "context.hpp"
#include "platform/window.hpp"

namespace lumen {
namespace render {

/**
 * @class Surface
 * @brief Vulkan Surface 的 RAII 封装
 *
 * 封装 VkSurfaceKHR，负责其生命周期管理：
 * - 持有 VkInstance（用于销毁）
 * - 析构时自动调用 vkDestroySurfaceKHR
 *
 * 📌 Vulkan 语义：
 * - Surface 是“窗口系统抽象”（WSI）
 * - 本质是一个 opaque handle（不透明句柄）
 *
 * ⚠️ 关键注意事项：
 * - vkDestroySurfaceKHR 仅断开 Vulkan 与窗口的连接，
 *   不会销毁窗口本身
 * - Surface 可能被多个 Swapchain 使用
 *
 * 使用方式：
 * - 推荐：`Surface(ctx, window)`，内部使用 `ctx.instance()` 与 `Window::create_vulkan_surface`
 * - 或：由平台创建 `VkSurfaceKHR` 后用 `Surface(instance, surface)` 接管（RAII）
 */
class Surface {
public:
    /**
     * @brief 默认构造（无效 Surface）
     */
    Surface() = default;

    /**
     * @brief 接管已创建的 Surface 句柄
     *
     * @param instance 关联的 VkInstance（用于析构）
     * @param surface 由平台创建的 VkSurfaceKHR（可为 VK_NULL_HANDLE）
     *
     * 📌 所有权语义：
     * - Surface 对象“接管”该句柄
     * - 析构时自动调用 vkDestroySurfaceKHR
     *
     * ⚠️ 要求：
     * - surface 必须由该 instance 创建
     */
    Surface(VkInstance instance, VkSurfaceKHR surface) noexcept
        : instance_(instance), surface_(surface) {}

    /**
     * @brief 从 Context 与 Window 创建（等价于双参数构造函数 + `create_vulkan_surface`）
     */
    Surface(const Context &ctx, const platform::Window &window) noexcept
        : Surface(ctx.instance(), window.create_vulkan_surface(ctx)) {}

    /// 禁止拷贝（唯一所有权）
    Surface(const Surface &) = delete;

    /**
     * @brief 移动构造（转移 Surface 所有权）
     */
    Surface(Surface &&other) noexcept
        : instance_(other.instance_), surface_(other.surface_) {
        other.instance_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
    }

    /// 禁止拷贝赋值
    Surface &operator=(const Surface &) = delete;

    /**
     * @brief 移动赋值
     *
     * - 释放当前 Surface
     * - 接管 other 的资源
     */
    Surface &operator=(Surface &&other) noexcept {
        if (this != &other) {
            destroy_();
            instance_ = other.instance_;
            surface_ = other.surface_;
            other.instance_ = VK_NULL_HANDLE;
            other.surface_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    /**
     * @brief 析构函数（RAII）
     *
     * 自动调用 vkDestroySurfaceKHR
     */
    ~Surface() { destroy_(); }

    /**
     * @brief 获取 VkSurfaceKHR 句柄
     */
    [[nodiscard]] VkSurfaceKHR handle() const { return surface_; }

    /**
     * @brief 隐式转换为 VkSurfaceKHR
     */
    [[nodiscard]] explicit operator VkSurfaceKHR() const { return surface_; }

    /**
     * @brief 判断 Surface 是否有效
     */
    [[nodiscard]] bool is_valid() const { return surface_ != VK_NULL_HANDLE; }

private:
    /**
     * @brief 销毁 Surface（内部函数）
     *
     * 调用 vkDestroySurfaceKHR
     *
     * ⚠️ Vulkan 约束：
     * - 所有基于该 surface 创建的 swapchain 必须已销毁
     *   否则会触发 validation error :contentReference[oaicite:2]{index=2}
     *
     * - instance 必须仍然有效
     */
    void destroy_() {
        if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
    }

    /// 创建该 Surface 的 Instance（销毁时需要）
    VkInstance instance_ { VK_NULL_HANDLE };

    /// Vulkan Surface 句柄
    VkSurfaceKHR surface_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
