/**
 * @file scene_hierarchy_panel.hpp
 * @brief 场景层级树（EnTT + Parent）；创建空实体、删除、选中
 */

#pragma once

#include <vector>

#include <entt/entt.hpp>

#include "ui/editor_selection.hpp"
#include "ui/panel.hpp"

namespace lumen {
namespace scene {
class Scene;
} // namespace scene

namespace ui {

class SceneHierarchyPanel final : public IPanel {
public:
    SceneHierarchyPanel(scene::Scene *scene, EditorSelection *selection);

    void on_imgui_render() override;

private:
    scene::Scene *scene_;
    EditorSelection *selection_;
    std::vector<::entt::entity> pending_destroy_;
};

} // namespace ui
} // namespace lumen
