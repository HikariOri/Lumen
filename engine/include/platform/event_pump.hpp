/**
 * @file event_pump.hpp
 * @brief 事件轮询 + 回调：SDL3 实现，poll() 拉取并分发
 *
 * 单对象：注册回调后每帧 poll()，返回 false 即退出。
 * 需在 Window 创建后调用（SDL_Init 已执行）。
 */

#pragma once

#include "platform/event.hpp"
#include "platform/input.hpp"

#include <functional>

namespace lumen {
namespace platform {

/**
 * @class EventPump
 * @brief 事件轮询器：拉取事件、更新 Input、分发到回调
 *
 * 用法：
 *   EventPump pump;
 *   pump.on_quit([] { running = false; });
 *   pump.on_key_down([](const EventKeyDown& e) { ... });
 *   while (pump.poll()) {
 *       if (pump.input().is_key_down(Key::W)) ...
 *   }
 */
class EventPump {
public:
    using QuitFn = std::function<void()>;
    using KeyDownFn = std::function<void(const EventKeyDown&)>;
    using KeyUpFn = std::function<void(const EventKeyUp&)>;
    using MouseButtonDownFn =
        std::function<void(const EventMouseButtonDown&)>;
    using MouseButtonUpFn = std::function<void(const EventMouseButtonUp&)>;
    using MouseMoveFn = std::function<void(const EventMouseMove&)>;
    using MouseWheelFn = std::function<void(const EventMouseWheel&)>;
    using WindowResizeFn = std::function<void(const EventWindowResize&)>;
    /// 原始 SDL 事件回调（用于 ImGui 等，每事件调用一次）
    using SDLEventFn = std::function<void(const void* sdlEvent)>;

    void on_quit(QuitFn f) { on_quit_ = std::move(f); }
    void on_key_down(KeyDownFn f) { on_key_down_ = std::move(f); }
    void on_key_up(KeyUpFn f) { on_key_up_ = std::move(f); }
    void on_mouse_button_down(MouseButtonDownFn f) {
        on_mouse_button_down_ = std::move(f);
    }
    void on_mouse_button_up(MouseButtonUpFn f) {
        on_mouse_button_up_ = std::move(f);
    }
    void on_mouse_move(MouseMoveFn f) { on_mouse_move_ = std::move(f); }
    void on_mouse_wheel(MouseWheelFn f) { on_mouse_wheel_ = std::move(f); }
    void on_window_resize(WindowResizeFn f) {
        on_window_resize_ = std::move(f);
    }
    void on_sdl_event(SDLEventFn f) { on_sdl_event_ = std::move(f); }

    /**
     * @brief 轮询事件、更新 Input、分发回调
     * @return false 表示收到退出请求，主循环应结束
     */
    bool poll();

    /// 本帧输入状态（poll 后有效）
    const Input& input() const { return input_; }

private:
    void dispatch_(const EventList& events);

    Input input_;
    EventList events_;

    QuitFn on_quit_;
    KeyDownFn on_key_down_;
    KeyUpFn on_key_up_;
    MouseButtonDownFn on_mouse_button_down_;
    MouseButtonUpFn on_mouse_button_up_;
    MouseMoveFn on_mouse_move_;
    MouseWheelFn on_mouse_wheel_;
    WindowResizeFn on_window_resize_;
    SDLEventFn on_sdl_event_;
};

} // namespace platform
} // namespace lumen
