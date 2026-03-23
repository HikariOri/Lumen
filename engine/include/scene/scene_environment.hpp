/**
 * @file scene_environment.hpp
 * @brief 场景级 IBL / 环境光设置（不挂实体；由渲染器或示例持有）
 *
 * 与 docs/design/material-system-ibl-pbr.md 一致。
 */

#pragma once

#include <string>

namespace lumen {
namespace scene {

/// 全局环境：立方体贴图来源路径、曝光与 IBL 强度
struct SceneEnvironment {
    /// 六面图目录（px/nx/py/ny/pz/nz.png 或 .jpg），或单张等距柱状 `.hdr` 文件路径；空表示程序化天空
    std::string cubemap_directory;

    float exposure { 1.0f };
    float ibl_strength { 1.0f };
};

} // namespace scene
} // namespace lumen
