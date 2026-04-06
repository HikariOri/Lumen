/**
 * @file environment_panel.hpp
 * @brief IBL / 环境立方体贴图、曝光、强度（全局设置，不依赖选中实体）
 */

#pragma once

#include "ui/panel.hpp"

#include <functional>
#include <string>

namespace lumen {
namespace scene {
struct SceneEnvironment;
} // namespace scene

namespace ui {

/**
 * @brief 环境面板：编辑 `SceneEnvironment` 字符串字段与标量；加载/恢复由回调完成
 *
 * `on_request_reload_from_directory`：由应用实现 `wait_idle`、加载立方体、更新 Descriptor。
 * `on_request_procedural`：恢复程序化天空等默认环境。
 */
class EnvironmentPanel final : public IPanel {
public:
    EnvironmentPanel(scene::SceneEnvironment *env,
                       std::function<void()> on_request_reload_from_directory,
                       std::function<void()> on_request_procedural);

    void on_imgui_render() override;

private:
    scene::SceneEnvironment *env_;
    std::function<void()> reload_cb_;
    std::function<void()> procedural_cb_;

    static constexpr int kPathBuf { 1024 };
    char dir_buf_[kPathBuf] {};
};

} // namespace ui
} // namespace lumen
