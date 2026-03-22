/**
 * @file gizmo.hpp
 * @brief ImGuizmo 封装：离屏视口内对 glm::mat4 做平移 / 旋转 / 缩放
 *
 * 须在 ImGui::Begin 与对应 Image 之后的同一窗口内调用（或由
 * imgui_texture_view_panel 的 after_image 回调调用）。每帧
 * `ImGuizmo::BeginFrame()` 由 imgui_backend_new_frame() 负责。
 */

#pragma once

#include <ImGuizmo.h>

#include <glm/mat4x4.hpp>

#include "ui/texture_view_panel.hpp"

namespace lumen {
namespace ui {

/**
 * @brief 在视口矩形内绘制并处理 Gizmo，就地修改 object_world
 * @param viewport_rect Scene Image 的屏幕矩形（与离屏相机一致）
 * @param view 与离屏渲染相同的视图矩阵（列主序）
 * @param proj 与离屏渲染相同的投影矩阵（列主序，含 Vulkan NDC 的 `proj[1][1] *= -1`）。
 *             内部会再抵消该 Y 翻转后传给 ImGuizmo（其按 OpenGL 风格 NDC 计算）。
 * @param object_world 物体世界矩阵，输入输出
 */
void imguizmo_manipulate(const TextureViewRect &viewport_rect,
                         const glm::mat4 &view, const glm::mat4 &proj,
                         glm::mat4 *object_world,
                         ImGuizmo::OPERATION operation = ImGuizmo::ROTATE,
                         ImGuizmo::MODE mode = ImGuizmo::LOCAL);

/// 上一帧 imguizmo_manipulate 调用后，用户是否正在拖拽 Gizmo
bool imguizmo_is_using();

/// 上一帧 imguizmo_manipulate 调用后，鼠标是否悬停在 Gizmo 上
bool imguizmo_is_over();

} // namespace ui
} // namespace lumen
