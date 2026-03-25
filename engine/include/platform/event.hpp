/**
 * @file event.hpp
 * @brief 平台无关事件类型：键盘、鼠标、窗口
 *
 * 提供跨平台事件抽象，应用层不依赖 SDL3
 * 等底层具体事件系统。
 *
 * 事件类型包括：
 * - 键盘事件
 * - 鼠标事件
 * - 窗口大小变化等
 *
 * 应用层通过 EventPump 获取当前帧事件列表（EventList）。
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace lumen {
namespace platform {

/**
 * @brief 按键码（物理扫描码）
 *
 * 与键盘布局无关，适用于游戏输入映射。
 */
using KeyCode = uint32_t;

/**
 * @namespace Key
 * @brief 常用按键常量，与 SDL3 Scancode 映射一致
 *
 * 这些常量代表物理按键位置，对应 SDL 的 scancode 信息。
 * 在 SDL 事件中，获取的键码可以通过这些值匹配。
 */
namespace Key {
constexpr KeyCode Unknown = 0;
constexpr KeyCode A = 4;
constexpr KeyCode B = 5;
constexpr KeyCode C = 6;
constexpr KeyCode D = 7;
constexpr KeyCode E = 8;
constexpr KeyCode F = 9;
constexpr KeyCode G = 10;
constexpr KeyCode H = 11;
constexpr KeyCode I = 12;
constexpr KeyCode J = 13;
constexpr KeyCode K = 14;
constexpr KeyCode L = 15;
constexpr KeyCode M = 16;
constexpr KeyCode N = 17;
constexpr KeyCode O = 18;
constexpr KeyCode P = 19;
constexpr KeyCode Q = 20;
constexpr KeyCode R = 21;
constexpr KeyCode S = 22;
constexpr KeyCode T = 23;
constexpr KeyCode U = 24;
constexpr KeyCode V = 25;
constexpr KeyCode W = 26;
constexpr KeyCode X = 27;
constexpr KeyCode Y = 28;
constexpr KeyCode Z = 29;
constexpr KeyCode Num1 = 30;
constexpr KeyCode Num2 = 31;
constexpr KeyCode Num3 = 32;
constexpr KeyCode Num4 = 33;
constexpr KeyCode Num5 = 34;
constexpr KeyCode Num6 = 35;
constexpr KeyCode Num7 = 36;
constexpr KeyCode Num8 = 37;
constexpr KeyCode Num9 = 38;
constexpr KeyCode Num0 = 39;
constexpr KeyCode Return = 40;
constexpr KeyCode Escape = 41;
constexpr KeyCode Backspace = 42;
constexpr KeyCode Tab = 43;
constexpr KeyCode Space = 44;
constexpr KeyCode LShift = 225;
constexpr KeyCode RShift = 229;
constexpr KeyCode LCtrl = 224;
constexpr KeyCode RCtrl = 228;
constexpr KeyCode LAlt = 226;
constexpr KeyCode RAlt = 230;
constexpr KeyCode Left = 80;
constexpr KeyCode Right = 79;
constexpr KeyCode Up = 82;
constexpr KeyCode Down = 81;
} // namespace Key

/**
 * @brief 修饰键掩码
 *
 * 支持组合，如 Shift、Ctrl、Alt、Gui。
 */
enum class Modifier : uint16_t {
    None = 0,
    Shift = 1 << 0,
    Ctrl = 1 << 1,
    Alt = 1 << 2,
    Gui = 1 << 3,
};

constexpr Modifier operator|(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<uint16_t>(a) |
                                 static_cast<uint16_t>(b));
}

/**
 * @brief 组合修饰键
 *
 * 用于检测修饰键状态是否包含某一位。
 *
 * @param mask 当前修饰键掩码
 * @param m 待检测的修饰键
 */
constexpr bool has_modifier(Modifier mask, Modifier m) {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(m)) != 0;
}

constexpr bool has_shift(Modifier m) {
    return has_modifier(Modifier::Shift, m);
}

constexpr bool has_ctrl(Modifier m) { return has_modifier(Modifier::Ctrl, m); }

constexpr bool has_alt(Modifier m) { return has_modifier(Modifier::Alt, m); }

constexpr bool has_gui(Modifier m) { return has_modifier(Modifier::Gui, m); }

/**
 * @brief 鼠标按钮
 */
enum class MouseButton : uint8_t {
    Left = 1,   ///< 左键
    Middle = 2, ///< 中键
    Right = 3   ///< 右键
};

// ============== 事件类型 ==============

struct EventQuit {};

/**
 * @brief 键盘按下事件
 */
struct EventKeyDown {
    KeyCode key { 0 };                ///< 被按下的键
    bool repeat { false };            ///< 是否是重复按下
    Modifier mods { Modifier::None }; ///< 当前修饰键状态
};

/**
 * @brief 键盘释放事件
 */
