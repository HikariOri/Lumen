/**
 * @file asset/gltf_mesh_scene_importer.cpp
 * @brief glTF / GLB → `SceneMeshAsset`（`GltfSceneImporter` / `import_gltf_scene_mesh`）
 */

#include "asset/mesh_scene_importer.hpp"

#include "gltf/gltf_scene_mesh.hpp"
#include "scene/scene_mesh_asset.hpp"

namespace lumen::asset {

class GltfSceneImporter final : public IMeshSceneImporter {
public:
    [[nodiscard]] bool import(lumen::render::Context &ctx, VkQueue transfer_queue,
                              lumen::render::CommandPool &cmd_pool,
                              const std::string_view path,
                              lumen::scene::SceneMeshAsset &out,
                              const lumen::scene::SceneMeshLoadOptions &opts,
                              std::string *error_message) const override {
        return lumen::scene::import_gltf_scene_mesh(ctx, transfer_queue, cmd_pool,
                                                    path, out, opts,
                                                    error_message);
    }

    [[nodiscard]] bool
    supports_extension(const std::string_view ext_lower) const override {
        return ext_lower == ".glb" || ext_lower == ".gltf";
    }
};

[[nodiscard]] std::unique_ptr<IMeshSceneImporter> make_gltf_scene_importer() {
    return std::make_unique<GltfSceneImporter>();
}

} // namespace lumen::asset
