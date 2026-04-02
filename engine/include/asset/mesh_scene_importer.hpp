/**
 * @file asset/mesh_scene_importer.hpp
 * @brief 磁盘网格/场景 → `SceneMeshAsset` 的 Importer 接口
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "render/vulkan.hpp"

namespace lumen::render {
class CommandPool;
class Context;
} // namespace lumen::render

namespace lumen::scene {
struct SceneMeshAsset;
struct SceneMeshLoadOptions;
} // namespace lumen::scene

namespace lumen::asset {

class IMeshSceneImporter {
public:
    virtual ~IMeshSceneImporter() = default;

    [[nodiscard]] virtual bool import(
        lumen::render::Context &ctx, VkQueue transfer_queue,
        lumen::render::CommandPool &cmd_pool, std::string_view path,
        lumen::scene::SceneMeshAsset &out,
        const lumen::scene::SceneMeshLoadOptions &opts,
        std::string *error_message) const = 0;

    [[nodiscard]] virtual bool
    supports_extension(std::string_view ext_lower) const = 0;
};

[[nodiscard]] std::unique_ptr<IMeshSceneImporter> make_gltf_scene_importer();
[[nodiscard]] std::unique_ptr<IMeshSceneImporter> make_obj_mesh_scene_importer();
[[nodiscard]] std::unique_ptr<IMeshSceneImporter> make_lumenmesh_scene_importer();

} // namespace lumen::asset
