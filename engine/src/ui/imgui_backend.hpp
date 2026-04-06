/**
 * @file imgui_backend.hpp
 * @brief ImGui Vulkan + SDL3 后端封装
 *
 * @details
 * 封装 Dear ImGui 在 Vulkan + SDL3 环境下的初始化、帧更新、渲染与资源管理。
 *
 * ============================================================
 * 架构说明
 * ============================================================
 *
 * ImGui 在 Vulkan 下的使用通常涉及三部分：
 *
 * 1. 平台层（Platform Backend）
 *    - SDL3：窗口、输入（鼠标/键盘/事件）
 *
 * 2. 渲染层（Renderer Backend）
 *    - Vulkan：负责将 ImGui draw data 提交到 GPU
 *
 * 3. 用户层（Application）
 *    - 调用 NewFrame / Render / UI 构建逻辑
 *
 * 本模块统一封装上述流程，避免应用层直接接触 ImGui_ImplXXX。
 *
 * ============================================================
 * 生命周期
 * ============================================================
 *
 * 初始化：
 *   imgui_backend_init()
 *
 * 每帧（与 Hazel `ImGuiLayer::Begin`/`End` 对齐时，请经 `ImGuiLayer::begin_frame` /
 * `end_frame`，其内部调用本模块）：
 *   `imgui_backend_new_frame()`（SDL/Vulkan NewFrame、Dock、ImGuizmo::BeginFrame）
 *   → 用户在各「逻辑层」中构建 UI（`ImGui::Begin` / `End`）
 *   `imgui_backend_render(cmd)`（须在 swapchain RenderPass 内）
 *
 * 关闭：
 *   imgui_backend_shutdown()
 *
 * ============================================================
 * Vulkan 依赖
 * ============================================================
 *
 * - DescriptorPool（内部创建）
 * - RenderPass（必须与当前帧一致）
 * - CommandBuffer（调用 render 时提供）
 *
 * ============================================================
 * 注意事项
 * ============================================================
 *
 * @warning
 * - imgui_backend_render() 必须在 RenderPass 内调用
 * - 必须在 3D 渲染之后调用（UI 覆盖）
 * - shutdown 前需确保 GPU idle（vkDeviceWaitIdle）
 */

#pragma once

#include <cstdint>

#include "render/vulkan.hpp"

#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/swapchain.hpp"

struct SDL_Window;

namespace lumen {
namespace ui {

/**
 * @brief ImGui Vulkan 初始化参数
 *
 * @details
 * 描述 ImGui 后端初始化所需的 Vulkan 与窗口信息。
 */
struct ImGuiBackendInitInfo {

    /// Vulkan 上下文（设备 / 队列 / 实例）
    const render::Context *ctx { nullptr };

    /// Swapchain（用于获取图像数量 / 格式）
    const render::Swapchain *swapchain { nullptr };
 
    /**
     * @brief ImGui 使用的 RenderPass
     *
     * @note
     * - 必须与实际渲染 UI 时使用的 RenderPass 完全一致
     * - 通常为最终输出到 swapchain 的 pass
     */
    VkRenderPass renderPass { VK_NULL_HANDLE };

    /// SDL3 窗口（用于输入与 DPI 获取）
    SDL_Window *window { nullptr };

    /**
     * @brief 中文（CJK）主字体路径
     *
     * @details
     * 支持 UTF-8 路径，加载 TrueType / OpenType / TTC 字体。
     *
     * 行为：
     * - 若为空 → 使用 ImGui 默认字体
     * - 若仅此项存在：
     *     使用 GetGlyphRangesChineseSimplifiedCommon（常用简体）
     * - 若与 japanese_merge 同时存在：
     *     1. 加载本字体（作为基础）
     *     2. Merge Chinese Full
     *     3. Merge Japanese
     *
     * @note
     * 中文优先级高于日文
     */
    const char *cjk_font_ttf_path { nullptr };

    /**
     * @brief 日文字体（Merge）
     *
     * @details
     * 仅在 cjk_font_ttf_path 非空时生效。
     *
     * @note
     * 使用 MergeMode 合并到主字体中
     */
    const char *cjk_font_japanese_merge_path { nullptr };

    /**
     * @brief 字体大小（像素）
     *
     * @note
     * - <= 0 时使用默认值 18px
     * - 实际显示大小受 DPI scaling 影响
     */
    float cjk_font_size_pixels { 18.0F };

