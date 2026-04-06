/**
 * @file asset/asset_handle.hpp
 * @brief 类型标记的 `AssetHandle<Tag>`（opaque id）
 */

#pragma once

#include "asset/asset_id.hpp"

namespace lumen::asset {

template <typename Tag>
struct AssetHandle {
    AssetId id {};

    [[nodiscard]] bool valid() const { return id != 0; }

    friend bool operator==(const AssetHandle &, const AssetHandle &) = default;
};

struct SceneMeshTag {};
using SceneMeshAssetHandle = AssetHandle<SceneMeshTag>;

} // namespace lumen::asset
