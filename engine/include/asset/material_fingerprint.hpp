/**
 * @file asset/material_fingerprint.hpp
 * @brief `MaterialLoadDesc` → 稳定缓存键（路径规范化 + 格式槽位 + 因子/标志）
 */

#pragma once

#include <string>

namespace lumen::render {
struct MaterialLoadDesc;
} // namespace lumen::render

namespace lumen::asset {

/**
 * @brief 与 glTF 加载及 `TextureRegistry` 键一致：各贴图相对路径 + VkFormat + 默认 sampler 指纹
 */
[[nodiscard]] std::string material_load_desc_cache_key(
    const lumen::render::MaterialLoadDesc &desc);

} // namespace lumen::asset
