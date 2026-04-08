/**
 * @file input.hpp
 * @brief 输入状态快照：本帧按键、鼠标位置和修饰键状态
 *
 * 该类由 EventPump 每帧更新，用于存储当前帧的输入状态，
 * 包括按下键、鼠标位置、鼠标位移、按钮以及修饰键状态。
 * 应用层只读查询，不应主动修改。
 */
#pragma once

#include "platform/event.hpp"

namespace lumen {
namespace platform {

/**
 * @class Input
 * @brief 当前帧输入状态快照
 *
 * Input 由 EventPump 在事件循环中更新内部状态。
 * 提供按键、鼠标位置、鼠标位移以及修饰键查询接口。
 *
 * @note SDL3 在内部会在 PollEvent/ PumpEvents 后更新状态，
 *       包括键盘状态数组和鼠标状态等。:contentReference[oaicite:1]{index=1}
 */
class Input {
public:
    Input() = default;

    /**
     * @brief 检查指定按键是否按下
     *
     * @param key 要查询的键码（KeyCode 类型）
     * @return 如果当前帧该键处于按下状态则返回 true
     *
     * @note 使用事件驱动更新方式维护状态，而不是每次调用 SDL_GetKeyboardState。
     */
    [[nodiscard]] bool is_key_down(KeyCode key) const;

    /**
     * @brief 获取当前鼠标位置（窗口坐标系）
     *
     * @param x 输出参数，鼠标当前横坐标
     * @param y 输出参数，鼠标当前纵坐标
     */
    [[nodiscard]] void mouse_position(float &x, float &y) const {
        x = mouseX_;
        y = mouseY_;
    }

    /**
     * @brief 获取当前鼠标横坐标
     */
    [[nodiscard]] float mouse_x() const { return mouseX_; }

    /**
     * @brief 获取当前鼠标纵坐标
     */
    [[nodiscard]] float mouse_y() const { return mouseY_; }

    /**
     * @brief 获取本帧鼠标位移
     *
     * @param dx 输出参数，本帧在 x 方向移动的位移
     * @param dy 输出参数，本帧在 y 方向移动的位移
     */
    [[nodiscard]] void mouse_delta(float &dx, float &dy) const {
        dx = deltaX_;
        dy = deltaY_;
    }

    /**
     * @brief 获取鼠标本帧在 x 方向移动的位移
     */
    [[nodiscard]] float mouse_delta_x() const { return deltaX_; }

    /**
     * @brief 获取鼠标本帧在 y 方向移动的位移
     */
    [[nodiscard]] float mouse_delta_y() const { return deltaY_; }

    /**
     * @brief 检查鼠标按钮是否按下
     *
     * @param btn 要检查的鼠标按钮
     * @return 鼠标按钮当前是否按下
     *
     * @note SDL_Event 中会包含鼠标按钮事件，可用于更新状态。
     */
    [[nodiscard]] bool is_mouse_button_down(MouseButton btn) const;

    /**
     * @brief 获取当前修饰键状态
     *
     * 修饰键包括 Shift / Ctrl / Alt 等，用于组合输入检测。
     *
     * @return 当前修饰键状态
     */
    [[nodiscard]] Modifier modifiers() const { return mods_; }

    /**
     * @brief 是否包含 Shift 修饰键
     */
    [[nodiscard]] bool has_shift() const {
        return has_modifier(mods_, Modifier::Shift);
    }

    /**
     * @brief 是否包含 Ctrl 修饰键
     */
    [[nodiscard]] bool has_ctrl() const {
        return has_modifier(mods_, Modifier::Ctrl);
    }

    /**
     * @brief 是否包含 Alt 修饰键
     */
    [[nodiscard]] bool has_alt() const {
        return has_modifier(mods_, Modifier::Alt);
    }

private:
    friend class EventPump;

    static constexpr size_t k_max_keys = 512;

    float mouseX_ { 0 }; ///< 当前鼠标横坐标（像素）
    float mouseY_ { 0 }; ///< 当前鼠标纵坐标（像素）

    float deltaX_ { 0 }; ///< 本帧鼠标 x 轴移动量
    float deltaY_ { 0 }; ///< 本帧鼠标 y 轴移动量

    bool keys_[k_max_keys] {}; ///< 键盘按键状态数组，按键是否按下

    bool mouseLeft_ { false };   ///< 左键按下状态
    bool mouseMiddle_ { false }; ///< 中键按下状态
    bool mouseRight_ { false };  ///< 右键按下状态

    Modifier mods_ { Modifier::None }; ///< 当前修饰键状态

    /**
     * @brief 内部：重置本帧的鼠标位移
     *
     * EventPump 在每帧开始时调用。
     */
    void reset_delta_();

    /**
     * @brief 内部：更新按键状态
     *
     * @param key 键码
     * @param down 是否按下
     */
    void update_key_(KeyCode key, bool down);

    /**
     * @brief 内部：更新鼠标按钮状态
     *
     * @param btn 鼠标按钮
     * @param down 按下或释放
     */
    void update_mouse_button_(MouseButton btn, bool down);

    /**
     * @brief 内部：更新鼠标位置
     *
     * @param x 新横坐标
     * @param y 新纵坐标
     */
    void update_mouse_position_(float x, float y);

    /**
     * @brief 内部：更新修饰键状态
     *
     * @param sdlMods SDL 原始修饰键位掩码
     */
    void update_modifiers_(uint16_t sdlMods);
};

} // namespace platform
} // namespace lumen
