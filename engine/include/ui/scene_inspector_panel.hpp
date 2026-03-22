/**
 * @file scene_inspector_panel.hpp
 * @brief 检视选中实体的 ObjectId、名称与 Transform（TRS）
 */

#pragma once

#include "ui/editor_selection.hpp"
#include "ui/panel.hpp"

namespace lumen {
namespace scene {
class Scene;
} // namespace scene

namespace ui {

class SceneInspectorPanel final : public IPanel {
public:
    SceneInspectorPanel(scene::Scene *scene, EditorSelection *selection);

    void on_imgui_render() override;

private:
    scene::Scene *scene_;
    EditorSelection *selection_;
};

} // namespace ui
} // namespace lumen
