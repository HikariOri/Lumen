/**
 * @file input_bridge.hpp
 * @brief 输入桥接：连接 ImGui 与平台事件/输入系统
 *
 * 提供 SDL→ImGui 事件转发、WantCapture 查询，实现分层输入：
 * 1. SDL3 原始事件 → ImGui 先处理
 * 2. 平台 EventPump 更新 Input 状态
 * 3. 游戏逻辑根据 imgui_wants_* 决定是否使用输入
 */

#pragma once

namespace lumen {
namespace platform {
class EventPump;
}
namespace ui {

/**
 * @brief 将 SDL 事件转发给 ImGui（供 on_sdl_event 使用）
 * @param sdlEvent 指向 SDL_Event 的指针
 */
void imgui_process_sdl_event(const void *sdlEvent);

/**
 * @brief ImGui 是否想捕获鼠标（悬停在 ImGui 控件上）
 * @return true 表示游戏逻辑应忽略鼠标输入
 */
bool imgui_wants_mouse();

/**
 * @brief ImGui 是否想捕获键盘（聚焦在 ImGui 控件上）
 * @return true 表示游戏逻辑应忽略键盘输入
 */
bool imgui_wants_keyboard();

/**
 * @brief ImGui 是否想捕获任一输入
 * @return imgui_wants_mouse() || imgui_wants_keyboard()
 */
bool imgui_wants_any_input();

/**
 * @brief 为 EventPump 注册 ImGui 事件转发
 *
 * 等价于 pump.on_sdl_event(imgui_process_sdl_event)。
 * 应在 imgui_backend_init 之后调用。
 *
 * @param pump 事件泵
 */
void imgui_setup_event_pump(platform::EventPump &pump);

} // namespace ui
} // namespace lumen
