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
    /// 环境面板「应用目录」或「程序化」后由 main 重载立方体并清回 false
    bool environment_cubemap_reload_requested { false };
};

} // namespace ui
} // namespace lumen
