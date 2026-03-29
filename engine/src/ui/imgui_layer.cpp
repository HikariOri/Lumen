/**
 * @file imgui_layer.cpp
 */

#include "ui/imgui_layer.hpp"
#include "core/logger.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "ui/imgui_backend.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

namespace lumen::ui {

namespace {

void apply_want_capture_to_event(platform::DispatchableEvent &de,
                                 bool block_events) {
    if (!block_events || de.handled) {
        return;
    }
    const uint32_t cat = platform::event_categories(de.event);
    ImGuiIO &io = ImGui::GetIO();
    if (platform::event_in_category(cat, platform::EventCategory::Mouse)) {
        if (io.WantCaptureMouse) {
            de.handled = true;
        }
    }
    if (platform::event_in_category(cat, platform::EventCategory::Keyboard)) {
        if (io.WantCaptureKeyboard) {
            de.handled = true;
        }
    }
}

} // namespace

void imgui_process_sdl_event(const void *sdlEvent) {
    if (sdlEvent != nullptr) {
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(sdlEvent));
    }
}

bool imgui_wants_mouse() { return ImGui::GetIO().WantCaptureMouse; }

bool imgui_wants_keyboard() { return ImGui::GetIO().WantCaptureKeyboard; }

bool imgui_wants_any_input() {
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void ImGuiLayer::attach(platform::EventPump &pump) {
    if (attached_) {
        LUMEN_LOG_WARN("ImGuiLayer::attach 重复调用，已忽略");
        return;
    }
    pump.add_sdl_event_handler(imgui_process_sdl_event);
    pump.push_overlay([this](platform::DispatchableEvent &de) {
        on_event(de);
    });
    attached_ = true;
}

void ImGuiLayer::begin_frame() { imgui_backend_new_frame(); }

void ImGuiLayer::end_frame(VkCommandBuffer cmd) const {
    imgui_backend_render(cmd);
}

void ImGuiLayer::on_event(platform::DispatchableEvent &de) {
    apply_want_capture_to_event(de, block_events_);
}

} // namespace lumen::ui
