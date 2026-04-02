/**
 * @file asset/asset_registry.cpp
 */

#include "asset/asset_registry.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"

namespace lumen::asset {

AssetRegistry &AssetRegistry::instance() {
    static AssetRegistry reg {};
    return reg;
}

void AssetRegistry::clear_all() {
    materials_.clear();
    textures_.clear();
    AssetManager::instance().clear_scene_meshes();
    fonts_.clear_session();
}

std::shared_ptr<lumen::scene::SceneMeshAsset> AssetRegistry::load_scene_mesh(
    lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts,
    std::string *error_message) {
    return AssetManager::instance().load_scene_mesh(ctx, transfer_queue, cmd_pool,
                                                    path, opts, error_message);
}

std::shared_ptr<lumen::scene::SceneMeshAsset>
AssetRegistry::try_get_scene_mesh(
    const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts) const {
    return AssetManager::instance().try_get_scene_mesh(path, opts);
}

SceneMeshAssetHandle AssetRegistry::load_scene_mesh_handle(
    lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, const std::string_view path,
    const lumen::scene::SceneMeshLoadOptions &opts,
    std::string *error_message) {
    return AssetManager::instance().load_scene_mesh_handle(
        ctx, transfer_queue, cmd_pool, path, opts, error_message);
}

std::shared_ptr<lumen::scene::SceneMeshAsset>
AssetRegistry::try_get_scene_mesh(const SceneMeshAssetHandle handle) const {
    return AssetManager::instance().try_get_scene_mesh(handle);
}

void AssetRegistry::unload_scene_mesh(const std::string_view path) {
    AssetManager::instance().unload_scene_mesh(path);
}

void AssetRegistry::unload_scene_mesh(const SceneMeshAssetHandle handle) {
    AssetManager::instance().unload_scene_mesh(handle);
}

} // namespace lumen::asset
