/**
 * @file event_debug.hpp
 * @brief 输入事件调试：将鼠标键盘事件输出到日志
 *
 * 通过 add_sdl_event_handler 注册，不影响现有回调。
 * 日志含 [ImGui: capture] 或 [ImGui: game]，表示该帧输入被 ImGui 捕获或由游戏处理。
 * 使用 LUMEN_LOG_DEBUG，Release 下无输出。需在 imgui_setup_event_pump 之后调用。
 */

#pragma once

namespace lumen {
namespace platform {

class EventPump;

/**
 * @brief 注册输入调试 handler，将键盘鼠标事件输出到 engine 日志
 *
 * 会 add_sdl_event_handler，与 ImGui、应用回调共存。
 * 日志级别为 Debug，需 Logger 已初始化。
 *
 * @param pump 事件泵
 */
void add_input_debug_handler(EventPump &pump);

} // namespace platform
} // namespace lumen
