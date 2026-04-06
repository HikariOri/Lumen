/**
 * @file panel.hpp
 * @brief ImGui 面板接口与集中绘制（无指令行；ECS 相关面板见 scene_*_panel）
 */

#pragma once

#include <memory>
#include <vector>

namespace lumen {
namespace ui {

class IPanel {
public:
    virtual ~IPanel() = default;
    virtual void on_imgui_render() = 0;
};

class PanelManager {
public:
    void add(std::unique_ptr<IPanel> panel);
    void set_default_dock_id(unsigned int dock_id);

    /// 在 `imgui_backend_new_frame()` 之后、`imgui_backend_render()` 之前调用
    void render_all();

private:
    std::vector<std::unique_ptr<IPanel>> panels_;
    unsigned int default_dock_id_ { 0 };
};

} // namespace ui
} // namespace lumen
