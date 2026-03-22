/**
 * @file imgui_backend.hpp
 * @brief ImGui Vulkan + SDL3 后端封装
 *
 * 提供 ImGui 初始化、每帧更新、渲染、关闭的便捷接口。
 */

#pragma once

#include <vulkan/vulkan.h>

#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/swapchain.hpp"

struct SDL_Window;

namespace lumen {
namespace ui {

/**
 * @brief ImGui Vulkan 初始化参数
 */
struct ImGuiBackendInitInfo {
    const render::Context *ctx { nullptr };
    const render::Swapchain *swapchain { nullptr };
    VkRenderPass renderPass { VK_NULL_HANDLE };
    SDL_Window *window { nullptr };

    /// 含中文等 CJK 的主字体文件路径（.ttf / .otf / .ttc），UTF-8 编码。
    /// - 若 `cjk_font_japanese_merge_path` 为空：按原逻辑仅加载常用简体（
    ///   GetGlyphRangesChineseSimplifiedCommon）。
    /// - 若二者均非空：先用本路径加载默认拉丁，再 MergeMode 合并
    ///   GetGlyphRangesChineseFull，再 MergeMode 合并日文字形（见
    ///   `cjk_font_japanese_merge_path`）。中文合并优先于日文。
    const char *cjk_font_ttf_path { nullptr };
    /// 与简体主字体合并的日文字体路径；仅在与 `cjk_font_ttf_path` 同时非空时生效。
    const char *cjk_font_japanese_merge_path { nullptr };
    /// 与 cjk_font_ttf_path 配套的字号（像素）；<=0 时用 18
    float cjk_font_size_pixels { 18.0f };
};

/**
 * @brief 初始化 ImGui（Context + SDL3 + Vulkan 后端）
 * @return 成功返回 true
 */
bool imgui_backend_init(const ImGuiBackendInitInfo &info);

/**
 * @brief 关闭 ImGui 后端（在 ctx.wait_idle() 之后调用）
 */
void imgui_backend_shutdown();

/**
 * @brief Swapchain 重建后调用，更新 MinImageCount
 */
void imgui_backend_set_min_image_count(uint32_t minImageCount);

/**
 * @brief 每帧开始：SDL3 NewFrame + Vulkan NewFrame + ImGui::NewFrame
 */
void imgui_backend_new_frame();

/**
 * @brief 在 RenderPass 内、3D 绘制之后，渲染 ImGui
 * @param cmd 当前帧的 VkCommandBuffer
 */
void imgui_backend_render(VkCommandBuffer cmd);

/**
 * @brief 注册 Vulkan 纹理供 ImGui::Image 使用
 * @return ImTextureID（即 VkDescriptorSet），用于 ImGui::Image(id, size)
 */
void *imgui_backend_add_texture(VkSampler sampler, VkImageView imageView,
                                VkImageLayout imageLayout);

/**
 * @brief 移除已注册的纹理（resize 时需先移除再重新添加）
 */
void imgui_backend_remove_texture(void *textureId);

} // namespace ui
} // namespace lumen
