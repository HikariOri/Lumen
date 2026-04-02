/**
 * @file asset/asset_id.cpp
 */

#include "asset/asset_id.hpp"

#include <cctype>

#include <ghc/filesystem.hpp>

namespace lumen::asset {
namespace {

namespace fs = ghc::filesystem;

} // namespace

std::string normalize_scene_mesh_path_key(const std::string_view path) {
    std::error_code ec;
    fs::path p { std::string { path } };
    const fs::path canon = fs::weakly_canonical(fs::absolute(p), ec);
    if (!ec) {
        return canon.lexically_normal().generic_string();
    }
    return fs::absolute(p).lexically_normal().generic_string();
}

AssetId fnv1a64_string(const std::string_view s) {
    constexpr AssetId offset { 14695981039346656037ULL };
    constexpr AssetId prime { 1099511628211ULL };
    AssetId h { offset };
    for (unsigned char c : s) {
        h ^= static_cast<AssetId>(c);
        h *= prime;
    }
    return h;
}

std::string make_scene_mesh_cache_key(const std::string_view path,
                                      const bool recenter_to_origin,
                                      const float uniform_scale_max_axis) {
    return normalize_scene_mesh_path_key(path) + "|" +
           std::to_string(static_cast<int>(recenter_to_origin)) + "|" +
           std::to_string(uniform_scale_max_axis);
}

AssetId make_scene_mesh_asset_id(const std::string_view cache_key) {
    return fnv1a64_string(cache_key);
}

} // namespace lumen::asset
