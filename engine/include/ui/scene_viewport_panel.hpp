/**
 * @file scene_viewport_panel.hpp
 * @brief 离屏场景纹理视口：封装 texture_view_panel、无效纹理占位与 pending
 * 尺寸钳位
 *
 * 渲染与相机逻辑由调用方通过回调注入，避免 UI 依赖 scene 模块。
 */

#pragma once

#include "ui/texture_view_panel.hpp"

#include <imgui.h>

#include <cstdint>
#include <functional>

namespace lumen {
namespace ui {

/**
 * @brief 场景视口窗口（标题与 imgui_texture_view_panel 一致地使用 @a title）
 *
 * @param texture_id 为 0 时仅更新 pending 尺寸（resize 过渡期），不绘制 Image
 * @param pending_width / pending_height 输出给离屏 resize，至少钳到 2
 * @param out_viewport_hovered 可为 nullptr；对应上一帧 Image 的 IsItemHovered
 * @param on_wheel_if_hovered 悬停且 MouseWheel != 0 时调用，参数为
 * ImGui::GetIO().MouseWheel
 * @param after_image Image 之后绘制（如调试标注），在滚轮与 hover 写入之后调用
 */
void imgui_scene_viewport_panel(
    const char *title, ImTextureID texture_id, uint32_t *pending_width,
    uint32_t *pending_height, bool *out_viewport_hovered,
    const std::function<void(float wheel_delta)> &on_wheel_if_hovered = {},
    const std::function<void(const TextureViewRect &)> &after_image = {});

} // namespace ui
} // namespace lumen