    /**
     * @brief 是否启用 ImGui Docking，并在 `imgui_backend_new_frame()` 内绘制全屏
     * Dock 宿主窗口
     */
    bool enable_docking { true };
};

/**
 * @brief 初始化 ImGui 后端（SDL3 + Vulkan）
 *
 * @param info 初始化参数
 * @return 初始化成功返回 true
 *
 * @details
 * 内部执行：
 * - 创建 ImGui Context
 * - 初始化 SDL3 backend（输入）
 * - 初始化 Vulkan backend（渲染）
 * - 创建字体纹理（上传 GPU）
 *
 * @note
 * - 必须在 Vulkan 初始化之后调用
 * - 仅调用一次
 */
bool imgui_backend_init(const ImGuiBackendInitInfo &info);

/**
 * @brief 关闭 ImGui 后端
 *
 * @details
 * 销毁：
 * - ImGui context
 * - Vulkan descriptor / pipeline / font texture
 *
 * @note
 * 必须在设备 idle 后调用：
 * @code
 * vkDeviceWaitIdle(device);
 * imgui_backend_shutdown();
 * @endcode
 */
void imgui_backend_shutdown();

/**
 * @brief 设置最小 swapchain image 数量
 *
 * @param minImageCount 最小图像数量
 *
 * @details
 * 当 swapchain 重建（resize）时调用：
 * - 更新 ImGui backend 内部 frame buffering
 *
 * @note
 * 通常等于 swapchain image count
 */
void imgui_backend_set_min_image_count(uint32_t minImageCount);

/**
 * @brief 每帧开始（NewFrame）
 *
 * @details
 * 调用顺序：
 * 1. ImGui_ImplSDL3_NewFrame()
 * 2. ImGui_ImplVulkan_NewFrame()
 * 3. ImGui::NewFrame()
 *
 * @note
 * 必须在所有 ImGui::Begin() 之前调用（Dock 宿主由本函数在 `NewFrame` 之后插入，
 * 应用侧其它窗口应在本函数返回之后再 `Begin`）
 */
void imgui_backend_new_frame();

/**
 * @brief 初始化时是否启用了 Docking（与 `ImGuiBackendInitInfo::enable_docking` 一致）
 */
[[nodiscard]] bool imgui_backend_docking_enabled() noexcept;

/**
 * @brief 主 Dock 空间 ID（与 `imgui_backend_new_frame` 内 `DockSpace` 一致）
 *
 * @return 未启用 Docking 时尚未建立时为 0；否则为当前帧的 `ImGuiID`（可传给
 * `PanelManager::set_default_dock_id` 等）
 */
[[nodiscard]] std::uint32_t imgui_backend_main_dockspace_id() noexcept;

/**
 * @brief 渲染 ImGui（提交 draw calls）
 *
 * @param cmd 当前帧的 VkCommandBuffer
 *
 * @details
 * 内部流程：
 * - ImGui::Render()
 * - ImGui_ImplVulkan_RenderDrawData()
 *
 * @warning
 * - 必须在 RenderPass 内调用
 * - 必须在 3D 渲染之后调用
 *
 * @code
 * vkCmdBeginRenderPass(...)
 * draw_scene();
 * imgui_backend_render(cmd);
 * vkCmdEndRenderPass(...)
 * @endcode
 */
void imgui_backend_render(VkCommandBuffer cmd);

/**
 * @brief 注册纹理供 ImGui::Image 使用
 *
 * @param sampler Vulkan Sampler
 * @param imageView Vulkan ImageView
 * @param imageLayout 图像布局（通常为 SHADER_READ_ONLY_OPTIMAL）
 *
 * @return ImTextureID（本实现中为 VkDescriptorSet）
 *
 * @details
 * ImGui 并不直接使用 VkImage，而是通过 ImTextureID（void*）抽象：
 *
 * 实际实现：
 * - 创建 DescriptorSet
 * - 写入 sampler + imageView
 *
 * 使用方式：
 * @code
 * ImTextureID id = imgui_backend_add_texture(...);
 * ImGui::Image(id, ImVec2(100, 100));
 * @endcode
 *
 * @note
 * - 每个 texture 对应一个 descriptor set
 * - 需要手动释放（见 remove_texture）
 */
void *imgui_backend_add_texture(VkSampler sampler, VkImageView imageView,
                                VkImageLayout imageLayout);

/**
 * @brief 移除 ImGui 纹理
 *
 * @param textureId 由 add_texture 返回的 ID
 *
 * @details
 * 释放对应的 descriptor set
 *
 * @note
 * - 在纹理销毁或 resize 时必须调用
 * - 否则会造成 descriptor 泄漏
 */
void imgui_backend_remove_texture(void *textureId);

} // namespace ui
} // namespace lumen
