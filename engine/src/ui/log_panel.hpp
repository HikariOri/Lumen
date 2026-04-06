/**
 * @file log_panel.hpp
 * @brief 从 LogViewBuffer 读取并显示引擎 / 应用日志的 ImGui 面板
 */

#pragma once

#include "ui/panel.hpp"

namespace lumen {
namespace ui {

class LogPanel final : public IPanel {
public:
    LogPanel();
    void on_imgui_render() override;

private:
    int filter_min_level_ { 0 }; // spdlog::level::level_enum
    bool auto_scroll_ { true };
};

} // namespace ui
} // namespace lumen
