/**
 * @file event_pump.hpp
 * @brief SDL3 事件轮询：Input 快照 + Hazel 式层链（Overlay / Layer）+ 可选 SDL
 * 钩子
 *
 * 不再提供 `on_key_down` 等按类型的回调；应用在 `push_layer` 内使用
 * `EventDispatcher` 分派，与 Hazel `Application::OnEvent` + `Layer::OnEvent`
 * 一致。
 */

#pragma once

#include "platform/event.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/input.hpp"

namespace lumen {
namespace platform {

/**
 * @class EventPump
 * @brief 每帧 `poll()`：SDL →（可选）原始回调 → `Event` + `Input` → 层链
 *
 * 层链顺序：`push_overlay` 插在队首（最先收到事件）；`push_layer` 追加在队尾。
 * 若某层将 `DispatchableEvent::handled` 置为 true，则**不再**向后续层传递该事件
 *（与 Hazel 在 `Handled` 为 true 时停止向下分发一致）。
 *
 * @code
 * EventPump pump;
 * ui::ImGuiLayer imgui;
 * imgui.attach(pump);
 * pump.set_on_application_event([&](DispatchableEvent& de) {
 *   EventDispatcher d(de);
 *   d.dispatch<EventWindowResize>([&](EventWindowResize& r) { ... });
 * });
 * pump.push_layer([&](DispatchableEvent& de) { ... });
 * while (pump.poll()) { ... }
 * @endcode
 *
 * 与 Hazel `Application::OnEvent` 一致：先执行 **应用级**
 * 回调（窗口关闭、resize 等）， 再自前向后遍历 Overlay/Layer 栈；若 `handled`
 * 则不再向下传递。
 */
class EventPump {
public:
    /**
     * @brief 层回调：等价于 Hazel `Layer::OnEvent(Event&)`
     */
    using EventFn = std::function<void(DispatchableEvent &)>;

    /**
     * @brief 原始 SDL 事件（`ImGui_ImplSDL3_ProcessEvent` 等）
     */
    using SDLEventFn = std::function<void(const void *sdlEvent)>;

    /**
     * @brief Overlay：插入队首，最先处理（`ImGuiLayer::attach` 会注册 ImGui
     * Overlay）
     */
    void push_overlay(EventFn fn) {
        event_stack_.insert(event_stack_.begin(), std::move(fn));
    }

    /**
     * @brief Layer：追加在队尾，在已有 Overlay / Layer 之后处理
     */
    void push_layer(EventFn fn) { event_stack_.push_back(std::move(fn)); }

    /**
     * @brief 应用级事件（对齐 Hazel `Application::OnEvent` 中先于 Layer 栈的
     * `EventDispatcher::Dispatch`）
     *
     * 典型：`EventQuit`、`EventWindowResize`；若将 `handled` 置为 true，后续
     * Overlay/Layer 不会收到该事件。
     */
    void set_on_application_event(EventFn f) {
        on_application_event_ = std::move(f);
    }

    void on_sdl_event(SDLEventFn f) {
        sdl_event_handlers_.clear();
        sdl_event_handlers_.push_back(std::move(f));
    }

    void add_sdl_event_handler(SDLEventFn f) {
        sdl_event_handlers_.push_back(std::move(f));
    }

    /**
     * @return false 表示 `EventQuit` / `SDL_QUIT`，应结束主循环
     */
    bool poll();

    const Input &input() const { return input_; }

private:
    void dispatch_(const EventList &events);

    Input input_;
    EventList events_;
    std::vector<SDLEventFn> sdl_event_handlers_;
    EventFn on_application_event_;
    std::vector<EventFn> event_stack_;
};

} // namespace platform
} // namespace lumen
