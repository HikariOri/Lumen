/**
 * @file editor_selection.hpp
 * @brief 编辑器当前选中实体（Hierarchy / Inspector / Gizmo 共享）
 */

#pragma once

#include <entt/entt.hpp>

namespace lumen {
namespace ui {

struct EditorSelection {
    ::entt::entity entity { ::entt::null };
};

} // namespace ui
} // namespace lumen
