/**
 * @file imgui_hazel_helpers.cpp
 * @brief Hazel 风格 ImGui 控件（对齐 TheCherno/Hazel SceneHierarchyPanel）
 */

#include "ui/imgui_hazel_helpers.hpp"

#include <algorithm>

#include <imgui.h>

namespace lumen::ui {

bool imgui_hazel_draw_vec3(const char *label, glm::vec3 &values,
                           float reset_value, float column_width,
                           float drag_speed, const char *fmt, float v_min,
                           float v_max) {
    ImGui::PushID(label);

    ImGuiIO &io = ImGui::GetIO();
    ImFont *label_font = io.FontDefault;
    if (label_font == nullptr && io.Fonts->Fonts.Size > 0) {
        label_font = io.Fonts->Fonts[0];
    }

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, column_width);
    ImGui::TextUnformatted(label);
    ImGui::NextColumn();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2 { 0.0f, 0.0f });

    // 与 DragFloat 同一行时，按钮高度须与 framed 控件一致，否则色块会偏上/偏矮。
    const float frame_h = ImGui::GetFrameHeight();
    const ImVec2 button_size { frame_h, frame_h };
    const float col_avail = ImGui::GetContentRegionAvail().x;
    const float drag_w =
        std::max(24.0f, (col_avail - 3.0f * button_size.x) / 3.0f - 2.0f);

    bool changed = false;

    auto axis_buttons = [&](int axis, const ImVec4 &btn, const ImVec4 &hovered,
                            const ImVec4 &active, char axis_char) {
        ImGui::PushStyleColor(ImGuiCol_Button, btn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
        if (label_font) {
            ImGui::PushFont(label_font);
        }
        char lbl[2] { static_cast<char>(axis_char), '\0' };
        if (ImGui::Button(lbl, button_size)) {
            values[axis] = reset_value;
            changed = true;
        }
        if (label_font) {
            ImGui::PopFont();
        }
        ImGui::PopStyleColor(3);
    };

    axis_buttons(
        0, ImVec4 { 0.8f, 0.1f, 0.15f, 1.0f }, ImVec4 { 0.9f, 0.2f, 0.2f, 1.0f },
        ImVec4 { 0.8f, 0.1f, 0.15f, 1.0f }, 'X');
    ImGui::SameLine();
    ImGui::SetNextItemWidth(drag_w);
    if (v_min < v_max) {
        changed |= ImGui::DragFloat("##X", &values.x, drag_speed, v_min, v_max,
                                    fmt);
    } else {
        changed |= ImGui::DragFloat("##X", &values.x, drag_speed, 0.0f, 0.0f,
                                    fmt);
    }
    ImGui::SameLine();

    axis_buttons(
        1, ImVec4 { 0.2f, 0.7f, 0.2f, 1.0f }, ImVec4 { 0.3f, 0.8f, 0.3f, 1.0f },
        ImVec4 { 0.2f, 0.7f, 0.2f, 1.0f }, 'Y');
    ImGui::SameLine();
    ImGui::SetNextItemWidth(drag_w);
    if (v_min < v_max) {
        changed |= ImGui::DragFloat("##Y", &values.y, drag_speed, v_min, v_max,
                                    fmt);
    } else {
        changed |= ImGui::DragFloat("##Y", &values.y, drag_speed, 0.0f, 0.0f,
                                    fmt);
    }
    ImGui::SameLine();

    axis_buttons(
        2, ImVec4 { 0.1f, 0.25f, 0.8f, 1.0f },
        ImVec4 { 0.2f, 0.35f, 0.9f, 1.0f },
        ImVec4 { 0.1f, 0.25f, 0.8f, 1.0f }, 'Z');
    ImGui::SameLine();
    ImGui::SetNextItemWidth(drag_w);
    if (v_min < v_max) {
        changed |= ImGui::DragFloat("##Z", &values.z, drag_speed, v_min, v_max,
                                    fmt);
    } else {
        changed |= ImGui::DragFloat("##Z", &values.z, drag_speed, 0.0f, 0.0f,
                                    fmt);
    }

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return changed;
}

bool imgui_hazel_component_begin(const char *title, const void *stable_id) {
    constexpr ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap |
        ImGuiTreeNodeFlags_FramePadding;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2 { 4.0f, 4.0f });
    ImGui::Separator();
    const bool open =
        ImGui::TreeNodeEx(stable_id, flags, "%s", title);
    ImGui::PopStyleVar();
    return open;
}

} // namespace lumen::ui
