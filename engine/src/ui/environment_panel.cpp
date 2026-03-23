/**
 * @file environment_panel.cpp
 */

#include "ui/environment_panel.hpp"

#include "scene/scene_environment.hpp"

#include <cstring>

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
    ImGui::TextUnformatted(
        "环境路径：六面图目录（px…nz.png/.jpg）或单张等距柱状 .hdr");
    ImGui::InputText("##env_dir", dir_buf_, sizeof(dir_buf_));
    if (ImGui::Button("应用路径并加载", ImVec2(-1, 0))) {
        env_->cubemap_directory = dir_buf_;
        if (reload_cb_) {
            reload_cb_();
        }
    }
    if (ImGui::Button("恢复程序化天空", ImVec2(-1, 0))) {
        env_->cubemap_directory.clear();
        dir_buf_[0] = '\0';
        if (procedural_cb_) {
            procedural_cb_();
        }
    }
    ImGui::Separator();
    ImGui::SliderFloat("曝光 (Env Exposure)", &env_->exposure, 0.1f, 4.0f,
                       "%.2f");
    ImGui::SliderFloat("IBL 强度", &env_->ibl_strength, 0.0f, 2.0f, "%.2f");
    ImGui::TextDisabled("切换环境贴图会阻塞等待 GPU 空闲；详见 "
                        "material-system-ibl-pbr.md。");
    ImGui::End();
}

} // namespace lumen::ui
