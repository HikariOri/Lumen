/**
 * @file event_pump.cpp
 * @brief EventPump SDL3 实现
 */

#include "platform/event_pump.hpp"
#include "platform/event.hpp"
#include "platform/input.hpp"

#include <utility>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

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

bool EventPump::poll() {
    events_.clear();
    input_.reset_delta_();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (on_sdl_event_)
            on_sdl_event_(&e);
        switch (static_cast<SDL_EventType>(e.type)) {
        case SDL_EVENT_QUIT:
            events_.emplace_back(std::in_place_type<EventQuit>);
            dispatch_(events_);
            if (on_quit_)
                on_quit_();
            return false;

        case SDL_EVENT_KEY_DOWN: {
            auto &k = e.key;
            KeyCode code = static_cast<KeyCode>(k.scancode);
            input_.update_key_(code, true);
            input_.update_modifiers_(static_cast<uint16_t>(k.mod));
            events_.emplace_back(std::in_place_type<EventKeyDown>, code,
                                 k.repeat, sdl_mods_to_modifier(k.mod));
            break;
        }
        case SDL_EVENT_KEY_UP: {
            auto &k = e.key;
            KeyCode code = static_cast<KeyCode>(k.scancode);
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
            if (on_quit_)
                on_quit_();
            return false;

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
    for (const auto &e : events) {
        if (std::holds_alternative<EventKeyDown>(e) && on_key_down_) {
            on_key_down_(std::get<EventKeyDown>(e));
        } else if (std::holds_alternative<EventKeyUp>(e) && on_key_up_) {
            on_key_up_(std::get<EventKeyUp>(e));
        } else if (std::holds_alternative<EventMouseButtonDown>(e) &&
                   on_mouse_button_down_) {
            on_mouse_button_down_(std::get<EventMouseButtonDown>(e));
        } else if (std::holds_alternative<EventMouseButtonUp>(e) &&
                   on_mouse_button_up_) {
            on_mouse_button_up_(std::get<EventMouseButtonUp>(e));
        } else if (std::holds_alternative<EventMouseMove>(e) &&
                   on_mouse_move_) {
            on_mouse_move_(std::get<EventMouseMove>(e));
        } else if (std::holds_alternative<EventMouseWheel>(e) &&
                   on_mouse_wheel_) {
            on_mouse_wheel_(std::get<EventMouseWheel>(e));
        } else if (std::holds_alternative<EventWindowResize>(e) &&
                   on_window_resize_) {
            on_window_resize_(std::get<EventWindowResize>(e));
        }
    }
}

} // namespace lumen::platform
