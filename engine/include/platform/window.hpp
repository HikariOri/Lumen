/**
 * @file window.hpp
 * @brief 窗口抽象：SDL3 实现
 *
 * 提供窗口创建/销毁、尺寸、全屏、Vulkan Surface 创建。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct SDL_Window;

namespace lumen {
namespace platform {

/// 窗口创建配置
struct WindowConfig {
    std::string title { "LearnVulkan" };
    uint32_t width { 1280 };
    uint32_t height { 720 };
    bool fullscreen { false };
    bool resizable { true };
};

/**
 * @class Window
 * @brief 基于 SDL3 的窗口封装
 *
 * 负责创建 Vulkan 兼容窗口，提供 Instance 扩展与 Surface 创建。
 */
class Window {
public:
    Window() = default;
    Window(const Window &) = delete;
    Window(Window &&other) noexcept;
    Window &operator=(const Window &) = delete;
    Window &operator=(Window &&other) noexcept;
    ~Window();

    /**
     * @brief 创建窗口
     * @param config 窗口配置
     * @return 成功返回 true
     */
    bool create(const WindowConfig &config);

    /**
     * @brief 获取 Vulkan Instance 所需扩展（用于 Context::init_instance）
     */
    std::vector<const char *> get_vulkan_instance_extensions() const;

    /**
     * @brief 创建 Vulkan Surface
     * @param instance 已创建的 VkInstance
     * @return 成功返回 Surface 句柄，失败返回 VK_NULL_HANDLE
     */
    VkSurfaceKHR create_vulkan_surface(VkInstance instance) const;

    /**
     * @brief 轮询窗口事件（应在每帧调用）
     * @return false 表示收到退出请求
     */
    bool poll_events();

    /// 当前宽度（考虑 DPI 时可能与配置不同）
    [[nodiscard]] uint32_t width() const;
    /// 当前高度
    [[nodiscard]] uint32_t height() const;

    /// 窗口 framebuffer 尺寸（用于 Vulkan Swapchain）
    void get_framebuffer_size(int *width, int *height) const;

    void set_title(const std::string &title);
    void set_fullscreen(bool fullscreen);

    /// SDL 窗口句柄（供 ImGui 等使用）
    [[nodiscard]] SDL_Window *sdl_window() const { return window_; }

    /// 是否有效
    [[nodiscard]] bool is_valid() const { return window_ != nullptr; }

private:
    void destroy_();

    SDL_Window *window_ { nullptr };
    uint32_t width_ { 0 };
    uint32_t height_ { 0 };
};

} // namespace platform
} // namespace lumen
