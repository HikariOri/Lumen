/**
 * @file gltf/gltf_scene_mesh.hpp
 * @brief glTF / GLB → `Model` + GPU 缓冲 + PBR 材质（内部用 `gltf/gltf_loader.hpp` 的 `core::load_gltf`）
 *
 * 顶点布局与 `make_vertex_layout_pbr_forward_tangent()` / `pbr_forward.vert` 一致。
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "scene/mesh.hpp"

namespace lumen::render {
class CommandPool;
class Context;
class IndexBuffer;
struct Material;
class Texture;
class VertexBuffer;
} // namespace lumen::render

namespace lumen::scene {

/// 上传前对 CPU 顶点位置做预处理
struct GltfSceneMeshLoadOptions {
    bool recenterToOrigin { true };
    /// `<= 0` 不缩放；`> 0` 时最长边缩放到该长度
    float uniformScaleMaxAxis { 0.F };
};

/// `Model`（与 glTF `meshes[]` 下标对齐）+ `Material` + GPU 资源；不可复制
struct GltfSceneMesh {
    lumen::scene::Model model {};
    std::vector<lumen::render::Material> materials {};
    std::unique_ptr<lumen::render::VertexBuffer> vertexBuffer {};
    std::unique_ptr<lumen::render::IndexBuffer> indexBuffer {};
    std::vector<std::unique_ptr<lumen::render::Texture>> textures {};

    std::size_t statsVertexCount { 0 };
    std::size_t statsIndexCount { 0 };

    GltfSceneMesh() = default;
    GltfSceneMesh(GltfSceneMesh &&) noexcept = default;
    GltfSceneMesh &operator=(GltfSceneMesh &&) noexcept = default;
    GltfSceneMesh(const GltfSceneMesh &) = delete;
    GltfSceneMesh &operator=(const GltfSceneMesh &) = delete;

    [[nodiscard]] MeshBuffer geometry() const {
        return { .vertexBuffer = vertexBuffer.get(),
                 .indexBuffer = indexBuffer.get() };
    }
};

bool load_gltf_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                          lumen::render::CommandPool &cmd_pool,
                          std::string_view gltf_path, GltfSceneMesh &out,
                          const GltfSceneMeshLoadOptions &opts = {},
                          std::string *error_message = nullptr);

} // namespace lumen::scene
