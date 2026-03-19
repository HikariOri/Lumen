/**
 * @file window.cpp
 * @brief Window SDL3 实现
 */

#include "platform/window.hpp"
#include "platform/event_pump.hpp"
#include "core/logger.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>

namespace lumen::platform {

    bool Window::create(const WindowConfig &config) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            LUMEN_LOG_ERROR("SDL_Init 失败: {}", SDL_GetError());
            return false;
        }

        uint32_t flags = SDL_WINDOW_VULKAN;
        if (config.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }

        window_ = SDL_CreateWindow(config.title.c_str(),
                                   static_cast<int>(config.width),
                                   static_cast<int>(config.height), flags);
        if (!window_) {
            LUMEN_LOG_ERROR("SDL_CreateWindow 失败: {}", SDL_GetError());
            SDL_Quit();
            return false;
        }

        width_ = config.width;
        height_ = config.height;

        if (config.fullscreen) {
            SDL_SetWindowFullscreen(window_, true);
        }

        return true;
    }

    std::vector<const char *> Window::get_vulkan_instance_extensions() const {
        if (!window_)
            return {};

        uint32_t count { 0 };
        const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!names || count == 0)
            return {};

        std::vector<const char *> result(count);
        for (uint32_t i { 0 }; i < count; ++i) {
            result[i] = names[i];
        }
        return result;
    }

    VkSurfaceKHR Window::create_vulkan_surface(VkInstance instance) const {
        if (!window_ || instance == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;

        VkSurfaceKHR surface { VK_NULL_HANDLE };
        if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
            return VK_NULL_HANDLE;
        }
        return surface;
    }

    bool Window::poll_events() {
        EventPump pump;
        EventList events;
        Input input;
        return pump.poll(events, input);
    }

    uint32_t Window::width() const {
        if (!window_)
            return 0;
        int w { 0 }, h { 0 };
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return static_cast<uint32_t>(w > 0 ? w : width_);
    }

    uint32_t Window::height() const {
        if (!window_)
            return 0;
        int w { 0 }, h { 0 };
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        return static_cast<uint32_t>(h > 0 ? h : height_);
    }

    void Window::get_framebuffer_size(int *w, int *h) const {
        if (!window_ || !w || !h)
            return;
        SDL_GetWindowSizeInPixels(window_, w, h);
    }

    void Window::set_title(const std::string &title) {
        if (window_) {
            SDL_SetWindowTitle(window_, title.c_str());
        }
    }

    void Window::set_fullscreen(bool fullscreen) {
        if (window_) {
            SDL_SetWindowFullscreen(window_, fullscreen);
        }
    }

    void Window::destroy_() {
        if (window_) {
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
        if (this == &other)
            return *this;
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
