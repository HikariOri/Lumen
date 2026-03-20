/**
 * @file texture_view_panel.hpp
 * @brief 纹理预览面板：在 ImGui 窗口中显示离屏渲染纹理
 *
 * 用于 Scene、Wireframe、Normal、Depth 等视口，支持输出显示尺寸供调用方
 * 调整离屏分辨率（1:1 像素匹配，节省显存）。
 * 需在 ImGui 帧内调用（NewFrame 之后、Render 之前）。
 */

#pragma once

#include <imgui.h>

#include <cstdint>

namespace lumen {
namespace ui {

/**
 * @brief 纹理预览面板
 *
 * 绘制一个 ImGui 窗口，以 GetContentRegionAvail() 为尺寸显示纹理。
 * 若提供 outWidth/outHeight，则输出本帧显示尺寸，供下一帧调整离屏目标。
 *
 * @param title 窗口标题（如 "Scene", "Wireframe", "Normal", "Depth"）
 * @param textureId ImTextureID（来自 imgui_backend_add_texture）
 * @param outWidth 输出显示宽度，可为 nullptr
 * @param outHeight 输出显示高度，可为 nullptr
 * @param uv0 纹理左下 UV，默认 (0,0)
 * @param uv1 纹理右上 UV，默认 (1,1)，Vulkan 离屏通常不需 Y 翻转
 */
void imgui_texture_view_panel(const char *title, ImTextureID textureId,
                              uint32_t *outWidth = nullptr,
                              uint32_t *outHeight = nullptr,
                              const ImVec2 &uv0 = ImVec2(0, 0),
                              const ImVec2 &uv1 = ImVec2(1, 1));

} // namespace ui
} // namespace lumen
