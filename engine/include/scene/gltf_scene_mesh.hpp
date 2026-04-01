/**
 * @file gltf_scene_mesh.hpp
 * @brief 将 glTF / GLB 一步加载为可绘制的 `scene::Model`（GPU 缓冲 + PBR 材质）
 *
 * @details
 * 内部调用 `core::load_gltf` 解析几何与材质描述，再计算切线、上传
 * `VertexBuffer` / `IndexBuffer`、按路径去重加载贴图并填充 `render::Material`。
 * 调用方持有返回的 `GltfSceneMesh` 即可渲染，无需再处理 `CpuMesh` /
 * `PrimitiveSlice`。
 *
 * @note
 * 顶点布局与 `make_vertex_layout_pbr_forward_tangent()` 及 `pbr_forward.vert`
 * 一致。
 *
 * @ingroup lumen_scene_mesh
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

/**
 * @brief 控制几何预处理（在计算切线与上传 GPU 之前作用于 CPU 顶点位置）
 */
struct GltfSceneMeshLoadOptions {
    /// 将包围盒中心平移到原点
    bool recenter_to_origin { true };
    /**
     * @brief 平移后按包围盒最长边统一缩放
     *
     * `<= 0` 表示不缩放；`> 0` 时最长边被缩放到该长度（世界单位）。
     */
    float uniform_scale_max_axis { 0.F };
};

/**
 * @brief glTF 加载结果：`Model` + 材质 + 其生命周期内的 GPU 资源
 *
 * @note
 * 不可复制；`model[i].primitives` 仅含偏移与材质指针；顶点/索引数据在
 * `vertex_buffer` / `index_buffer`。绘制用 `geometry()` 配合
 * `append_mesh_render_items` / `append_model_render_items`。
 */
struct GltfSceneMesh {
    lumen::scene::Model model {};
    std::vector<lumen::render::Material> materials {};
    std::unique_ptr<lumen::render::VertexBuffer> vertex_buffer {};
    std::unique_ptr<lumen::render::IndexBuffer> index_buffer {};
    std::vector<std::unique_ptr<lumen::render::Texture>> textures {};

    std::size_t stats_vertex_count { 0 };
    std::size_t stats_index_count { 0 };

    GltfSceneMesh() = default;
    GltfSceneMesh(GltfSceneMesh &&) noexcept = default;
    GltfSceneMesh &operator=(GltfSceneMesh &&) noexcept = default;
    GltfSceneMesh(const GltfSceneMesh &) = delete;
    GltfSceneMesh &operator=(const GltfSceneMesh &) = delete;

    [[nodiscard]] MeshBuffer geometry() const {
        return { vertex_buffer.get(), index_buffer.get() };
    }
};

/**
 * @brief 从 glTF / GLB 构建 `GltfSceneMesh`
 *
 * @param[in]  ctx            Vulkan 上下文
 * @param[in]  transfer_queue 用于 staging 上传的队列（通常与 `graphics_queue`
 * 相同）
 * @param[in]  cmd_pool       用于提交上传命令的池
 * @param[in]  gltf_path      文件路径（可为资源根相对路径，与
 * `get_resource_path` 一致）
 * @param[out] out            成功时写入；失败时尽力清空
 * @param[in]  opts           预处理选项
 * @param[out] error_message  可选；失败时追加简短说明（不含换行）
 *
 * @return 成功为 true
 */
bool load_gltf_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                          lumen::render::CommandPool &cmd_pool,
                          std::string_view gltf_path, GltfSceneMesh &out,
                          const GltfSceneMeshLoadOptions &opts = {},
                          std::string *error_message = nullptr);

} // namespace lumen::scene
