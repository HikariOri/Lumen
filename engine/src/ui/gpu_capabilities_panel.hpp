/**
 * @file gpu_capabilities_panel.hpp
 * @brief GPU Capabilities ImGui 面板：显示物理设备信息与限制
 *
 * 需在 ImGui 帧内调用（NewFrame 之后、Render 之前）。
 */

#pragma once

#include "ui/panel.hpp"

namespace lumen {
namespace render {
class Context;
}
namespace ui {

/**
 * @brief 绘制 GPU Capabilities 面板
 * @param ctx 已初始化 Device 的 Context
 * @param title 窗口标题，默认 "GPU Capabilities"
 * @param p_open 非空时显示关闭按钮，为 false 时不再绘制内容
 */
void imgui_gpu_capabilities_panel(const render::Context &ctx,
                                  const char *title = "GPU Capabilities",
                                  bool *p_open = nullptr);

/**
 * @brief 供 PanelManager 使用的 GPU 信息面板封装
 */
class GpuCapabilitiesPanel final : public IPanel {
public:
    explicit GpuCapabilitiesPanel(const render::Context &ctx);
    void on_imgui_render() override;

private:
    const render::Context *ctx_ { nullptr };
};

} // namespace ui
} // namespace lumen
