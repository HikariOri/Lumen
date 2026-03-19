/**
 * @file event_pump.cpp
 * @brief EventPump SDL3 实现
 */

#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/input.hpp"

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
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    default:
        return MouseButton::Left;
    }
}

} // namespace

bool EventPump::poll(EventList& events, Input& input) {
    events.clear();
    input.reset_delta_();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (static_cast<SDL_EventType>(e.type)) {
        case SDL_EVENT_QUIT:
            events.push_back(EventQuit {});
            return false;

        case SDL_EVENT_KEY_DOWN: {
            auto& k = e.key;
            KeyCode code = static_cast<KeyCode>(k.scancode);
            input.update_key_(code, true);
            input.update_modifiers_(static_cast<uint16_t>(k.mod));
            events.push_back(
                EventKeyDown { code, k.repeat, sdl_mods_to_modifier(k.mod) });
            break;
        }
        case SDL_EVENT_KEY_UP: {
            auto& k = e.key;
            KeyCode code = static_cast<KeyCode>(k.scancode);
            input.update_key_(code, false);
            input.update_modifiers_(static_cast<uint16_t>(k.mod));
            events.push_back(EventKeyUp { code, sdl_mods_to_modifier(k.mod) });
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            auto& b = e.button;
            MouseButton btn = sdl_button_to_mouse_button(b.button);
            input.update_mouse_button_(btn, true);
            input.update_mouse_position_(b.x, b.y);
            events.push_back(
                EventMouseButtonDown { btn, b.x, b.y });
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            auto& b = e.button;
            MouseButton btn = sdl_button_to_mouse_button(b.button);
            input.update_mouse_button_(btn, false);
            input.update_mouse_position_(b.x, b.y);
            events.push_back(EventMouseButtonUp { btn, b.x, b.y });
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            auto& m = e.motion;
            input.update_mouse_position_(m.x, m.y);
            input.mouseX_ = m.x;
            input.mouseY_ = m.y;
            input.deltaX_ += m.xrel;
            input.deltaY_ += m.yrel;
            events.push_back(EventMouseMove { m.x, m.y, m.xrel, m.yrel });
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            auto& w = e.wheel;
            float dx = w.x;
            float dy = w.y;
            if (w.direction == SDL_MOUSEWHEEL_FLIPPED) {
                dx = -dx;
                dy = -dy;
            }
            events.push_back(EventMouseWheel { dx, dy });
            break;
        }

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            events.push_back(
                EventWindowResize { e.window.data1, e.window.data2 });
            break;
        }

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            events.push_back(EventQuit {});
            return false;

        default:
            break;
        }
    }

    // 同步键盘/鼠标状态（SDL 权威状态，确保与事件一致）
    int numKeys { 0 };
    const bool* keyState = SDL_GetKeyboardState(&numKeys);
    if (keyState) {
        for (int i { 0 }; i < numKeys && i < static_cast<int>(Input::k_max_keys);
             ++i) {
            input.keys_[i] = keyState[i];
        }
    }
    float mx { 0 }, my { 0 };
    SDL_MouseButtonFlags btnState = SDL_GetMouseState(&mx, &my);
    input.update_mouse_position_(mx, my);
    input.mouseLeft_ = (btnState & SDL_BUTTON_LMASK) != 0;
    input.mouseMiddle_ = (btnState & SDL_BUTTON_MMASK) != 0;
    input.mouseRight_ = (btnState & SDL_BUTTON_RMASK) != 0;
    input.update_modifiers_(static_cast<uint16_t>(SDL_GetModState()));

    return true;
}

} // namespace lumen::platform
