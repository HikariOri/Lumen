/**
 * @file scene_hierarchy_panel.cpp
 */

#include "ui/scene_hierarchy_panel.hpp"

#include "scene/components.hpp"
#include "scene/scene.hpp"
#include "ui/editor_selection.hpp"

#include <entt/entt.hpp>
#include <imgui.h>

namespace lumen::ui {
namespace {

[[nodiscard]] bool is_root(const ::entt::registry &reg, ::entt::entity e) {
    const auto *p = reg.try_get<lumen::scene::ParentComponent>(e);
    if (!p) {
        return true;
    }
    if (p->parent == ::entt::null || !reg.valid(p->parent)) {
        return true;
    }
    return false;
}

void draw_entity_node(lumen::scene::Scene *scene, lumen::ui::EditorSelection *sel,
                      std::vector<::entt::entity> *pending_destroy,
                      ::entt::entity e) {
    if (!scene || !sel || !scene->registry().valid(e)) {
        return;
    }
    ::entt::registry &reg = scene->registry();
    auto &name = reg.get<lumen::scene::NameComponent>(e).name;
    const std::vector<::entt::entity> children = scene->children_of(e);
    const bool has_children = !children.empty();

    ImGui::PushID(static_cast<int>(::entt::to_integral(e)));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (sel->entity == e) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool open = false;
    if (has_children) {
        open = ImGui::TreeNodeEx(name.c_str(), flags);
    } else {
        ImGui::TreeNodeEx(name.c_str(),
                          flags | ImGuiTreeNodeFlags_Leaf |
                              ImGuiTreeNodeFlags_NoTreePushOnOpen);
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        sel->entity = e;
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete entity") && pending_destroy) {
            if (sel->entity == e) {
                sel->entity = ::entt::null;
            }
            pending_destroy->push_back(e);
        }
        ImGui::EndPopup();
    }

    if (has_children && open) {
        for (auto c : children) {
            draw_entity_node(scene, sel, pending_destroy, c);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // namespace

SceneHierarchyPanel::SceneHierarchyPanel(scene::Scene *scene,
                                         EditorSelection *selection)
    : scene_(scene), selection_(selection) {}

void SceneHierarchyPanel::on_imgui_render() {
    if (!scene_ || !selection_) {
        return;
    }

    ImGui::Begin("Scene Hierarchy");
    ::entt::registry &reg = scene_->registry();

    if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        const float btn_w =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
            0.5f;
        if (ImGui::Button("Create empty", ImVec2(btn_w, 0))) {
            ::entt::entity parent { ::entt::null };
            if (reg.valid(selection_->entity)) {
                parent = selection_->entity;
            }
            const ::entt::entity n = scene_->create_entity("GameObject");
            if (parent != ::entt::null && reg.valid(parent)) {
                scene_->set_parent(n, parent);
            }
            selection_->entity = n;
        }
        ImGui::SameLine();
        if (ImGui::Button("Create root", ImVec2(btn_w, 0))) {
            selection_->entity = scene_->create_entity("GameObject");
        }
    }

    if (ImGui::CollapsingHeader("Entities", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("hierarchy_tree", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar);

        pending_destroy_.clear();
        for (const ::entt::entity ent :
             reg.view<lumen::scene::NameComponent>()) {
            if (is_root(reg, ent)) {
                draw_entity_node(scene_, selection_, &pending_destroy_, ent);
            }
        }

        ImGui::EndChild();
    }

    for (const ::entt::entity d : pending_destroy_) {
        scene_->destroy_entity(d);
    }
    pending_destroy_.clear();

    ImGui::End();
}

} // namespace lumen::ui
