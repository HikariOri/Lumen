/**
 * @file imgui_hazel_helpers.hpp
 * @brief 与 TheCherno Hazel 编辑器面板一致的 ImGui 控件（SceneHierarchyPanel 风格）
 */

#pragma once

#include <glm/vec3.hpp>

namespace lumen::ui {

/**
 * Hazel 式 vec3：左列标签、右列 X/Y/Z 彩色重置键 + DragFloat。
 * @param v_min,v_max 若 `v_min >= v_max` 则拖拽无上下限（与 Hazel 一致）；否则传入 DragFloat 范围。
 * @return 任一控件是否产生修改
 */
bool imgui_hazel_draw_vec3(const char *label, glm::vec3 &values,
                           float reset_value = 0.0f, float column_width = 100.0f,
                           float drag_speed = 0.1f, const char *fmt = "%.2f",
                           float v_min = 0.0f, float v_max = 0.0f);

/**
 * Hazel `DrawComponent` 式标题：Separator + 带边框 TreeNode（默认展开）。
 * 若返回 true，须在内容结束后调用 `ImGui::TreePop()`。
 */
bool imgui_hazel_component_begin(const char *title, const void *stable_id);

} // namespace lumen::ui
