/**
 * @file texture_view_panel.cpp
 * @brief 纹理预览面板实现
 */

#include "ui/texture_view_panel.hpp"

#include <algorithm>
#include <cmath>

namespace lumen {
namespace ui {
namespace {

/// ImGui::GetContentRegionAvail() 在窗口过窄、Dock 首帧等情况下可能为负或非有限值；
/// 直接 static_cast<uint32_t> 会无符号回绕，进而把离屏 resize 撑爆。
uint32_t content_avail_to_pixel_dim(float v) {
    if (!std::isfinite(v) || v < 1.0f) {
        return 1u;
    }
    constexpr float kMaxReasonable = 16384.0f;
    const float c = std::min(v, kMaxReasonable);
    const auto as_u32 = static_cast<uint32_t>(c);
    return std::max(1u, as_u32);
}

} // namespace

ViewportMouseState viewport_mouse_state(const TextureViewRect &rect,
                                        float mouseX, float mouseY) {
    ViewportMouseState s;
    const float w = rect.width();
    const float h = rect.height();
    s.inViewport = mouseX >= rect.minX && mouseX <= rect.maxX &&
                   mouseY >= rect.minY && mouseY <= rect.maxY && w > 0 &&
                   h > 0;
    if (s.inViewport) {
        s.localX = mouseX - rect.minX;
        s.localY = mouseY - rect.minY;
        s.normX = s.localX / w;
        s.normY = s.localY / h;
    }
    return s;
}

void imgui_viewport_mouse_debug(const TextureViewRect &rect,
                                const ViewportMouseState &mouseState,
                                const char *label) {
    ImGui::Separator();
    ImGui::Text("%s Rect (screen)", label);
    ImGui::Text("min: (%.0f, %.0f)  max: (%.0f, %.0f)", rect.minX, rect.minY,
                rect.maxX, rect.maxY);
    ImGui::Text("size: %.0f x %.0f", rect.width(), rect.height());
    if (mouseState.inViewport) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                          "Mouse in %s: local=(%.0f, %.0f) norm=(%.3f, %.3f)",
                          label, mouseState.localX, mouseState.localY,
                          mouseState.normX, mouseState.normY);
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Mouse: [not in %s]",
                          label);
    }
}

void imgui_texture_view_panel(const char *title, ImTextureID textureId,
                              uint32_t *outWidth, uint32_t *outHeight,
                              TextureViewRect *outRect, const ImVec2 &uv0,
                              const ImVec2 &uv1) {
    ImGui::Begin(title);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const uint32_t w_px = content_avail_to_pixel_dim(avail.x);
    const uint32_t h_px = content_avail_to_pixel_dim(avail.y);
    ImGui::Image(textureId,
                  ImVec2(static_cast<float>(w_px), static_cast<float>(h_px)),
                  uv0, uv1);
    if (outWidth) {
        *outWidth = w_px;
    }
    if (outHeight) {
        *outHeight = h_px;
    }
    if (outRect) {
        const ImVec2 minP = ImGui::GetItemRectMin();
        const ImVec2 maxP = ImGui::GetItemRectMax();
        outRect->minX = minP.x;
        outRect->minY = minP.y;
        outRect->maxX = maxP.x;
        outRect->maxY = maxP.y;
    }
    ImGui::End();
}

} // namespace ui
} // namespace lumen
