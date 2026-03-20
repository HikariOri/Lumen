/**
 * @file texture_view_panel.cpp
 * @brief 纹理预览面板实现
 */

#include "ui/texture_view_panel.hpp"

#include <algorithm>

namespace lumen {
namespace ui {

void imgui_texture_view_panel(const char *title, ImTextureID textureId,
                              uint32_t *outWidth, uint32_t *outHeight,
                              const ImVec2 &uv0, const ImVec2 &uv1) {
    ImGui::Begin(title);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Image(textureId, avail, uv0, uv1);
    if (outWidth) {
        *outWidth = std::max(1u, static_cast<uint32_t>(avail.x));
    }
    if (outHeight) {
        *outHeight = std::max(1u, static_cast<uint32_t>(avail.y));
    }
    ImGui::End();
}

} // namespace ui
} // namespace lumen
