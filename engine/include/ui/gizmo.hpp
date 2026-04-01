/**
 * @file gizmo.hpp
 * @brief ImGuizmo 封装：离屏视口内对 glm::mat4 做平移 / 旋转 / 缩放
 *
 * 可在轨道相机等逻辑更新后再调用，使 view/proj 与即将渲染的离屏场景一致（仍须与
 * @a viewport_rect 为同一帧内记录）。若当前不在目标窗口的 `Begin`/`End` 之间，应传入
 * @a imgui_host_window，以便使用其 `DrawList`（与 vcpkg 旧版 ImGuizmo 无
 * `SetAlternativeWindow` 时仍能正确拾取）。
 * 每帧 `ImGuizmo::BeginFrame()` 由 imgui_backend_new_frame() 负责。
 */

#pragma once

#include <ImGuizmo.h>

#include <cstdint>

#include <glm/mat4x4.hpp>

#include "ui/texture_view_panel.hpp"

struct ImDrawList;
struct ImGuiWindow;

namespace lumen {
namespace ui {

/**
 * @brief 在视口矩形内绘制并处理 Gizmo，就地修改 object_world
 * @param viewport_rect Scene Image 的屏幕矩形（与离屏相机一致；绘制会裁剪到此矩形内）
 * @param view 与离屏渲染相同的视图矩阵（列主序）
 * @param proj 与离屏渲染相同的投影矩阵（列主序，含 Vulkan NDC 的 `proj[1][1] *=
 * -1`）。 内部会再抵消该 Y 翻转后传给 ImGuizmo（其按 OpenGL 风格 NDC 计算）。
 * @param object_world
 * 物体世界矩阵，输入输出。每次调用后会对各轴缩放绝对值做下限钳制 （默认
 * 1e-2），避免缩放过小导致矩阵奇异、无法再放大。
 * @param draw_list 非空时强制使用该 DrawList（须与宿主窗口一致，否则旧版 ImGuizmo
 * 拾取会失效）；通常传 `nullptr`。
 * @param imgui_host_window 非空且 @a draw_list 为空时，使用
 * `imgui_host_window->DrawList`（如 Scene 视口 `after_image` 内
 * `ImGui::GetCurrentWindowRead()`），以便在窗口 `End` 之后仍可绘制且悬停判定正确。
 */
void imguizmo_manipulate(const TextureViewRect &viewport_rect,
                         const glm::mat4 &view, const glm::mat4 &proj,
                         glm::mat4 *object_world,
                         ImGuizmo::OPERATION operation = ImGuizmo::ROTATE,
                         ImGuizmo::MODE mode = ImGuizmo::LOCAL,
                         ImDrawList *draw_list = nullptr,
                         ImGuiWindow *imgui_host_window = nullptr);

/// 上一帧 imguizmo_manipulate 调用后，用户是否正在拖拽 Gizmo
bool imguizmo_is_using();

/// 上一帧 imguizmo_manipulate 调用后，鼠标是否悬停在 Gizmo 上
bool imguizmo_is_over();

/// 本帧不调用 imguizmo_manipulate 时（如 Unity 式 Q 视图工具），须调用以清除
/// IsUsing/IsOver 缓存，避免误挡相机/模型输入路由。
void imguizmo_reset_interaction_state();

/**
 * @brief 方向立方体 (ImGuizmo::ViewManipulate)，用于快速对齐轴向视角
 *
 * 在屏幕固定矩形内绘制；使用前景 DrawList，宜在主视口 Dock 之后调用。
 * 会就地修改 @a view；应用侧宜随后根据 view
 * 同步轨道相机参数（yaw/pitch/radius）。
 *
 * @param length 相机到观察目标距离（与 lookAt 的 orbit 半径一致）
 * @param background_rgba 背景色，建议用 `IM_COL32(r,g,b,a)` 传入
 *
 * 调用后请用 `lumen::scene::SceneOrbitController::sync_from_view` 将视图矩阵
 * 同步回轨道参数（见 `docs/design/scene-camera.md`）。
 */
void imguizmo_view_manipulate(glm::mat4 *view, float length, float region_x,
                              float region_y, float region_w, float region_h,
                              std::uint32_t background_rgba = 0xCC101010u);

} // namespace ui
} // namespace lumen
