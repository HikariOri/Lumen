/**
 * @file panel_manager.cpp
 */

#include "ui/panel.hpp"

#include <imgui.h>
#include <utility>

namespace lumen {
namespace ui {

void PanelManager::add(std::unique_ptr<IPanel> panel) {
    if (panel) {
        panels_.push_back(std::move(panel));
    }
}

void PanelManager::set_default_dock_id(unsigned int dock_id) {
    default_dock_id_ = dock_id;
}

void PanelManager::render_all() {
    for (auto &p : panels_) {
        if (default_dock_id_ != 0) {
            ImGui::SetNextWindowDockID(default_dock_id_,
                                       ImGuiCond_FirstUseEver);
        }
        p->on_imgui_render();
    }
}

} // namespace ui
} // namespace lumen
