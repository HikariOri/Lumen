/**
 * @file input.cpp
 * @brief Input 状态实现
 */

#include "platform/input.hpp"

namespace lumen::platform {

bool Input::is_key_down(KeyCode key) const {
    if (key >= k_max_keys) {
        return false;
    }
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
    if (key < k_max_keys) {
        keys_[key] = down;
    }
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

    constexpr auto SDL_SHIFT = SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT;
    constexpr auto SDK_CTRL = SDL_KMOD_LCTRL | SDL_KMOD_RCTRL;
    constexpr auto SDL_ALT = SDL_KMOD_LALT | SDL_KMOD_RALT;
    constexpr auto SDL_GUI = SDL_KMOD_LGUI | SDL_KMOD_RGUI;

    if (sdlMods & SDL_SHIFT)
        mods_ = mods_ | Modifier::Shift;
    if (sdlMods & SDK_CTRL)
        mods_ = mods_ | Modifier::Ctrl;
    if (sdlMods & SDL_ALT)
        mods_ = mods_ | Modifier::Alt;
    if (sdlMods & SDL_GUI)
        mods_ = mods_ | Modifier::Gui;
}

} // namespace lumen::platform
