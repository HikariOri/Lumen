/**
 * @file asset/asset_manager.hpp
 * @brief 场景网格资产：`AssetId` 缓存、按扩展名分发的 Importer、`SceneMeshAssetHandle`
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "asset/asset_handle.hpp"
#include "asset/asset_id.hpp"
#include "scene/scene_mesh_asset.hpp"

namespace lumen::render {
class CommandPool;
class Context;
} // namespace lumen::render

namespace lumen::asset {

class IMeshSceneImporter;

using SceneMeshAssetHandle = AssetHandle<SceneMeshTag>;

inline constexpr SceneMeshAssetHandle INVALID_SCENE_MESH_HANDLE {};

class AssetManager {
public:
    [[nodiscard]] static AssetManager &instance();

    AssetManager(const AssetManager &) = delete;
    AssetManager &operator=(const AssetManager &) = delete;

    [[nodiscard]] std::shared_ptr<lumen::scene::SceneMeshAsset>
    load_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                    lumen::render::CommandPool &cmd_pool, std::string_view path,
                    const lumen::scene::SceneMeshLoadOptions &opts = {},
                    std::string *error_message = nullptr);

    [[nodiscard]] std::shared_ptr<lumen::scene::SceneMeshAsset>
    try_get_scene_mesh(std::string_view path,
                       const lumen::scene::SceneMeshLoadOptions &opts = {}) const;

    [[nodiscard]] SceneMeshAssetHandle
    load_scene_mesh_handle(lumen::render::Context &ctx, VkQueue transfer_queue,
                           lumen::render::CommandPool &cmd_pool,
                           std::string_view path,
                           const lumen::scene::SceneMeshLoadOptions &opts = {},
                           std::string *error_message = nullptr);

    [[nodiscard]] std::shared_ptr<lumen::scene::SceneMeshAsset>
    try_get_scene_mesh(SceneMeshAssetHandle handle) const;

    void unload_scene_mesh(std::string_view path);
    void unload_scene_mesh(SceneMeshAssetHandle handle);
    void clear_scene_meshes();

    [[nodiscard]] AssetId
    scene_mesh_asset_id(std::string_view path,
                        const lumen::scene::SceneMeshLoadOptions &opts) const;

private:
    AssetManager();
    ~AssetManager() = default;

    void register_default_importers();

    [[nodiscard]] IMeshSceneImporter *importer_for_path(std::string_view path);

    [[nodiscard]] std::string extension_lower(std::string_view path) const;

    void cache_insert(const std::string &cache_key,
                      const std::shared_ptr<lumen::scene::SceneMeshAsset> &sp);

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<IMeshSceneImporter>> importers_;

    std::unordered_map<std::string, std::shared_ptr<lumen::scene::SceneMeshAsset>>
        by_cache_key_;
    std::unordered_map<AssetId, std::shared_ptr<lumen::scene::SceneMeshAsset>>
        by_asset_id_;
    std::unordered_map<AssetId, std::string> id_to_cache_key_;
};

} // namespace lumen::asset
