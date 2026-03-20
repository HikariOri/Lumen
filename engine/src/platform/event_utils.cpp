/**
 * @file event_utils.cpp
 * @brief 事件/按键名称：key_name, event_type_name 等
 */

#include "platform/event.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

namespace lumen::platform {

std::string_view key_name(KeyCode key) {
    const char *name = SDL_GetScancodeName(static_cast<SDL_Scancode>(key));
    if (name && name[0] != '\0') {
        return name;
    }
    return "Unknown";
}

std::string_view mouse_button_name(MouseButton btn) {
    switch (btn) {
    case MouseButton::Left: return "Left";
    case MouseButton::Middle: return "Middle";
    case MouseButton::Right: return "Right";
    }
    return "Unknown";
}

std::string_view modifier_name(Modifier mod) {
    if (mod == Modifier::None) {
        return "None";
    }
    // 组合修饰键：返回第一个非 None 的名称，简化处理
    if (has_modifier(mod, Modifier::Shift))
        return "Shift";
    if (has_modifier(mod, Modifier::Ctrl))
        return "Ctrl";
    if (has_modifier(mod, Modifier::Alt))
        return "Alt";
    if (has_modifier(mod, Modifier::Gui))
        return "Gui";
    return "None";
}

std::string_view event_type_name(const Event &e) {
    return std::visit(
        [](auto &&arg) -> std::string_view {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, EventQuit>)
                return "Quit";
            if constexpr (std::is_same_v<T, EventKeyDown>)
                return "KeyDown";
            if constexpr (std::is_same_v<T, EventKeyUp>)
                return "KeyUp";
            if constexpr (std::is_same_v<T, EventMouseButtonDown>)
                return "MouseButtonDown";
            if constexpr (std::is_same_v<T, EventMouseButtonUp>)
                return "MouseButtonUp";
            if constexpr (std::is_same_v<T, EventMouseMove>)
                return "MouseMove";
            if constexpr (std::is_same_v<T, EventMouseWheel>)
                return "MouseWheel";
            if constexpr (std::is_same_v<T, EventWindowResize>)
                return "WindowResize";
            return "Unknown";
        },
        e);
}

} // namespace lumen::platform
