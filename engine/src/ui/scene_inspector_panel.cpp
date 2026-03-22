/**
 * @file scene_inspector_panel.cpp
 */

#include "ui/scene_inspector_panel.hpp"

#include "scene/components.hpp"
#include "scene/scene.hpp"

#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cstdio>

#include <ImGuizmo.h>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace lumen::ui {
namespace {

/// 与 Unity / UE 视口一致的轴向色：X 红、Y 绿、Z 蓝
constexpr ImVec4 kAxisX { 0.95f, 0.32f, 0.32f, 1.0f };
constexpr ImVec4 kAxisY { 0.40f, 0.82f, 0.42f, 1.0f };
constexpr ImVec4 kAxisZ { 0.38f, 0.62f, 0.98f, 1.0f };

/// 与 `drag_float3_color_row` 搭配的标签列宽（最长文案 + 内边距），保证多行右侧控件左缘对齐。
float label_column_width_for_vec3_rows() {
    const ImGuiStyle &st = ImGui::GetStyle();
    float w = 0.0f;
    for (const char *s :
         {"Position", "Rotation (deg)", "Scale", "Direction (local)"}) {
        w = std::max(w, ImGui::CalcTextSize(s).x);
    }
    return w + st.ItemInnerSpacing.x;
}

/// 单行：■红 ■绿 ■蓝 各带一个 DragFloat（无 X/Y/Z 文字）
[[nodiscard]] bool drag_float3_color_row(const char *group_id, float *v3,
                                           float speed, float min_v, float max_v,
                                           const char *fmt) {
    ImGui::PushID(group_id);
    const float block_sz = ImGui::GetFrameHeight();
    const float avail = ImGui::GetContentRegionAvail().x;
    constexpr float gap_block_to_drag { 4.0f };
    constexpr float gap_between_axes { 6.0f };
    const float drag_total =
        avail - 3.0f * block_sz - 3.0f * gap_block_to_drag -
        2.0f * gap_between_axes;
    float drag_w = drag_total / 3.0f;
    if (drag_w < 24.0f) {
        drag_w = 24.0f;
    }

    const ImVec4 colors[] = { kAxisX, kAxisY, kAxisZ };
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine(0, gap_between_axes);
        }
        ImGui::PushID(i);
        ImGui::InvisibleButton("blk", ImVec2(block_sz, block_sz));
        const ImVec2 r0 = ImGui::GetItemRectMin();
        const ImVec2 r1 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            r0, r1, ImGui::ColorConvertFloat4ToU32(colors[i]), 2.0f);
        ImGui::GetWindowDrawList()->AddRect(r0, r1, IM_COL32(0, 0, 0, 72), 2.0f);
        ImGui::SameLine(0, gap_block_to_drag);
        ImGui::SetNextItemWidth(drag_w);
        if (min_v < max_v) {
            changed |= ImGui::DragFloat("##v", &v3[i], speed, min_v, max_v, fmt);
        } else {
            changed |= ImGui::DragFloat("##v", &v3[i], speed, 0.0f, 0.0f, fmt);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

SceneInspectorPanel::SceneInspectorPanel(scene::Scene *scene,
                                         EditorSelection *selection)
    : scene_(scene), selection_(selection) {}

void SceneInspectorPanel::on_imgui_render() {
    if (!scene_ || !selection_) {
        return;
    }

    ImGui::Begin("Inspector");
    ::entt::registry &reg = scene_->registry();
    const ::entt::entity e = selection_->entity;

    if (!reg.valid(e)) {
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto *name = reg.try_get<lumen::scene::NameComponent>(e)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", name->name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) {
                name->name = buf;
            }
        }
        if (const auto *oid = reg.try_get<lumen::scene::ObjectId>(e)) {
            ImGui::BeginDisabled();
            char idBuf[32];
            std::snprintf(idBuf, sizeof(idBuf), "%" PRIu32, oid->id);
            ImGui::InputText("Object ID", idBuf, sizeof(idBuf),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "Pick / 序列化用 uint32，与 EnTT 句柄无关。");
        }
    }

    if (reg.all_of<lumen::scene::TransformComponent>(e)) {
        if (ImGui::CollapsingHeader("Transform",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            auto &tr = reg.get<lumen::scene::TransformComponent>(e);
            float pos[3];
            float rotDeg[3];
            float sc[3];
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(tr.matrix), pos,
                                                  rotDeg, sc);
            ImGui::TextDisabled("Local space");
            bool edited = false;
            const float label_col_w = label_column_width_for_vec3_rows();
            if (ImGui::BeginTable("##transform_vec3", 2,
                                  ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("lbl",
                                        ImGuiTableColumnFlags_WidthFixed,
                                        label_col_w);
                ImGui::TableSetupColumn("row", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Position");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                edited |= drag_float3_color_row("pos", pos, 0.01f, 0.0f, 0.0f,
                                                "%.3f");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Rotation (deg)");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                edited |= drag_float3_color_row("rot", rotDeg, 0.5f, 0.0f, 0.0f,
                                                "%.1f");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Scale");
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                edited |= drag_float3_color_row("scl", sc, 0.01f, 1e-2f, 1e3f,
                                                "%.3f");

                ImGui::EndTable();
            }
            if (edited) {
                ImGuizmo::RecomposeMatrixFromComponents(pos, rotDeg, sc,
                                                        glm::value_ptr(tr.matrix));
            }
        }
    }

    if (ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto *par = reg.try_get<lumen::scene::ParentComponent>(e)) {
            if (par->parent != ::entt::null && reg.valid(par->parent)) {
                const auto *pn =
                    reg.try_get<lumen::scene::NameComponent>(par->parent);
                ImGui::Text("Parent: %s (%u)", pn ? pn->name.c_str() : "?",
                            static_cast<unsigned>(::entt::to_integral(par->parent)));
                if (ImGui::Button("Clear parent")) {
                    scene_->set_parent(e, ::entt::null);
                }
            } else {
                ImGui::TextUnformatted("Parent: (none)");
            }
        } else {
            ImGui::TextUnformatted("Parent: (none)");
        }
    }

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (reg.all_of<lumen::scene::DrawableTag>(e)) {
            ImGui::TextUnformatted("Drawable: enabled (mesh uses first drawable)");
            if (ImGui::Button("Remove Drawable")) {
                reg.remove<lumen::scene::DrawableTag>(e);
            }
        } else {
            ImGui::TextDisabled("No Drawable tag on this entity.");
            if (ImGui::Button("Add Drawable")) {
                reg.emplace<lumen::scene::DrawableTag>(e);
            }
        }
    }

    if (reg.all_of<lumen::scene::LightComponent>(e)) {
        if (ImGui::CollapsingHeader(
                "Light", ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_SpanAvailWidth)) {
            auto &light = reg.get<lumen::scene::LightComponent>(e);
            int type_i = static_cast<int>(light.type);
            if (ImGui::Combo("Type", &type_i,
                             "Directional\0Point\0Spot\0\0")) {
                light.type = static_cast<lumen::scene::LightType>(type_i);
            }
            ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.02f, 0.0f, 64.0f,
                             "%.2f");
            if (light.type == lumen::scene::LightType::Directional ||
                light.type == lumen::scene::LightType::Spot) {
                const float label_col_w = label_column_width_for_vec3_rows();
                if (ImGui::BeginTable("##light_dir_vec3", 2,
                                      ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn(
                        "lbl", ImGuiTableColumnFlags_WidthFixed, label_col_w);
                    ImGui::TableSetupColumn("row",
                                            ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Direction (local)");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    (void)drag_float3_color_row(
                        "ldir", glm::value_ptr(light.local_direction), 0.01f,
                        0.0f, 0.0f, "%.3f");
                    ImGui::EndTable();
                }
            }
            if (light.type == lumen::scene::LightType::Point ||
                light.type == lumen::scene::LightType::Spot) {
                ImGui::DragFloat("Range", &light.range, 0.05f, 0.01f, 256.0f,
                                 "%.2f");
            }
            if (light.type == lumen::scene::LightType::Spot) {
                float inner_deg = light.inner_radians * 57.2957795f;
                float outer_deg = light.outer_radians * 57.2957795f;
                if (ImGui::DragFloat("Inner cone (deg)", &inner_deg, 0.25f, 0.1f,
                                     89.0f, "%.1f")) {
                    light.inner_radians = inner_deg * 0.0174532925f;
                }
                if (ImGui::DragFloat("Outer cone (deg)", &outer_deg, 0.25f, 0.2f,
                                     90.0f, "%.1f")) {
                    light.outer_radians = outer_deg * 0.0174532925f;
                }
            }
            if (ImGui::Button("Remove Light")) {
                reg.remove<lumen::scene::LightComponent>(e);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!reg.all_of<lumen::scene::LightComponent>(e)) {
            if (ImGui::MenuItem("Light")) {
                reg.emplace<lumen::scene::LightComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::DrawableTag>(e)) {
            if (ImGui::MenuItem("Drawable")) {
                reg.emplace<lumen::scene::DrawableTag>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::TransformComponent>(e)) {
            if (ImGui::MenuItem("Transform")) {
                reg.emplace<lumen::scene::TransformComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace lumen::ui
