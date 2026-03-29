/**
 * @file event_debug.cpp
 * @brief 输入事件调试实现
 */

#include "platform/event.hpp"
#include "platform/event_debug.hpp"
#include "platform/event_pump.hpp"

#include "core/logger.hpp"
#include "ui/imgui_layer.hpp"

#include <imgui.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

namespace lumen::platform {

namespace {

bool imgui_capture_mouse_safe() {
    return ImGui::GetCurrentContext() != nullptr && lumen::ui::imgui_wants_mouse();
}

bool imgui_capture_keyboard_safe() {
    return ImGui::GetCurrentContext() != nullptr &&
           lumen::ui::imgui_wants_keyboard();
}

void log_sdl_event(const void *sdlEvent) {
    if (sdlEvent == nullptr) {
        return;
    }
    const auto &e = *static_cast<const SDL_Event *>(sdlEvent);

    switch (static_cast<SDL_EventType>(e.type)) {
    case SDL_EVENT_KEY_DOWN: {
        const auto &k = e.key;
        const bool imgui = imgui_capture_keyboard_safe();
        LUMEN_LOG_DEBUG("Input KeyDown: {} repeat={} [ImGui: {}]",
                        key_name(static_cast<KeyCode>(k.scancode)), k.repeat,
                        imgui ? "capture" : "game");
        break;
    }
    case SDL_EVENT_KEY_UP: {
        const auto &k = e.key;
        const bool imgui = imgui_capture_keyboard_safe();
        LUMEN_LOG_DEBUG("Input KeyUp: {} [ImGui: {}]",
                        key_name(static_cast<KeyCode>(k.scancode)),
                        imgui ? "capture" : "game");
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const auto &b = e.button;
        const char *btnName = "Unknown";
        switch (b.button) {
        case SDL_BUTTON_LEFT: btnName = "Left"; break;
        case SDL_BUTTON_MIDDLE: btnName = "Middle"; break;
        case SDL_BUTTON_RIGHT: btnName = "Right"; break;
        default: break;
        }
        const bool imgui = imgui_capture_mouse_safe();
        LUMEN_LOG_DEBUG("Input MouseButtonDown: {} at ({:.1f}, {:.1f}) [ImGui: {}]",
                        btnName, b.x, b.y, imgui ? "capture" : "game");
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const auto &b = e.button;
        const char *btnName = "Unknown";
        switch (b.button) {
        case SDL_BUTTON_LEFT: btnName = "Left"; break;
        case SDL_BUTTON_MIDDLE: btnName = "Middle"; break;
        case SDL_BUTTON_RIGHT: btnName = "Right"; break;
        default: break;
        }
        const bool imgui = imgui_capture_mouse_safe();
        LUMEN_LOG_DEBUG("Input MouseButtonUp: {} at ({:.1f}, {:.1f}) [ImGui: {}]",
                        btnName, b.x, b.y, imgui ? "capture" : "game");
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        const auto &m = e.motion;
        const bool imgui = imgui_capture_mouse_safe();
        LUMEN_LOG_DEBUG("Input MouseMove: ({:.1f}, {:.1f}) delta=({:.1f}, {:.1f}) [ImGui: {}]",
                        m.x, m.y, m.xrel, m.yrel, imgui ? "capture" : "game");
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const auto &w = e.wheel;
        float dx = w.x;
        float dy = w.y;
        if (w.direction == SDL_MOUSEWHEEL_FLIPPED) {
            dx = -dx;
            dy = -dy;
        }
        const bool imgui = imgui_capture_mouse_safe();
        LUMEN_LOG_DEBUG("Input MouseWheel: delta=({:.1f}, {:.1f}) [ImGui: {}]",
                        dx, dy, imgui ? "capture" : "game");
        break;
    }
    default:
        break;
    }
}

} // namespace

void add_input_debug_handler(EventPump &pump) {
    pump.add_sdl_event_handler(log_sdl_event);
}

} // namespace lumen::platform
