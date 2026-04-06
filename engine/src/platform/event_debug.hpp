/**
 * @file event_debug.hpp
 * @brief 输入事件调试工具：将键盘与鼠标事件输出到日志系统
 *
 * 该模块用于调试输入系统，通过注册额外的 SDL 事件 handler，
 * 将输入事件打印到引擎日志中，不会影响现有事件回调链。
 *
 * 日志格式会包含：
 * - [ImGui: capture] 表示当前帧输入被 ImGui 捕获
 * - [ImGui: game]    表示输入传递给游戏逻辑处理
 *
 * 注意事项：
 * - 使用 LUMEN_LOG_DEBUG 输出，Release 构建下默认无日志
 * - 依赖 ImGui 事件状态，必须在 `ImGuiLayer::attach` 之后调用
 */

#pragma once

#include "platform/event_pump.hpp"

namespace lumen {
namespace platform {

/**
 * @brief 注册输入调试处理器（Input Debug Handler）
 *
 * 向 EventPump 中添加一个额外的 SDL 事件处理回调，用于：
 * - 打印键盘事件（按下 / 释放）
 * - 打印鼠标事件（移动 / 按键 / 滚轮）
 * - 标记当前输入是否被 ImGui 捕获
 *
 * 该 handler：
 * - 与 ImGui handler、应用层 handler 共存
 * - 不会中断或吞掉事件（只做观测）
 *
 * 使用场景：
 * - 调试输入系统是否正确分发
 * - 排查 ImGui 与游戏输入冲突
 * - 验证事件顺序与帧行为
 *
 * @param pump 事件泵（EventPump 实例），用于注册 handler
 */
void add_input_debug_handler(EventPump &pump);

} // namespace platform
} // namespace lumen
