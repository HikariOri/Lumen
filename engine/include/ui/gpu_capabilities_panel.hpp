/**
 * @file gpu_capabilities_panel.hpp
 * @brief GPU Capabilities ImGui 面板：显示物理设备信息与限制
 *
 * 需在 ImGui 帧内调用（NewFrame 之后、Render 之前）。
 */

#pragma once

namespace lumen {
namespace render {
class Context;
}
namespace ui {

/**
 * @brief 绘制 GPU Capabilities 面板
 * @param ctx 已初始化 Device 的 Context
 * @param title 窗口标题，默认 "GPU Capabilities"
 */
void imgui_gpu_capabilities_panel(const render::Context &ctx,
                                  const char *title = "GPU Capabilities");

} // namespace ui
} // namespace lumen
