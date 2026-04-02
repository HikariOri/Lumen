/**
 * @file scene/scene_mesh_asset.hpp
 * @brief 场景级网格资产：逻辑几何 + GPU 缓冲 + PBR 材质 + 场景节点表（格式无关）
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

#include "asset/geometry/mesh_asset.hpp"
#include "asset/pbr_material_instance.hpp"

namespace lumen::render {
class CommandPool;
class Context;
class IndexBuffer;
struct Material;
class Texture;
class VertexBuffer;
} // namespace lumen::render

namespace lumen::scene {

/// 上传前对 CPU 顶点位置做预处理（部分 Importer：仅当资产 **仅含一个 mesh** 时生效）
struct SceneMeshLoadOptions {
    bool recenterToOrigin { true };
    float uniformScaleMaxAxis { 0.0F };
};

struct SceneNode {
    int parent_index { -1 };
    std::string name {};
    glm::mat4 local_transform { 1.0F };
    int mesh_index { -1 };
};

struct SceneMeshAsset {
    lumen::asset::geometry::Model model {};
    std::vector<std::shared_ptr<lumen::asset::PbrMaterialInstance>> materials {};
    std::unique_ptr<lumen::render::VertexBuffer> vertexBuffer {};
    std::unique_ptr<lumen::render::IndexBuffer> indexBuffer {};
    std::vector<SceneNode> scene_nodes {};

    std::size_t statsVertexCount { 0 };
    std::size_t statsIndexCount { 0 };

    SceneMeshAsset() = default;
    SceneMeshAsset(SceneMeshAsset &&) noexcept = default;
    SceneMeshAsset &operator=(SceneMeshAsset &&) noexcept = default;
    SceneMeshAsset(const SceneMeshAsset &) = delete;
    SceneMeshAsset &operator=(const SceneMeshAsset &) = delete;

    [[nodiscard]] lumen::asset::geometry::MeshBuffer geometry() const {
        return { .vertexBuffer = vertexBuffer.get(),
                 .indexBuffer = indexBuffer.get() };
    }
};

} // namespace lumen::scene
