/**
 * @file gizmo.cpp
 * @brief ImGuizmo 封装实现
 */

#include "ui/gizmo.hpp"

#include <cmath>

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>

namespace lumen {
namespace ui {
namespace {

bool g_last_using { false };
bool g_last_over { false };

} // namespace

void imguizmo_manipulate(const TextureViewRect &viewport_rect,
                         const glm::mat4 &view, const glm::mat4 &proj,
                         glm::mat4 *object_world, ImGuizmo::OPERATION operation,
                         ImGuizmo::MODE mode) {
    if (!object_world) {
        g_last_using = false;
        g_last_over = false;
        return;
    }
    const float w = viewport_rect.width();
    const float h = viewport_rect.height();
    if (w <= 0.0f || h <= 0.0f || !std::isfinite(w) || !std::isfinite(h)) {
        g_last_using = false;
        g_last_over = false;
        return;
    }

    // ImGuizmo 按 OpenGL 风格 NDC（clip Y 向上为正）做拾取与轴向；Vulkan 渲染侧常用的
    // proj[1][1] *= -1 需在此抵消，否则轴向与拖拽方向易与场景不一致（见 ImGuizmo#154）。
    glm::mat4 proj_imguizmo = proj;
    proj_imguizmo[1][1] *= -1.0f;

    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewport_rect.minX, viewport_rect.minY, w, h);
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj_imguizmo),
                         operation, mode, glm::value_ptr(*object_world));
    g_last_using = ImGuizmo::IsUsing();
    g_last_over = ImGuizmo::IsOver();
}

bool imguizmo_is_using() { return g_last_using; }

bool imguizmo_is_over() { return g_last_over; }

} // namespace ui
} // namespace lumen
