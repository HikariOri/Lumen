/**
 * @file scene_camera_controller.hpp
 * @brief `ISceneCameraController`：场景相机导航控制器接口
 */

#pragma once

#include "scene/scene_camera.hpp"

namespace lumen {
namespace scene {

/**
 * @brief 场景相机控制器接口：将导航状态写入
 * `SceneCamera`（轨道、第一人称等由子类实现）
 */
class ISceneCameraController {
public:
    virtual ~ISceneCameraController() = default;

    /// 宜在每帧取 `view_matrix` 前调用
    virtual void apply_to(SceneCamera &cam) const = 0;
};

} // namespace scene
} // namespace lumen
