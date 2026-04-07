/**
 * @file window.hpp
 * @brief 窗口抽象：SDL3 实现
 *
 * 提供窗口创建/销毁、尺寸管理、全屏控制，以及 Vulkan Surface 创建能力。
 *
 * @note 该模块是平台层（platform）的一部分，用于隔离 SDL 与上层渲染逻辑。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rhi/vulkan.hpp"

struct SDL_Window;
struct SDL_Surface;

namespace lumen {
namespace platform {

/**
 * @struct WindowConfig
 * @brief 窗口创建配置
 *
 * 用于描述窗口初始化参数，包括尺寸、标题、图标及行为属性。
 */
struct WindowConfig {

    /// 窗口标题
    std::string title { "Lumen" };

    /// 初始宽度（逻辑尺寸）
    uint32_t width { 1280 };

    /// 初始高度（逻辑尺寸）
    uint32_t height { 720 };

    /// 是否全屏
    bool fullscreen { false };

    /// 是否允许用户调整窗口大小
    bool resizable { true };

    /// 窗口图标图像路径（PNG 等，由 SDL3_image 解码）；空表示不设置
    std::string icon_path {};
};

/**
 * @class Window
 * @brief 基于 SDL3 的窗口封装
 *
 * 负责：
 * - 创建与销毁窗口
 * - 处理窗口事件
 * - 提供 Vulkan 所需扩展
 * - 创建 Vulkan Surface
 *
 * @note
 * - 与 `Context::init_instance(config, window)` 联用时，无需再手动把扩展写入
 *   `ContextConfig::instanceExtensions`
 * - 单独创建 Instance 时，须在 `vkCreateInstance` 前调用
 *   `get_vulkan_instance_extensions()`
 * - framebuffer 尺寸可能与窗口尺寸不同（例如高 DPI）
 */
class Window {
public:
    Window() = default;

    /// 禁止拷贝
    Window(const Window &) = delete;

    /**
     * @brief 移动构造
     */
    Window(Window &&other) noexcept;

    /// 禁止拷贝赋值
    Window &operator=(const Window &) = delete;

    /**
     * @brief 移动赋值
     */
    Window &operator=(Window &&other) noexcept;

    /**
     * @brief 析构函数
     *
     * 自动销毁窗口资源（调用 destroy_）
     */
    ~Window();

    /**
     * @brief 创建窗口
     *
     * @param config 窗口配置
     * @return 成功返回 true，失败返回 false
     *
     * @note
     * - 创建失败通常意味着 SDL 初始化或窗口创建失败
     * - 成功后 window_ 不为空
     * - 若 `config.icon_path`
     * 非空，会尝试设置图标；加载失败仅打日志，不导致本函数返回 false
     */
    bool create(const WindowConfig &config);

    /**
     * @brief 获取 Vulkan Instance 所需扩展
     *
     * 用于 VkInstance 创建时启用 SDL 所需扩展。
     *
     * @return 扩展字符串列表（const char*）
     *
     * @note 必须在创建 Vulkan Instance 前调用
     */
    std::vector<const char *> get_vulkan_instance_extensions() const;

    /**
     * @brief 创建 Vulkan Surface
     *
     * @param instance 已创建的 `vk::Instance`
     * @return 成功返回 `vk::SurfaceKHR`，失败返回空句柄
     *
     * @note
     * - 依赖 SDL_Vulkan_CreateSurface
     * - 仅在窗口有效时调用
     * - 典型用法：`render::Surface surface(ctx, window)`，无需直接调用本函数
     */
    vk::SurfaceKHR create_vulkan_surface(vk::Instance instance) const;

    /**
     * @brief 轮询窗口事件
     *
     * 应在主循环中每帧调用，用于处理输入与窗口事件。
     *
     * @return
     * - true  : 继续运行
     * - false : 收到退出请求（如关闭窗口）
     */
    bool poll_events();

    /**
     * @brief 获取当前窗口宽度
     *
     * @return 当前逻辑宽度
     *
     * @note 在高 DPI 显示器上可能与 framebuffer 宽度不同
     */
    [[nodiscard]] uint32_t width() const;

    /**
     * @brief 获取当前窗口高度
     *
     * @return 当前逻辑高度
     */
    [[nodiscard]] uint32_t height() const;

    /**
     * @brief 获取 framebuffer 尺寸
     *
     * 用于 Vulkan Swapchain 创建（像素级尺寸）。
     *
     * @param width 输出宽度（像素）
     * @param height 输出高度（像素）
     */
    void get_framebuffer_size(int *width, int *height) const;

    /**
     * @brief 设置窗口标题
     *
     * @param title 新标题
     */
    void set_title(const std::string &title);

    /**
     * @brief 设置窗口图标
     *
     * @param icon 由 `SDL_CreateSurface`、`IMG_Load` 等得到的像素面；调用成功后
     * 若不再需要，调用方应 `SDL_DestroySurface(icon)`。
     * @return 成功返回 true
     *
     * @note 仅应在主线程调用（与 `SDL_SetWindowIcon` 一致）。
     */
    bool set_icon(SDL_Surface *icon);

    /**
     * @brief 从图像文件设置窗口图标
     *
     * 使用 SDL3_image `IMG_Load`（PNG、JPEG 等取决于引擎链接的 SDL_image
     * 能力）。
     *
     * @param path 文件路径
     * @return 加载或设置失败返回 false
     */
    bool set_icon_from_file(const std::string &path);

    /**
     * @brief 设置全屏状态
     *
     * @param fullscreen 是否进入全屏
     */
    void set_fullscreen(bool fullscreen);

    /**
     * @brief 相对鼠标模式（锁定光标、用位移驱动视角等）
     *
     * 封装 `SDL_SetWindowRelativeMouseMode`；退出或失焦前应置为 `false`。
     *
     * @param relative 是否启用相对模式
     */
    void set_relative_mouse_mode(bool relative);

    /**
     * @brief 获取 SDL 原始窗口句柄
     *
     * 可用于 ImGui、原生 SDL 操作等。
     *
     * @return SDL_Window 指针（可能为 nullptr）
     */
    [[nodiscard]] SDL_Window *sdl_window() const { return window_; }

    /**
     * @brief 判断窗口是否有效
     *
     * @return true 表示窗口已创建
     */
    [[nodiscard]] bool is_valid() const { return window_ != nullptr; }

private:
    /**
     * @brief 销毁窗口资源
     *
     * 内部调用 SDL_DestroyWindow 等清理逻辑。
     */
    void destroy_();

    SDL_Window *window_ { nullptr }; ///< SDL 窗口句柄
    uint32_t width_ { 0 };           ///< 当前宽度
    uint32_t height_ { 0 };          ///< 当前高度
};

} // namespace platform
} // namespace lumen
