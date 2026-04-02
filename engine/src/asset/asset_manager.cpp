/**
 * @file asset/asset_manager.cpp
 */

#include "asset/asset_manager.hpp"

#include "asset/asset_id.hpp"
#include "asset/mesh_scene_importer.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/buffer.hpp"

#include <algorithm>
#include <cctype>

#include <ghc/filesystem.hpp>

namespace lumen::asset {
namespace {

namespace fs = ghc::filesystem;

[[nodiscard]] bool cache_key_for_path_prefix(const std::string &key,
                                             const std::string &norm_path) {
    if (key.size() < norm_path.size()) {
        return false;
    }
    if (key.compare(0, norm_path.size(), norm_path) != 0) {
        return false;
    }
    return key.size() == norm_path.size() || key[norm_path.size()] == '|';
}

} // namespace

AssetManager &AssetManager::instance() {
    static AssetManager m {};
    return m;
}

AssetManager::AssetManager() { register_default_importers(); }

void AssetManager::register_default_importers() {
    importers_.push_back(make_gltf_scene_importer());
    importers_.push_back(make_obj_mesh_scene_importer());
    importers_.push_back(make_lumenmesh_scene_importer());
}

void AssetManager::cache_insert(
    const std::string &cache_key,
    const std::shared_ptr<lumen::scene::SceneMeshAsset> &sp) {
    const AssetId aid = make_scene_mesh_asset_id(cache_key);
    by_cache_key_[cache_key] = sp;
    by_asset_id_[aid] = sp;
    id_to_cache_key_[aid] = cache_key;
}

std::string AssetManager::extension_lower(const std::string_view path) const {
    const fs::path p { std::string { path } };
    std::string e = p.extension().string();
    for (char &ch : e) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return e;
}

IMeshSceneImporter *AssetManager::importer_for_path(const std::string_view path) {
    const std::string ext = extension_lower(path);
    for (auto &imp : importers_) {
        if (imp->supports_extension(ext)) {
            return imp.get();
        }
    }
    return nullptr;
}

AssetId AssetManager::scene_mesh_asset_id(
    const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts) const {
    const std::string key = make_scene_mesh_cache_key(
        path, opts.recenterToOrigin, opts.uniformScaleMaxAxis);
    return lumen::asset::make_scene_mesh_asset_id(key);
}

std::shared_ptr<lumen::scene::SceneMeshAsset> AssetManager::load_scene_mesh(
    lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts,
    std::string *error_message) {
    const std::string cache_key =
        make_scene_mesh_cache_key(path, opts.recenterToOrigin,
                                  opts.uniformScaleMaxAxis);
    {
        std::lock_guard lock { mutex_ };
        if (const auto it = by_cache_key_.find(cache_key);
            it != by_cache_key_.end()) {
            return it->second;
        }
    }
    IMeshSceneImporter *imp = importer_for_path(path);
    if (imp == nullptr) {
        if (error_message != nullptr) {
            *error_message += "无可用 Importer（扩展名不支持）\n";
        }
        return nullptr;
    }
    auto loaded = std::make_shared<lumen::scene::SceneMeshAsset>();
    if (!imp->import(ctx, transfer_queue, cmd_pool, path, *loaded, opts,
                     error_message)) {
        return nullptr;
    }
    std::lock_guard lock { mutex_ };
    if (const auto it = by_cache_key_.find(cache_key);
        it != by_cache_key_.end()) {
        return it->second;
    }
    cache_insert(cache_key, loaded);
    return loaded;
}

std::shared_ptr<lumen::scene::SceneMeshAsset>
AssetManager::try_get_scene_mesh(const std::string_view path,
                                 const lumen::scene::SceneMeshLoadOptions &opts) const {
    const std::string cache_key =
        make_scene_mesh_cache_key(path, opts.recenterToOrigin,
                                  opts.uniformScaleMaxAxis);
    std::lock_guard lock { mutex_ };
    const auto it = by_cache_key_.find(cache_key);
    if (it == by_cache_key_.end()) {
        return nullptr;
    }
    return it->second;
}

SceneMeshAssetHandle AssetManager::load_scene_mesh_handle(
    lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts,
    std::string *error_message) {
    auto sp = load_scene_mesh(ctx, transfer_queue, cmd_pool, path, opts,
                              error_message);
    if (!sp) {
        return INVALID_SCENE_MESH_HANDLE;
    }
    const std::string cache_key =
        make_scene_mesh_cache_key(path, opts.recenterToOrigin,
                                  opts.uniformScaleMaxAxis);
    return SceneMeshAssetHandle {
        make_scene_mesh_asset_id(cache_key) };
}

std::shared_ptr<lumen::scene::SceneMeshAsset>
AssetManager::try_get_scene_mesh(const SceneMeshAssetHandle handle) const {
    if (!handle.valid()) {
        return nullptr;
    }
    std::lock_guard lock { mutex_ };
    const auto it = by_asset_id_.find(handle.id);
    if (it == by_asset_id_.end()) {
        return nullptr;
    }
    return it->second;
}

void AssetManager::unload_scene_mesh(const std::string_view path) {
    const std::string norm = normalize_scene_mesh_path_key(path);
    std::lock_guard lock { mutex_ };
    for (auto it = by_cache_key_.begin(); it != by_cache_key_.end();) {
        if (!cache_key_for_path_prefix(it->first, norm)) {
            ++it;
            continue;
        }
        const AssetId aid = make_scene_mesh_asset_id(it->first);
        by_asset_id_.erase(aid);
        id_to_cache_key_.erase(aid);
        it = by_cache_key_.erase(it);
    }
}

void AssetManager::unload_scene_mesh(const SceneMeshAssetHandle handle) {
    if (!handle.valid()) {
        return;
    }
    std::lock_guard lock { mutex_ };
    const auto hit = id_to_cache_key_.find(handle.id);
    if (hit == id_to_cache_key_.end()) {
        return;
    }
    const std::string key = hit->second;
    by_cache_key_.erase(key);
    by_asset_id_.erase(handle.id);
    id_to_cache_key_.erase(hit);
}

void AssetManager::clear_scene_meshes() {
    std::lock_guard lock { mutex_ };
    by_cache_key_.clear();
    by_asset_id_.clear();
    id_to_cache_key_.clear();
}

} // namespace lumen::asset
