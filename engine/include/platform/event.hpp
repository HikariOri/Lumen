/**
 * @file event.hpp
 * @brief 平台无关事件类型：键盘、鼠标、窗口
 *
 * 应用层不依赖 SDL3/GLFW，统一使用此抽象。
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace lumen {
namespace platform {

/// 按键码（物理扫描码，与键盘布局无关，适用于游戏）
using KeyCode = uint32_t;

/// 常用按键常量（与 SDL_Scancode 值一致）
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

/// 修饰键掩码
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

constexpr bool has_modifier(Modifier mask, Modifier m) {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(m)) != 0;
}

/// 鼠标按钮
enum class MouseButton : uint8_t { Left = 1, Middle = 2, Right = 3 };

// ============== 事件类型 ==============

struct EventQuit {};

struct EventKeyDown {
    KeyCode key { 0 };
    bool repeat { false };
    Modifier mods { Modifier::None };
};

struct EventKeyUp {
    KeyCode key { 0 };
    Modifier mods { Modifier::None };
};

struct EventMouseButtonDown {
    MouseButton button { MouseButton::Left };
    float x { 0 };
    float y { 0 };
};

struct EventMouseButtonUp {
    MouseButton button { MouseButton::Left };
    float x { 0 };
    float y { 0 };
};

struct EventMouseMove {
    float x { 0 };
    float y { 0 };
    float deltaX { 0 };
    float deltaY { 0 };
};

struct EventMouseWheel {
    float deltaX { 0 };
    float deltaY { 0 };
};

struct EventWindowResize {
    int width { 0 };
    int height { 0 };
};

/// 事件联合类型
using Event = std::variant<EventQuit,
                          EventKeyDown,
                          EventKeyUp,
                          EventMouseButtonDown,
                          EventMouseButtonUp,
                          EventMouseMove,
                          EventMouseWheel,
                          EventWindowResize>;

using EventList = std::vector<Event>;

// ============== 名称查询 ==============

/// 按键名称（如 "W", "Escape"）
std::string_view key_name(KeyCode key);

/// 鼠标按钮名称（如 "Left", "Middle", "Right"）
std::string_view mouse_button_name(MouseButton btn);

/// 修饰键名称（如 "Shift", "Ctrl"）
std::string_view modifier_name(Modifier mod);

/// 事件类型名称（如 "KeyDown", "MouseMove"）
std::string_view event_type_name(const Event& e);

} // namespace platform
} // namespace lumen