struct EventKeyUp {
    KeyCode key { 0 };                ///< 被释放的键
    Modifier mods { Modifier::None }; ///< 当前修饰键状态
};

/**
 * @brief 鼠标按钮按下事件
 */
struct EventMouseButtonDown {
    MouseButton button { MouseButton::Left }; ///< 鼠标按钮
    float x { 0 };                            ///< 鼠标在窗口内横坐标
    float y { 0 };                            ///< 鼠标在窗口内纵坐标
};

/**
 * @brief 鼠标按钮释放事件
 */
struct EventMouseButtonUp {
    MouseButton button { MouseButton::Left }; ///< 鼠标按钮
    float x { 0 };                            ///< 鼠标在窗口内横坐标
    float y { 0 };                            ///< 鼠标在窗口内纵坐标
};

/**
 * @brief 鼠标移动事件
 */
struct EventMouseMove {
    float x { 0 };      ///< 当前鼠标横坐标
    float y { 0 };      ///< 当前鼠标纵坐标
    float deltaX { 0 }; ///< 本次移动的 x 方向偏移
    float deltaY { 0 }; ///< 本次移动的 y 方向偏移
};

/**
 * @brief 鼠标滚轮事件
 */
struct EventMouseWheel {
    float deltaX { 0 }; ///< 鼠标水平滚动量
    float deltaY { 0 }; ///< 鼠标垂直滚动量
};

/**
 * @brief 窗口大小改变事件
 *
 * 与 SDL_WINDOWEVENT_RESIZED / SDL_EVENT_WINDOW_SIZE_CHANGED 对应。
 */
struct EventWindowResize {
    int width { 0 };  ///< 新宽度
    int height { 0 }; ///< 新高度
};

/**
 * @struct EventWindowMinimized
 * @brief 窗口最小化事件
 *
 * 当窗口被最小化时（用户点击最小化按钮，
 * 或调用 SDL_MinimizeWindow），SDL3 会发出该事件。
 *
 * @note 这不是 resize 事件；它表示窗口当前状态变为最小化。
 *       可以在应用逻辑层处理暂停渲染/音频等行为。
 * @see SDL_MinimizeWindow
 */
struct EventWindowMinimize {};

/**
 * @struct EventWindowMaximized
 * @brief 窗口最大化事件
 *
 * 当窗口被最大化时（用户点击最大化按钮，
 * 或调用 SDL_MaximizeWindow），SDL3 会发出该事件。
 *
 * @note 这不是 resize 事件。虽然最大化通常会改变窗口尺寸，
 *       你仍应结合 EventWindowResize 或像 SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
 *       事件来获取最终尺寸。
 * @see SDL_MaximizeWindow
 */
struct EventWindowMaximize {};

/**
 * @struct EventWindowRestored
 * @brief 窗口恢复事件
 *
 * 当窗口从最小化或最大化状态恢复为标准大小/位置时，
 * SDL3 会发出该事件。
 *
 * @note 通常伴随一个 EventWindowResize 或 SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
 *       事件，用于通知恢复后的新尺寸。
 * @see SDL_RestoreWindow
 */
struct EventWindowRestore {};

/**
 * @brief 所有平台无关事件的联合类型
 *
 * 使用 std::variant 存储任意一个事件结构体。
 */
using Event =
    std::variant<EventQuit, EventKeyDown, EventKeyUp, EventMouseButtonDown,
                 EventMouseButtonUp, EventMouseMove, EventMouseWheel,
                 EventWindowResize, EventWindowMinimize, EventWindowMaximize,
                 EventWindowRestore>;

/**
 * @brief 事件列表
 *
 * 应由 EventPump 每帧填充，并供上层查询。
 */
using EventList = std::vector<Event>;

// ============== 名称查询 ==============

/**
 * @brief 查询按键名称
 *
 * 返回与 KeyCode 对应的可读名称（如 "W", "Escape"）。
 * 对于常规字符和控制键，都有直观名称。
 *
 * @param key 要查询的键码
 * @return 对应按键名称字符串视图
 */
std::string_view key_name(KeyCode key);

/**
 * @brief 查询鼠标按钮名称
 *
 * 如 "Left", "Middle", "Right"。
 *
 * @param btn 鼠标按钮
 * @return 可读鼠标按钮名称
 */
std::string_view mouse_button_name(MouseButton btn);

/**
 * @brief 查询修饰键名称
 *
 * 返回修饰键的字符串，如 "Shift", "Ctrl"。
 *
 * @param mod 修饰键掩码
 * @return 修饰键名称
 */
std::string_view modifier_name(Modifier mod);

/**
 * @brief 查询事件类型名称
 *
 * 返回事件类型对应的名称，如 "KeyDown", "MouseMove"。
 *
 * @param e 事件 variant
 * @return 事件类型名称
 */
std::string_view event_type_name(const Event &e);

} // namespace platform
} // namespace lumen
