/**
 * @file input_bridge.cpp
 * @brief 输入桥接实现
 */

#include "ui/input_bridge.hpp"
#include "platform/event_pump.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <SDL3/SDL.h>

namespace lumen::ui {

void imgui_process_sdl_event(const void *sdlEvent) {
    if (sdlEvent) {
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(sdlEvent));
    }
}

bool imgui_wants_mouse() { return ImGui::GetIO().WantCaptureMouse; }

bool imgui_wants_keyboard() { return ImGui::GetIO().WantCaptureKeyboard; }

bool imgui_wants_any_input() {
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void imgui_setup_event_pump(platform::EventPump &pump) {
    pump.add_sdl_event_handler(imgui_process_sdl_event);
}

} // namespace lumen::ui
