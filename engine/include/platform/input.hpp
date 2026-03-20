/**
 * @file input.hpp
 * @brief 输入状态：本帧按键、鼠标位置、 modifiers
 *
 * 由 EventPump 每帧更新，应用层只读查询。
 */

#pragma once

#include "platform/event.hpp"

#include <cstdint>

namespace lumen {
namespace platform {

/**
 * @class Input
 * @brief 当前帧输入状态快照
 *
 * 在 EventPump::poll 中更新，提供按键、鼠标位置、delta 等查询。
 */
class Input {
public:
    Input() = default;

    /// 指定按键是否按下
    [[nodiscard]] bool is_key_down(KeyCode key) const;

    /// 鼠标位置（窗口坐标系）
    [[nodiscard]] void mouse_position(float &x, float &y) const {
        x = mouseX_;
        y = mouseY_;
    }
    [[nodiscard]] float mouse_x() const { return mouseX_; }
    [[nodiscard]] float mouse_y() const { return mouseY_; }

    /// 本帧鼠标位移
    [[nodiscard]] void mouse_delta(float &dx, float &dy) const {
        dx = deltaX_;
        dy = deltaY_;
    }
    [[nodiscard]] float mouse_delta_x() const { return deltaX_; }
    [[nodiscard]] float mouse_delta_y() const { return deltaY_; }

    /// 鼠标按钮是否按下
    [[nodiscard]] bool is_mouse_button_down(MouseButton btn) const;

    /// 当前修饰键
    [[nodiscard]] Modifier modifiers() const { return mods_; }

    /// 检查修饰键
    [[nodiscard]] bool has_shift() const {
        return has_modifier(mods_, Modifier::Shift);
    }
    [[nodiscard]] bool has_ctrl() const {
        return has_modifier(mods_, Modifier::Ctrl);
    }
    [[nodiscard]] bool has_alt() const {
        return has_modifier(mods_, Modifier::Alt);
    }

private:
    friend class EventPump;

    static constexpr size_t k_max_keys = 512;

    float mouseX_ { 0 };
    float mouseY_ { 0 };
    float deltaX_ { 0 };
    float deltaY_ { 0 };
    bool keys_[k_max_keys] {};
    bool mouseLeft_ { false };
    bool mouseMiddle_ { false };
    bool mouseRight_ { false };
    Modifier mods_ { Modifier::None };

    void reset_delta_();
    void update_key_(KeyCode key, bool down);
    void update_mouse_button_(MouseButton btn, bool down);
    void update_mouse_position_(float x, float y);
    void update_modifiers_(uint16_t sdlMods);
};

} // namespace platform
} // namespace lumen
