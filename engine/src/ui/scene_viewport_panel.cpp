/**
 * @file scene_viewport_panel.cpp
 */

#include "ui/scene_viewport_panel.hpp"

#include <algorithm>

namespace lumen {
namespace ui {

void imgui_scene_viewport_panel(
    const char *title, const ImTextureID textureID, uint32_t *pending_width,
    uint32_t *pending_height, bool *out_viewport_hovered,
    const std::function<void(float wheel_delta)> &on_wheel_if_hovered,
    const std::function<void(const TextureViewRect &)> &after_image) {
    if (out_viewport_hovered != nullptr) {
        *out_viewport_hovered = false;
    }

    if (textureID != static_cast<ImTextureID>(0)) {
        imgui_texture_view_panel(
            title, textureID, pending_width, pending_height, nullptr,
            ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F),
            [&](const TextureViewRect &rect) {
                const bool hovered = ImGui::IsItemHovered();
                if (out_viewport_hovered != nullptr) {
                    *out_viewport_hovered = hovered;
                }
                if (hovered && on_wheel_if_hovered) {
                    const float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0F) {
                        on_wheel_if_hovered(wheel);
                    }
                }
                if (after_image) {
                    after_image(rect);
                }
            });
        if (pending_width != nullptr) {
            *pending_width = std::max(2U, *pending_width);
        }
        if (pending_height != nullptr) {
            *pending_height = std::max(2U, *pending_height);
        }
    } else {
        ImGui::Begin(title);
        const ImVec2 vp_avail = ImGui::GetContentRegionAvail();
        if (pending_width != nullptr) {
            *pending_width =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, vp_avail.x)));
        }
        if (pending_height != nullptr) {
            *pending_height =
                std::max(2U, static_cast<uint32_t>(std::max(1.0F, vp_avail.y)));
        }
        ImGui::End();
    }
}

} // namespace ui
} // namespace lumen
