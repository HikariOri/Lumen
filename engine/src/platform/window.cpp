/**
 * @file window.cpp
 * @brief Window SDL3 实现
 */

#include "platform/window.hpp"
#include "core/log/logger.hpp"
#include "platform/event_pump.hpp"

#include <utility>

namespace lumen::platform {

std::expected<Window, std::string>
Window::create(const WindowConfig &config) {
    Window w;
    if (auto r = w.try_create_(config); !r) {
        return std::unexpected(std::move(r.error()));
    }
    return w;
}

std::expected<void, std::string>
Window::try_create_(const WindowConfig &config) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        const char *err = SDL_GetError();
        LUMEN_LOG_ERROR("SDL_Init 失败: {}", err != nullptr ? err : "");
        return std::unexpected(
            std::string("SDL_Init 失败: ") + (err != nullptr ? err : ""));
    }

    auto flags = SDL_WINDOW_VULKAN;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window_ =
        SDL_CreateWindow(config.title.c_str(), static_cast<int>(config.width),
                         static_cast<int>(config.height), flags);
    if (!window_) {
        const char *err = SDL_GetError();
        LUMEN_LOG_ERROR("SDL_CreateWindow 失败: {}", err != nullptr ? err : "");
        SDL_Quit();
        return std::unexpected(
            std::string("SDL_CreateWindow 失败: ") +
            (err != nullptr ? err : ""));
    }

    width_ = config.width;
    height_ = config.height;

    if (config.fullscreen) {
        SDL_SetWindowFullscreen(window_, true);
        int w {};
        int h {};
        get_framebuffer_size(&w, &h);
        width_ = static_cast<uint32_t>(w);
        height_ = static_cast<uint32_t>(h);
    }

    if (!config.icon_path.empty()) {
        if (!set_icon_from_file(config.icon_path)) {
            LUMEN_LOG_WARN("窗口图标未应用 path={}", config.icon_path);
        }
    }

    LUMEN_LOG_DEBUG("窗口创建成功 {}x{} \"{}\"", width_, height_, config.title);
    return {};
}

std::vector<const char *> Window::get_vulkan_instance_extensions() const {
    if (!window_) {
        LUMEN_LOG_WARN("window 未初始化");
        return {};
    }

    uint32_t count { 0 };
    const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names || count == 0) {
        LUMEN_LOG_WARN("未获取到 Vulkan Extensions");
        return {};
    }

    std::vector<const char *> result(count);
    for (uint32_t i { 0 }; i < count; ++i) {
        result[i] = names[i];
    }
    return result;
}

VkSurfaceKHR Window::create_vulkan_surface(VkInstance instance) const {
    if (!window_ || !instance) {
        LUMEN_LOG_WARN("window 或 instance 未初始化");
        return {};
    }

    VkSurfaceKHR surface {};
    if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(instance),
                                  nullptr, &surface)) {
        LUMEN_LOG_ERROR("Vulkan Surface 创建失败: {}", SDL_GetError());
        return {};
    }
    LUMEN_LOG_DEBUG("Vulkan Surface 创建成功");
    return surface;
}

bool Window::poll_events() {
    EventPump pump;
    return pump.poll();
}

uint32_t Window::width() const {
    if (!window_) {
        return 0;
    }
    int w { 0 };
    int h { 0 };
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return static_cast<uint32_t>(w > 0 ? w : width_);
}

uint32_t Window::height() const {
    if (!window_) {
        return 0;
    }
    int w { 0 }, h { 0 };
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return static_cast<uint32_t>(h > 0 ? h : height_);
}

void Window::get_framebuffer_size(int *w, int *h) const {
    if (!window_ || !w || !h) {
        return;
    }
    SDL_GetWindowSizeInPixels(window_, w, h);
}

void Window::set_title(const std::string &title) {
    if (window_) {
        SDL_SetWindowTitle(window_, title.c_str());
    }
}

bool Window::set_icon(SDL_Surface *icon) {
    if (!window_ || icon == nullptr) {
        return false;
    }
    if (!SDL_SetWindowIcon(window_, icon)) {
        LUMEN_LOG_ERROR("SDL_SetWindowIcon 失败: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool Window::set_icon_from_file(const std::string &path) {
    SDL_Surface *surf = IMG_Load(path.c_str());
    if (surf == nullptr) {
        LUMEN_LOG_ERROR("IMG_Load 图标失败 {}: {}", path, SDL_GetError());
        return false;
    }
    const bool ok = set_icon(surf);
    SDL_DestroySurface(surf);
    return ok;
}

void Window::set_fullscreen(bool fullscreen) {
    if (window_) {
        SDL_SetWindowFullscreen(window_, fullscreen);
    }
}

void Window::set_relative_mouse_mode(bool relative) {
    if (!window_) {
        return;
    }
    if (!SDL_SetWindowRelativeMouseMode(window_, relative)) {
        LUMEN_LOG_WARN("SDL_SetWindowRelativeMouseMode({}) 失败: {}", relative,
                       SDL_GetError());
    }
}

void Window::destroy_() {
    if (window_) {
        LUMEN_LOG_DEBUG("销毁窗口");
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

Window::~Window() { destroy_(); }

Window::Window(Window &&other) noexcept
    : window_ { other.window_ }, width_ { other.width_ },
      height_ { other.height_ } {
    other.window_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

Window &Window::operator=(Window &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy_();
    window_ = other.window_;
    width_ = other.width_;
    height_ = other.height_;
    other.window_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    return *this;
}

} // namespace lumen::platform
