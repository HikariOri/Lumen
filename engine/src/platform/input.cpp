/**
 * @file input.cpp
 * @brief Input 状态实现
 */

#include "platform/input.hpp"

namespace lumen::platform {

bool Input::is_key_down(KeyCode key) const {
    if (key >= k_max_keys)
        return false;
    return keys_[key];
}

bool Input::is_mouse_button_down(MouseButton btn) const {
    switch (btn) {
    case MouseButton::Left: return mouseLeft_;
    case MouseButton::Middle: return mouseMiddle_;
    case MouseButton::Right: return mouseRight_;
    }
    return false;
}

void Input::reset_delta_() {
    deltaX_ = 0;
    deltaY_ = 0;
}

void Input::update_key_(KeyCode key, bool down) {
    if (key < k_max_keys)
        keys_[key] = down;
}

void Input::update_mouse_button_(MouseButton btn, bool down) {
    switch (btn) {
    case MouseButton::Left: mouseLeft_ = down; break;
    case MouseButton::Middle: mouseMiddle_ = down; break;
    case MouseButton::Right: mouseRight_ = down; break;
    }
}

void Input::update_mouse_position_(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
}

void Input::update_modifiers_(uint16_t sdlMods) {
    mods_ = Modifier::None;
    if (sdlMods & 0x0003u) // SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT
        mods_ = mods_ | Modifier::Shift;
    if (sdlMods & 0x00C0u) // SDL_KMOD_LCTRL | SDL_KMOD_RCTRL
        mods_ = mods_ | Modifier::Ctrl;
    if (sdlMods & 0x0300u) // SDL_KMOD_LALT | SDL_KMOD_RALT
        mods_ = mods_ | Modifier::Alt;
    if (sdlMods & 0x0C00u) // SDL_KMOD_LGUI | SDL_KMOD_RGUI
        mods_ = mods_ | Modifier::Gui;
}

} // namespace lumen::platform
