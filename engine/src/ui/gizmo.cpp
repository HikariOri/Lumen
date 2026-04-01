/**
 * @file gizmo.cpp
 * @brief ImGuizmo 封装实现
 */

#include "ui/gizmo.hpp"

#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace lumen {
namespace ui {
namespace {

bool g_last_using { false };
bool g_last_over { false };

/// 缩放过小会导致矩阵病态，ImGuizmo 缩放手柄无法拾取；钳制各轴绝对值下限。
void clamp_mat4_scale(glm::mat4 &m) {
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::vec3 translation;
    glm::quat orientation;
    glm::vec3 scale;
    glm::decompose(m, scale, orientation, translation, skew, perspective);
    constexpr float kMinAbs { 1e-2f };
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(scale[i])) {
            scale[i] = kMinAbs;
            continue;
        }
        const float a = std::fabs(scale[i]);
        const float c = std::max(a, kMinAbs);
        scale[i] = (scale[i] < 0.0f) ? -c : c;
    }
    m = glm::translate(glm::mat4(1.0f), translation) *
        glm::mat4_cast(orientation) * glm::scale(glm::mat4(1.0f), scale);
}

} // namespace

void imguizmo_manipulate(const TextureViewRect &viewport_rect,
                         const glm::mat4 &view, const glm::mat4 &proj,
                         glm::mat4 *object_world, ImGuizmo::OPERATION operation,
                         ImGuizmo::MODE mode, ImDrawList *draw_list,
                         ImGuiWindow *imgui_host_window) {
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
    ImDrawList *dl = draw_list;
    if (dl == nullptr && imgui_host_window != nullptr) {
        dl = imgui_host_window->DrawList;
    }
    if (dl == nullptr) {
        dl = ImGui::GetWindowDrawList();
    }
    ImGuizmo::SetDrawlist(dl);
    ImGuizmo::SetRect(viewport_rect.minX, viewport_rect.minY, w, h);
    // ImGuizmo 的轴线会沿屏幕延伸，不裁剪会画到 Scene 图像外侧；与 Image 矩形对齐
    const ImVec2 clip_min(viewport_rect.minX, viewport_rect.minY);
    const ImVec2 clip_max(viewport_rect.maxX, viewport_rect.maxY);
    dl->PushClipRect(clip_min, clip_max, true);
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj_imguizmo),
                         operation, mode, glm::value_ptr(*object_world));
    dl->PopClipRect();
    // 不限于 SCALE 模式：矩阵已缩放过小时也须拉回，否则缩放手柄无法再次拾取。
    clamp_mat4_scale(*object_world);
    g_last_using = ImGuizmo::IsUsing();
    g_last_over = ImGuizmo::IsOver();
}

bool imguizmo_is_using() { return g_last_using; }

bool imguizmo_is_over() { return g_last_over; }

void imguizmo_reset_interaction_state() {
    g_last_using = false;
    g_last_over = false;
}

void imguizmo_view_manipulate(glm::mat4 *view, float length, float region_x,
                              float region_y, float region_w, float region_h,
                              std::uint32_t background_rgba) {
    if (!view) {
        return;
    }
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::ViewManipulate(glm::value_ptr(*view), length,
                             ImVec2(region_x, region_y),
                             ImVec2(region_w, region_h),
                             static_cast<ImU32>(background_rgba));
    ImGuizmo::SetDrawlist(nullptr);
}

} // namespace ui
} // namespace lumen
