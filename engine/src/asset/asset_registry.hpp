/**
 * @file asset/asset_registry.hpp
 * @brief 集中资产门面：`Texture` / PBR `Material` / 场景网格（多格式）/ ImGui 字体
 */

#pragma once

#include <memory>
#include <string_view>

#include "render/vulkan.hpp"

#include "asset/asset_manager.hpp"
#include "asset/font_registry.hpp"
#include "asset/material_registry.hpp"
#include "asset/texture_registry.hpp"
#include "scene/scene_mesh_asset.hpp"

namespace lumen::render {
class CommandPool;
class Context;
} // namespace lumen::render

namespace lumen::asset {

class AssetRegistry {
public:
    [[nodiscard]] static AssetRegistry &instance();

    TextureRegistry &textures() { return textures_; }
    MaterialRegistry &materials() { return materials_; }
    FontRegistry &fonts() { return fonts_; }

    [[nodiscard]] AssetManager &scene_meshes() {
        return AssetManager::instance();
    }

    /// 释放材质 / 纹理 / 场景网格缓存及字体会话中的 GPU 资源（须在 `Context` 销毁前调用）
    void clear_all();

    [[nodiscard]] std::shared_ptr<lumen::scene::SceneMeshAsset> load_scene_mesh(
        lumen::render::Context &ctx, VkQueue transfer_queue,
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

private:
    AssetRegistry() = default;

    TextureRegistry textures_;
    MaterialRegistry materials_;
    FontRegistry fonts_;
};

} // namespace lumen::asset
