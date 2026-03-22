/**
 * @file scene_inspector_panel.cpp
 */

#include "ui/scene_inspector_panel.hpp"

#include "scene/components.hpp"
#include "scene/scene.hpp"

#include <cinttypes>
#include <cstdio>

#include <ImGuizmo.h>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace lumen::ui {

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

    if (const auto *oid = reg.try_get<lumen::scene::ObjectId>(e)) {
        ImGui::BeginDisabled();
        char idBuf[32];
        std::snprintf(idBuf, sizeof(idBuf), "%" PRIu32, oid->id);
        ImGui::InputText("Object ID", idBuf, sizeof(idBuf),
                         ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "创建时分配的对象 ID（uint32），与 EnTT 句柄无关；用于 Pick / 序列化。");
    }

    if (auto *name = reg.try_get<lumen::scene::NameComponent>(e)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", name->name.c_str());
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            name->name = buf;
        }
    }

    if (reg.all_of<lumen::scene::DrawableTag>(e)) {
        ImGui::TextUnformatted("Drawable: yes");
    } else {
        if (ImGui::Button("Mark drawable")) {
            reg.emplace<lumen::scene::DrawableTag>(e);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(mesh uses first drawable)");
    }

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

    if (auto *light = reg.try_get<lumen::scene::LightComponent>(e)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Light");
        int type_i = static_cast<int>(light->type);
        if (ImGui::Combo("Type", &type_i,
                          "Directional\0Point\0Spot\0\0")) {
            light->type = static_cast<lumen::scene::LightType>(type_i);
        }
        ImGui::ColorEdit3("Color", glm::value_ptr(light->color));
        ImGui::DragFloat("Intensity", &light->intensity, 0.02f, 0.0f, 64.0f,
                         "%.2f");
        if (light->type == lumen::scene::LightType::Directional ||
            light->type == lumen::scene::LightType::Spot) {
            ImGui::DragFloat3("Local direction", glm::value_ptr(light->local_direction),
                              0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::TextDisabled(
                "Directional: surface → light. Spot: cone axis (local).");
        }
        if (light->type == lumen::scene::LightType::Point ||
            light->type == lumen::scene::LightType::Spot) {
            ImGui::DragFloat("Range", &light->range, 0.05f, 0.01f, 256.0f,
                             "%.2f");
        }
        if (light->type == lumen::scene::LightType::Spot) {
            float inner_deg = light->inner_radians * 57.2957795f;
            float outer_deg = light->outer_radians * 57.2957795f;
            if (ImGui::DragFloat("Inner cone (deg)", &inner_deg, 0.25f, 0.1f,
                                 89.0f, "%.1f")) {
                light->inner_radians = inner_deg * 0.0174532925f;
            }
            if (ImGui::DragFloat("Outer cone (deg)", &outer_deg, 0.25f, 0.2f,
                                 90.0f, "%.1f")) {
                light->outer_radians = outer_deg * 0.0174532925f;
            }
        }
        if (ImGui::Button("Remove light")) {
            reg.remove<lumen::scene::LightComponent>(e);
        }
    } else {
        if (ImGui::Button("Add light")) {
            reg.emplace<lumen::scene::LightComponent>(e);
        }
    }

    if (auto *tr = reg.try_get<lumen::scene::TransformComponent>(e)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Transform (local)");
        float pos[3];
        float rotDeg[3];
        float sc[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(tr->matrix), pos,
                                              rotDeg, sc);
        bool edited = false;
        edited |= ImGui::DragFloat3("Position", pos, 0.01f);
        edited |= ImGui::DragFloat3("Rotation (deg)", rotDeg, 0.5f);
        edited |= ImGui::DragFloat3("Scale", sc, 0.01f, 1e-2f, 1e3f);
        if (edited) {
            ImGuizmo::RecomposeMatrixFromComponents(pos, rotDeg, sc,
                                                    glm::value_ptr(tr->matrix));
        }
    }

    ImGui::End();
}

} // namespace lumen::ui
