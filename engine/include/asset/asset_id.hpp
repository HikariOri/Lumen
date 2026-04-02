/**
 * @file asset/asset_id.hpp
 * @brief 资产路径规范化与稳定哈希（`AssetId`）
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lumen::asset {

using AssetId = std::uint64_t;

[[nodiscard]] std::string normalize_scene_mesh_path_key(std::string_view path);

[[nodiscard]] AssetId fnv1a64_string(std::string_view s);

[[nodiscard]] std::string make_scene_mesh_cache_key(
    std::string_view path, bool recenter_to_origin, float uniform_scale_max_axis);

[[nodiscard]] AssetId make_scene_mesh_asset_id(std::string_view cache_key);

} // namespace lumen::asset
