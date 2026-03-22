/**
 * @file light_viewport_gizmos.hpp
 * @brief 场景视口内光源 billboard 图标 + 选中光源的范围/方向调试线（Vulkan）
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vulkan/vulkan.h>

#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"

namespace lumen {
namespace render {
class Context;
class CommandPool;
} // namespace render

namespace ui {

/**
 * @brief 创建参数：SPIR-V 与 PNG 由应用提供路径（如 `get_resource_path`）
 */
struct LightViewportGizmosCreateInfo {
    const render::Context *ctx { nullptr };
    VkRenderPass scene_render_pass { VK_NULL_HANDLE };
    uint32_t subpass_index { 0 };
    render::CommandPool *cmd_pool { nullptr };
    VkQueue graphics_queue { VK_NULL_HANDLE };
    uint32_t max_frames_in_flight { 2 };
    float icon_half_extent { 0.18f };
    const char *spirv_light_icon_vert { nullptr };
    const char *spirv_light_icon_frag { nullptr };
    const char *spirv_light_debug_vert { nullptr };
    const char *spirv_light_debug_frag { nullptr };
    const char *png_directional_icon { nullptr };
    const char *png_point_icon { nullptr };
    const char *png_spot_icon { nullptr };
};

/**
 * @brief 封装光源图标与调试线资源及录制逻辑
 *
 * 须在 **与主场景相同** 的 `VkRenderPass` / subpass 内调用 `record`（深度测、混合与
 * demo 原实现一致）。
 */
class LightViewportGizmos {
public:
    /// 与 `light_debug` 顶点布局一致（pos + color）
    struct LineVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    LightViewportGizmos() = default;
    LightViewportGizmos(const LightViewportGizmos &) = delete;
    LightViewportGizmos &operator=(const LightViewportGizmos &) = delete;
    LightViewportGizmos(LightViewportGizmos &&other) noexcept;
    LightViewportGizmos &operator=(LightViewportGizmos &&other) noexcept;
    ~LightViewportGizmos();

    bool create(const LightViewportGizmosCreateInfo &info);
    void destroy();

    [[nodiscard]] bool icons_ready() const { return icons_ready_; }
    [[nodiscard]] bool debug_ready() const { return debug_ready_; }

    /**
     * @param selected_for_debug 带 `LightComponent` 的选中实体才生成范围/方向线
     */
    void prepare_frame(const ::entt::registry &registry,
                       ::entt::entity selected_for_debug,
                       bool draw_icons_for_all_lights,
                       bool draw_range_direction_for_selected,
                       uint32_t frame_index);

    /**
     * @pre 已 `vkCmdSetViewport` / `vkCmdSetScissor` 与场景一致；仍在同一 render pass 内
     */
    void record(VkCommandBuffer cmd, uint32_t frame_index,
                const glm::mat4 &view, const glm::mat4 &proj,
                const ::entt::registry &registry) const;

private:
    void destroy_();

    bool icons_ready_ { false };
    bool debug_ready_ { false };
    bool draw_icons_this_frame_ { false };
    uint32_t frames_in_flight_ { 2 };
    uint32_t line_vertex_count_ { 0 };
    float icon_half_extent_ { 0.18f };

    render::ShaderModule icon_vert_shader_;
    render::ShaderModule icon_frag_shader_;
    render::ShaderModule dbg_vert_shader_;
    render::ShaderModule dbg_frag_shader_;
    render::Texture tex_directional_;
    render::Texture tex_point_;
    render::Texture tex_spot_;
    render::VertexBuffer icon_vertex_buffer_;
    render::IndexBuffer icon_index_buffer_;
    render::DescriptorSetLayout icon_desc_layout_;
    render::DescriptorPool icon_desc_pool_;
    VkDescriptorSet icon_sets_[3] {};
    render::PipelineLayout icon_pipeline_layout_;
    render::GraphicsPipeline icon_pipeline_;
    render::PipelineLayout dbg_pipeline_layout_;
    render::GraphicsPipeline dbg_pipeline_;
    std::vector<render::VertexBuffer> line_vertex_buffers_;
    mutable std::vector<LineVertex> line_scratch_;
};

} // namespace ui
} // namespace lumen
