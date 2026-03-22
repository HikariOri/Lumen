/**
 * @file log_panel.cpp
 */

#include "ui/log_panel.hpp"

#include "core/log_view_buffer.hpp"

#include <imgui.h>
#include <spdlog/common.h>

namespace lumen {
namespace ui {
namespace {

ImVec4 level_color(spdlog::level::level_enum lvl) {
    switch (lvl) {
    case spdlog::level::trace:
        return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    case spdlog::level::debug:
        return ImVec4(0.65f, 0.75f, 0.95f, 1.0f);
    case spdlog::level::info:
        return ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
    case spdlog::level::warn:
        return ImVec4(0.95f, 0.85f, 0.40f, 1.0f);
    case spdlog::level::err:
        return ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
    case spdlog::level::critical:
        return ImVec4(0.98f, 0.35f, 0.65f, 1.0f);
    default:
        return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }
}

spdlog::level::level_enum index_to_min_level(int idx) {
    switch (idx) {
    case 0: return spdlog::level::trace;
    case 1: return spdlog::level::debug;
    case 2: return spdlog::level::info;
    case 3: return spdlog::level::warn;
    case 4: return spdlog::level::err;
    case 5: return spdlog::level::critical;
    default: return spdlog::level::info;
    }
}

} // namespace

LogPanel::LogPanel() = default;

void LogPanel::on_imgui_render() {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 320.0f),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Log")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        lumen::core::LogViewBuffer::instance().clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    static const char *const k_level_items[] = {
        "Trace+", "Debug+", "Info+", "Warn+", "Error+", "Critical",
    };
    ImGui::Combo("Min level", &filter_min_level_, k_level_items,
                  IM_ARRAYSIZE(k_level_items));

    const spdlog::level::level_enum min_level =
        index_to_min_level(filter_min_level_);

    const auto lines = lumen::core::LogViewBuffer::instance().snapshot();

    ImGui::Separator();
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("log_scroller", ImVec2(0, -footer), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto &line : lines) {
        if (line.level < min_level) {
            continue;
        }
        if (!line.time.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(0.50f, 0.52f, 0.58f, 1.0f));
            ImGui::TextUnformatted(line.time.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, level_color(line.level));
        ImGui::TextUnformatted("[");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(line.logger.c_str());
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("] ");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(line.message.c_str());
        ImGui::PopStyleColor();
    }

    if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Lines: %zu / cap %zu", lines.size(),
                lumen::core::LogViewBuffer::instance().capacity());

    ImGui::End();
}

} // namespace ui
} // namespace lumen
