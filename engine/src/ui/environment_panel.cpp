/**
 * @file environment_panel.cpp
 */

#include "ui/environment_panel.hpp"

#include "scene/scene_environment.hpp"
#include "ui/imgui_hazel_helpers.hpp"

#include <cstdint>
#include <cstdio>

#include <imgui.h>

namespace lumen::ui {

EnvironmentPanel::EnvironmentPanel(scene::SceneEnvironment *env,
                                   std::function<void()> on_request_reload,
                                   std::function<void()> on_request_procedural)
    : env_(env), reload_cb_(std::move(on_request_reload)),
      procedural_cb_(std::move(on_request_procedural)) {
    if (env_) {
        std::snprintf(dir_buf_, sizeof(dir_buf_), "%s",
                      env_->cubemap_directory.c_str());
    }
}

void EnvironmentPanel::on_imgui_render() {
    if (!env_) {
        return;
    }

    ImGui::Begin("Environment (IBL)");

    static const void *kEnvSource { reinterpret_cast<const void *>(uintptr_t { 0xE01 }) };
    static const void *kEnvTone { reinterpret_cast<const void *>(uintptr_t { 0xE02 }) };

    if (imgui_hazel_component_begin("Environment map", kEnvSource)) {
        ImGui::TextUnformatted(
            "路径：六面图目录（px…nz.png/.jpg）或单张等距柱状 .hdr");
        ImGui::InputText("##env_dir", dir_buf_, sizeof(dir_buf_));
        if (ImGui::Button("应用路径并加载", ImVec2(-1.0f, 0.0f))) {
            env_->cubemap_directory = dir_buf_;
            if (reload_cb_) {
                reload_cb_();
            }
        }
        if (ImGui::Button("恢复程序化天空", ImVec2(-1.0f, 0.0f))) {
            env_->cubemap_directory.clear();
            dir_buf_[0] = '\0';
            if (procedural_cb_) {
                procedural_cb_();
            }
        }
        ImGui::TextDisabled("切换环境会 wait idle GPU；详见 "
                            "material-system-ibl-pbr.md。");
        ImGui::TreePop();
    }

    if (imgui_hazel_component_begin("Tone & IBL", kEnvTone)) {
        ImGui::SliderFloat("Exposure", &env_->exposure, 0.1f, 4.0f, "%.2f");
        ImGui::SliderFloat("IBL strength", &env_->ibl_strength, 0.0f, 2.0f,
                           "%.2f");
        ImGui::TreePop();
    }
    ImGui::End();
}

} // namespace lumen::ui
