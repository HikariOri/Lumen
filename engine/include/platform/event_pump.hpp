/**
 * @file event_pump.hpp
 * @brief 事件轮询 + 回调：SDL3 实现，poll() 拉取并分发
 *
 * 提供一个平台无关的事件轮询器，用于：
 * - 从 SDL3 事件队列获取事件
 * - 更新输入快照（Input）
 * - 分发给注册的回调
 *
 * EventPump 应在 Window 创建之后调用（SDL_Init 已执行），
 * 并在主循环中每帧调用 poll() 处理事件。
 */

#pragma once

#include "platform/event.hpp"
#include "platform/input.hpp"

#include <functional>
#include <vector>

namespace lumen {
namespace platform {

/**
 * @class EventPump
 * @brief 事件轮询器：拉取事件、更新 Input、分发到回调
 *
 * 每帧调用 poll() 会：
 *   - 使用 SDL_PollEvent() 从事件队列中获取所有 pending 事件
 *   - 将事件转换为平台无关的 Event 类型
 *   - 更新 internal Input 状态（按键、鼠标等）
 *   - 调用已注册的各类事件回调
 *
 * 使用示例：
 * @code
 * EventPump pump;
 * pump.on_quit([] { running = false; });
 * pump.on_key_down([](const EventKeyDown &e) { ... });
 * while (pump.poll()) {
 *     if (pump.input().is_key_down(Key::W)) ...
 * }
 * @endcode
 *
 * @note SDL_PollEvent() 将事件从 SDL 内部队列弹出，并且会隐式调用
 * SDL_PumpEvents() 来填充队列。:contentReference[oaicite:1]{index=1}
 */
class EventPump {
public:
    using QuitFn = std::function<void()>;
    using KeyDownFn = std::function<void(const EventKeyDown &)>;
    using KeyUpFn = std::function<void(const EventKeyUp &)>;
    using MouseButtonDownFn = std::function<void(const EventMouseButtonDown &)>;
    using MouseButtonUpFn = std::function<void(const EventMouseButtonUp &)>;
    using MouseMoveFn = std::function<void(const EventMouseMove &)>;
    using MouseWheelFn = std::function<void(const EventMouseWheel &)>;
    using WindowResizeFn = std::function<void(const EventWindowResize &)>;
    using WindowMinimizeFn = std::function<void(const EventWindowMinimize &)>;
    using WindowMaximizeFn = std::function<void(const EventWindowMaximize &)>;
    using WindowRestoreFn = std::function<void(const EventWindowRestore &)>;

    /**
     * @brief 原始 SDL 事件回调（用于 ImGui 等插件）
     *
     * 每次处理 SDL_Event 时都会调用该回调。
     * 该回调接收未经转换的 SDL_Event 指针。
     */
    using SDLEventFn = std::function<void(const void *sdlEvent)>;

    /**
     * @brief 设置 quit 事件的处理函数
     * @param f 回调函数，当收到退出事件 (EventQuit) 时调用
     */
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
    void on_window_minimize(WindowMinimizeFn f) {
        on_window_minimize_ = std::move(f);
    }
    void on_window_maximize(WindowMaximizeFn f) {
        on_window_maximize_ = std::move(f);
    }
    void on_window_restore(WindowRestoreFn f) {
        on_window_restore_ = std::move(f);
    }

    /**
     * @brief 设置唯一 SDL 事件回调
     *
     * 该操作会清空之前通过 add_sdl_event_handler 添加的所有 SDL 事件回调，
     * 并将当前事件回调。回调设置为唯一的 SDL
     *
     * @param f 原始 SDL 事件处理函数
     */
    void on_sdl_event(SDLEventFn f) {
        sdl_event_handlers_.clear();
        sdl_event_handlers_.push_back(std::move(f));
    }

    /**
     * @brief 添加 SDL 事件回调
     *
     * 在处理 SDL3 事件时，会依次调用所有注册的回调。
     * 与 on_sdl_event 互斥：后者调用会清空之前的所有回调。
     *
     * @param f 原始 SDL 事件处理函数
     */
    void add_sdl_event_handler(SDLEventFn f) {
        sdl_event_handlers_.push_back(std::move(f));
    }

    /**
     * @brief 轮询事件、更新 Input、分发到所有回调
     *
     * 该函数通常在主循环中每帧调用一次：
     * - 从 SDL 事件队列 获取所有事件
     * - 将 SDL 事件转换为平台无关 Event 类型
     * - 调用对应的业务回调
     * - 更新输入状态快照（Input）
     *
     * @return false 表示收到退出请求 (EventQuit) 或 SDL_QUIT，应结束主循环
     */
    bool poll();

    /**
     * @brief 获取当前帧输入状态
     *
     * poll() 调用之后，Input 类型将包含本帧的按键、鼠标等状态，
     * 可用于游戏逻辑查询（如 is_key_down）。
     *
     * @return const Input& 当前输入快照
     */
    const Input &input() const { return input_; }

private:
    /**
     * @brief 内部事件分发器
     *
     * 将已收集的 EventList 中的所有事件分发给对应的回调。
     *
     * @param events 事件列表
     */
    void dispatch_(const EventList &events);

    Input input_;      ///< 本帧输入快照
    EventList events_; ///< 本帧收集的事件

    QuitFn on_quit_;                         ///< quit 事件回调
    KeyDownFn on_key_down_;                  ///< key down 事件回调
    KeyUpFn on_key_up_;                      ///< key up 事件回调
    MouseButtonDownFn on_mouse_button_down_; ///< 鼠标按下事件回调
    MouseButtonUpFn on_mouse_button_up_;     ///< 鼠标释放事件回调
    MouseMoveFn on_mouse_move_;              ///< 鼠标移动事件回调
    MouseWheelFn on_mouse_wheel_;            ///< 鼠标滚轮事件回调
    WindowResizeFn on_window_resize_;        ///< 窗口大小改变事件回调
    WindowMinimizeFn on_window_minimize_;    ///< 窗口最小化事件回调
    WindowMaximizeFn on_window_maximize_;    ///< 窗口最大化事件回调
    WindowRestoreFn on_window_restore_;      ///< 窗口恢复事件回调

    std::vector<SDLEventFn> sdl_event_handlers_; ///< 原始 SDL 事件回调列表
};

} // namespace platform
} // namespace lumen
