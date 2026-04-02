/**
 * @file gltf/gltf_scene_mesh.hpp
 * @brief glTF / GLB：解析并填充 `SceneMeshAsset`（由 `GltfSceneImporter` 调用）
 */

#pragma once

#include <string_view>

#include <vulkan/vulkan.h>

#include "scene/scene_mesh_asset.hpp"

namespace lumen::render {
class CommandPool;
class Context;
} // namespace lumen::render

namespace lumen::scene {

bool import_gltf_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                            lumen::render::CommandPool &cmd_pool,
                            std::string_view gltf_path, SceneMeshAsset &out,
                            const SceneMeshLoadOptions &opts = {},
                            std::string *error_message = nullptr);

} // namespace lumen::scene
