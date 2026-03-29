/**
 * @file imgui_layer.hpp
 * @brief 对齐 Hazel `ImGuiLayer`：SDL 输入、`OnEvent`（阻塞游戏层）、`Begin`/`End` 帧范围
 *
 * Hazel `Application::Run` 顺序参考：
 * - 各层 `OnUpdate`
 * - `m_ImGuiLayer->Begin()`
 * - 各层 `OnImGuiRender()`（在此之间写 `ImGui::Begin`/`End`）
 * - `m_ImGuiLayer->End()`
 *
 * Lumen 对应：`poll()` 之后 → `begin_frame()` → 构建 UI → `end_frame(cmd)`。
 */

#pragma once

#include <vulkan/vulkan.h>

namespace lumen {
namespace platform {
class EventPump;
struct DispatchableEvent;
} // namespace platform
namespace ui {

void imgui_process_sdl_event(const void *sdlEvent);

bool imgui_wants_mouse();
bool imgui_wants_keyboard();
bool imgui_wants_any_input();

/**
 * @brief Hazel `ImGuiLayer`：挂接 `EventPump` + 每帧 NewFrame / Render
 */
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ImGuiLayer(const ImGuiLayer &) = delete;
    ImGuiLayer &operator=(const ImGuiLayer &) = delete;
    ImGuiLayer(ImGuiLayer &&) = delete;
    ImGuiLayer &operator=(ImGuiLayer &&) = delete;

    /**
     * @brief 注册 SDL 转发 + Overlay `OnEvent`（`WantCapture*` → `handled`）
     * @note 重复 `attach` 同一实例会被忽略（打日志）；须在 `imgui_backend_init` 之后调用
     */
    void attach(platform::EventPump &pump);

    [[nodiscard]] bool is_attached() const noexcept { return attached_; }

    /**
     * @brief Hazel `ImGuiLayer::Begin`：Vulkan/SDL NewFrame、Dock 宿主、ImGuizmo::BeginFrame
     */
    void begin_frame();

    /**
     * @brief Hazel `ImGuiLayer::End`：须在 swapchain 的 RenderPass **内**调用
     */
    void end_frame(VkCommandBuffer cmd) const;

    /**
     * @brief 对应 Hazel `m_BlockEvents`：为 false 时不根据 `WantCapture*` 写 `handled`
     */
    void set_block_events(bool block) noexcept { block_events_ = block; }
    [[nodiscard]] bool block_events() const noexcept { return block_events_; }

    /**
     * @brief 可由自定义 `EventPump::push_overlay` 转发；默认由 `attach` 注册
     */
    void on_event(platform::DispatchableEvent &de);

private:
    bool block_events_ { true };
    bool attached_ { false };
};

} // namespace ui
} // namespace lumen
