/**
 * @file event_pump.cpp
 * @brief EventPump SDL3 实现
 */

#include "platform/event_pump.hpp"
#include "platform/event.hpp"
#include "platform/input.hpp"

namespace lumen::platform {

namespace {

Modifier sdl_mods_to_modifier(uint16_t sdlMods) {
    Modifier m { Modifier::None };
    if (sdlMods & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT))
        m = m | Modifier::Shift;
    if (sdlMods & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))
        m = m | Modifier::Ctrl;
    if (sdlMods & (SDL_KMOD_LALT | SDL_KMOD_RALT))
        m = m | Modifier::Alt;
    if (sdlMods & (SDL_KMOD_LGUI | SDL_KMOD_RGUI))
        m = m | Modifier::Gui;
    return m;
}

MouseButton sdl_button_to_mouse_button(uint8_t btn) {
    switch (btn) {
    case SDL_BUTTON_LEFT: return MouseButton::Left;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT: return MouseButton::Right;
    default: return MouseButton::Left;
    }
}

} // namespace

/// @todo 支持一次性推入多个事件，再一起 dispatch
bool EventPump::poll() {
    events_.clear();
    input_.reset_delta_();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        for (const auto &fn : sdl_event_handlers_) {
            if (fn) {
                fn(&e);
            }
        }
        switch (static_cast<SDL_EventType>(e.type)) {
        case SDL_EVENT_QUIT:
            events_.emplace_back(std::in_place_type<EventQuit>);
            dispatch_(events_);
            return false;

        case SDL_EVENT_KEY_DOWN: {
            auto &k = e.key;
            auto code = static_cast<KeyCode>(k.scancode);
            input_.update_key_(code, true);
            input_.update_modifiers_(static_cast<uint16_t>(k.mod));
            events_.emplace_back(std::in_place_type<EventKeyDown>, code,
                                 k.repeat, sdl_mods_to_modifier(k.mod));
            break;
        }
        case SDL_EVENT_KEY_UP: {
            auto &k = e.key;
            auto code = static_cast<KeyCode>(k.scancode);
            input_.update_key_(code, false);
            input_.update_modifiers_(static_cast<uint16_t>(k.mod));
            events_.emplace_back(std::in_place_type<EventKeyUp>, code,
                                 sdl_mods_to_modifier(k.mod));
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            auto &b = e.button;
            MouseButton btn = sdl_button_to_mouse_button(b.button);
            input_.update_mouse_button_(btn, true);
            input_.update_mouse_position_(b.x, b.y);
            events_.emplace_back(std::in_place_type<EventMouseButtonDown>, btn,
                                 b.x, b.y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            auto &b = e.button;
            MouseButton btn = sdl_button_to_mouse_button(b.button);
            input_.update_mouse_button_(btn, false);
            input_.update_mouse_position_(b.x, b.y);
            events_.emplace_back(std::in_place_type<EventMouseButtonUp>, btn,
                                 b.x, b.y);
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            auto &m = e.motion;
            input_.update_mouse_position_(m.x, m.y);
            input_.mouseX_ = m.x;
            input_.mouseY_ = m.y;
            input_.deltaX_ += m.xrel;
            input_.deltaY_ += m.yrel;
            events_.emplace_back(std::in_place_type<EventMouseMove>, m.x, m.y,
                                 m.xrel, m.yrel);
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            auto &w = e.wheel;
            float dx = w.x;
            float dy = w.y;
            if (w.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dx = -dx;
                dy = -dy;
            }
            events_.emplace_back(std::in_place_type<EventMouseWheel>, dx, dy);
            break;
        }

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            events_.emplace_back(std::in_place_type<EventWindowResize>,
                                 e.window.data1, e.window.data2);
            break;
        }

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            events_.emplace_back(std::in_place_type<EventQuit>);
            dispatch_(events_);
            return false;

        case SDL_EVENT_WINDOW_MINIMIZED:
            events_.emplace_back(std::in_place_type<EventWindowMinimize>);
            break;

        case SDL_EVENT_WINDOW_MAXIMIZED:
            events_.emplace_back(std::in_place_type<EventWindowMaximize>);
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            events_.emplace_back(std::in_place_type<EventWindowRestore>);
            break;

        default: break;
        }
    }

    // 同步键盘/鼠标状态（SDL 权威状态，确保与事件一致）
    int numKeys { 0 };
    const bool *keyState = SDL_GetKeyboardState(&numKeys);
    if (keyState) {
        for (int i { 0 };
             i < numKeys && i < static_cast<int>(Input::k_max_keys); ++i) {
            input_.keys_[i] = keyState[i];
        }
    }
    float mx { 0 }, my { 0 };
    SDL_MouseButtonFlags btnState = SDL_GetMouseState(&mx, &my);
    input_.update_mouse_position_(mx, my);
    input_.mouseLeft_ = (btnState & SDL_BUTTON_LMASK) != 0;
    input_.mouseMiddle_ = (btnState & SDL_BUTTON_MMASK) != 0;
    input_.mouseRight_ = (btnState & SDL_BUTTON_RMASK) != 0;
    input_.update_modifiers_(static_cast<uint16_t>(SDL_GetModState()));

    dispatch_(events_);
    return true;
}

void EventPump::dispatch_(const EventList &events) {
    for (const auto &ev : events) {
        DispatchableEvent de { .event = ev, .handled = false };
        if (on_application_event_) {
            on_application_event_(de);
        }
        if (de.handled) {
            continue;
        }
        for (auto &fn : event_stack_) {
            if (!fn) {
                continue;
            }
            fn(de);
            if (de.handled) {
                break;
            }
        }
    }
}

} // namespace lumen::platform
