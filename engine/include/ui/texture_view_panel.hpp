/**
 * @file texture_view_panel.hpp
 * @brief 纹理预览面板：在 ImGui 窗口中显示离屏渲染纹理
 *
 * 用于 Scene、Wireframe、Normal、Depth 等视口，支持输出显示尺寸供调用方
 * 调整离屏分辨率（1:1 像素匹配，节省显存），以及 Image
 * 的屏幕坐标（用于射线拾取等）。 需在 ImGui 帧内调用（NewFrame 之后、Render
 * 之前）。
 */

#pragma once

#include <imgui.h>

#include <cstdint>
#include <functional>

namespace lumen {
namespace ui {

/// Image 在屏幕上的矩形区域（可用于射线拾取、坐标转换等）
struct TextureViewRect {
    float minX { 0 };
    float minY { 0 };
    float maxX { 0 };
    float maxY { 0 };
    float width() const { return maxX - minX; }
    float height() const { return maxY - minY; }
};

/// 鼠标在视口内的状态（由 viewport_mouse_state 计算）
struct ViewportMouseState {
    bool inViewport { false };
    float localX { 0 };
    float localY { 0 };
    float normX { 0 };
    float normY { 0 };
};

/**
 * @brief 计算鼠标在视口内的局部坐标与归一化坐标
 *
 * @param rect 视口屏幕矩形（来自 texture_view_panel 的 outRect）
 * @param mouseX 鼠标屏幕 X（来自 Input::mouse_x）
 * @param mouseY 鼠标屏幕 Y（来自 Input::mouse_y）
 * @return ViewportMouseState，含 inViewport、local、norm
 */
ViewportMouseState viewport_mouse_state(const TextureViewRect &rect,
                                        float mouseX, float mouseY);

/**
 * @brief 在当前 ImGui 窗口内绘制视口矩形与鼠标坐标调试信息
 *
 * 需在 ImGui::Begin 之后调用。
 *
 * @param rect 视口屏幕矩形
 * @param mouseState 鼠标状态（由 viewport_mouse_state 计算）
 * @param label 可选前缀标签，默认 "Viewport"
 */
void imgui_viewport_mouse_debug(const TextureViewRect &rect,
                                const ViewportMouseState &mouseState,
                                const char *label = "Viewport");

/**
 * @brief 纹理预览面板
 *
 * 绘制一个 ImGui 窗口，以 GetContentRegionAvail() 为基准显示纹理。
 * 若提供 outWidth/outHeight，则输出本帧显示尺寸，供下一帧调整离屏目标。
 * 引擎内部会将非有限、非正或过大的可用区域规范为合法像素尺寸（至少为 1，
 * 并设合理上界），调用方无需再钳位即可传给 OffscreenRenderTarget::resize。
 * 若提供 outRect，则输出 Image 的屏幕坐标矩形（左上 min、右下 max）。
 *
 * @param title 窗口标题（如 "Scene", "Wireframe", "Normal", "Depth"）
 * @param textureId ImTextureID（来自 imgui_backend_add_texture）
 * @param outWidth 输出显示宽度，可为 nullptr
 * @param outHeight 输出显示高度，可为 nullptr
 * @param outRect 输出 Image 屏幕坐标矩形，可为 nullptr
 * @param uv0 纹理左下 UV，默认 (0,0)
 * @param uv1 纹理右上 UV，默认 (1,1)，Vulkan 离屏通常不需 Y 翻转
 * @param after_image 可选；在 Image 之后、End 之前调用（如绘制 Gizmo），参数为
 *                    与 outRect 一致的屏幕矩形
 */
void imgui_texture_view_panel(
    const char *title, ImTextureID textureId, uint32_t *outWidth = nullptr,
    uint32_t *outHeight = nullptr, TextureViewRect *outRect = nullptr,
    const ImVec2 &uv0 = ImVec2(0, 0), const ImVec2 &uv1 = ImVec2(1, 1),
    const std::function<void(const TextureViewRect &)> &after_image = {});

} // namespace ui
} // namespace lumen
