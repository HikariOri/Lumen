/**
 * @file light_viewport_gizmos.cpp
 */

#include "ui/light_viewport_gizmos.hpp"

#include "core/log/logger.hpp"
#include "render/context.hpp"

#include <algorithm>
#include <cstddef>

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace lumen::ui {
namespace {

using LineV = LightViewportGizmos::LineVertex;

constexpr std::size_t kLightDebugVbMaxBytes { 768U * 1024U };

struct BillboardVertex {
    glm::vec2 pos;
    glm::vec2 uv;
};

void build_selected_light_debug(const entt::registry &registry,
                                entt::entity selected,
                                std::vector<LineV> &out) {
    (void)registry;
    (void)selected;
    out.clear();
}

} // namespace

LightViewportGizmos::LightViewportGizmos(LightViewportGizmos &&other) noexcept =
    default;

LightViewportGizmos &
LightViewportGizmos::operator=(LightViewportGizmos &&other) noexcept = default;

LightViewportGizmos::~LightViewportGizmos() { destroy(); }

void LightViewportGizmos::destroy() {
    icon_pipeline_ = render::GraphicsPipeline {};
    dbg_pipeline_ = render::GraphicsPipeline {};
    icon_pipeline_layout_ = render::PipelineLayout {};
    dbg_pipeline_layout_ = render::PipelineLayout {};
    icon_desc_pool_ = render::DescriptorPool {};
    icon_desc_layout_ = render::DescriptorSetLayout {};
    icon_index_buffer_ = render::IndexBuffer {};
    icon_vertex_buffer_ = render::VertexBuffer {};
    tex_spot_ = render::Texture {};
    tex_point_ = render::Texture {};
    tex_directional_ = render::Texture {};
    dbg_frag_shader_ = render::ShaderModule {};
    dbg_vert_shader_ = render::ShaderModule {};
    icon_frag_shader_ = render::ShaderModule {};
    icon_vert_shader_ = render::ShaderModule {};
    icon_sets_[0] = nullptr;
    icon_sets_[1] = nullptr;
    icon_sets_[2] = nullptr;
    line_vertex_buffers_.clear();
    line_scratch_.clear();
    icons_ready_ = false;
    debug_ready_ = false;
    draw_icons_this_frame_ = false;
    line_vertex_count_ = 0;
    frames_in_flight_ = 2;
}

bool LightViewportGizmos::create(const LightViewportGizmosCreateInfo &info) {
    destroy();

    if (!info.ctx || !info.scene_render_pass || !info.cmd_pool ||
        !info.graphics_queue || !info.spirv_light_icon_vert ||
        !info.spirv_light_icon_frag || !info.spirv_light_debug_vert ||
        !info.spirv_light_debug_frag || !info.png_directional_icon ||
        !info.png_point_icon || !info.png_spot_icon) {
        LUMEN_LOG_WARN("LightViewportGizmos::create: 缺少必要参数");
        return false;
    }

    frames_in_flight_ = std::max(1u, std::min(8u, info.max_frames_in_flight));
    icon_half_extent_ = info.icon_half_extent;
    if (icon_half_extent_ < 1e-4f) {
        icon_half_extent_ = 0.18f;
    }

    const render::Context &ctx = *info.ctx;

    if (!icon_vert_shader_.create_from_file(ctx.device(),
                                            info.spirv_light_icon_vert) ||
        !icon_frag_shader_.create_from_file(ctx.device(),
                                            info.spirv_light_icon_frag) ||
        !dbg_vert_shader_.create_from_file(ctx.device(),
                                           info.spirv_light_debug_vert) ||
        !dbg_frag_shader_.create_from_file(ctx.device(),
                                           info.spirv_light_debug_frag)) {
        LUMEN_LOG_WARN("LightViewportGizmos: 着色器模块加载失败");
        destroy();
        return false;
    }

    if (!tex_directional_.create_from_file(ctx, info.png_directional_icon,
                                           info.graphics_queue,
                                           *info.cmd_pool) ||
        !tex_point_.create_from_file(ctx, info.png_point_icon,
                                     info.graphics_queue, *info.cmd_pool) ||
        !tex_spot_.create_from_file(ctx, info.png_spot_icon,
                                    info.graphics_queue, *info.cmd_pool)) {
        LUMEN_LOG_WARN("LightViewportGizmos: 光源图标纹理加载失败");
        destroy();
        return false;
    }

    const BillboardVertex k_quad[] = {
        { { -1.0f, -1.0f }, { 0.0f, 1.0f } },
        { { 1.0f, -1.0f }, { 1.0f, 1.0f } },
        { { 1.0f, 1.0f }, { 1.0f, 0.0f } },
        { { -1.0f, 1.0f }, { 0.0f, 0.0f } },
    };
    const uint32_t k_quad_idx[] = { 0u, 1u, 2u, 0u, 2u, 3u };
    if (!icon_vertex_buffer_.create_device_local_and_upload(
            ctx, info.graphics_queue, *info.cmd_pool, k_quad, sizeof(k_quad)) ||
        !icon_index_buffer_.create_device_local_and_upload(
            ctx, info.graphics_queue, *info.cmd_pool, k_quad_idx,
            sizeof(k_quad_idx))) {
        destroy();
        return false;
    }
    icon_index_buffer_.set_index_type(render::IndexBuffer::IndexType::Uint32);

    if (!icon_desc_layout_.create(
            ctx, { { 0, vk::DescriptorType::eCombinedImageSampler, 1,
                     vk::ShaderStageFlagBits::eFragment } })) {
        destroy();
        return false;
    }

    if (!icon_pipeline_layout_.create(
            ctx, { icon_desc_layout_.handle() },
            { vk::PushConstantRange { vk::ShaderStageFlagBits::eVertex, 0,
                                      sizeof(glm::mat4) } })) {
        destroy();
        return false;
    }

    if (!icon_desc_pool_.create(
            ctx, { { vk::DescriptorType::eCombinedImageSampler, 3 } }, 3)) {
        destroy();
        return false;
    }
    for (auto & icon_set : icon_sets_) {
        if (!icon_desc_pool_.allocate(ctx.device(), icon_desc_layout_.handle(),
                                      icon_set)) {
            destroy();
            return false;
        }
    }
    render::write_descriptor_image(ctx.device(), icon_sets_[0], 0,
                                   tex_directional_.view(),
                                   tex_directional_.sampler());
    render::write_descriptor_image(ctx.device(), icon_sets_[1], 0,
                                   tex_point_.view(), tex_point_.sampler());
    render::write_descriptor_image(ctx.device(), icon_sets_[2], 0,
                                   tex_spot_.view(), tex_spot_.sampler());

    render::GraphicsPipelineConfig icon_cfg {};
    icon_cfg.shaderStages.push_back({ icon_vert_shader_.handle(),
                                      vk::ShaderStageFlagBits::eVertex,
                                      "main" });
    icon_cfg.shaderStages.push_back({ icon_frag_shader_.handle(),
                                      vk::ShaderStageFlagBits::eFragment,
                                      "main" });
    icon_cfg.vertexBindings.push_back(
        { 0, sizeof(BillboardVertex), vk::VertexInputRate::eVertex });
    icon_cfg.vertexAttributes.push_back(
        { 0, 0, vk::Format::eR32G32Sfloat, offsetof(BillboardVertex, pos) });
    icon_cfg.vertexAttributes.push_back(
        { 1, 0, vk::Format::eR32G32Sfloat, offsetof(BillboardVertex, uv) });
    icon_cfg.depthTest = true;
    icon_cfg.depthWrite = false;
    icon_cfg.depthCompareOp = vk::CompareOp::eLess;
    icon_cfg.cullMode = vk::CullModeFlagBits::eNone;
    icon_cfg.frontFace = vk::FrontFace::eCounterClockwise;
    icon_cfg.alphaBlend = true;
    if (!icon_pipeline_.create(ctx, icon_pipeline_layout_.handle(),
                               info.scene_render_pass, info.subpass_index,
                               icon_cfg)) {
        LUMEN_LOG_WARN("LightViewportGizmos: 图标管线创建失败");
        destroy();
        return false;
    }
    icons_ready_ = true;

    if (!dbg_pipeline_layout_.create(
            ctx, {},
            { vk::PushConstantRange { vk::ShaderStageFlagBits::eVertex, 0,
                                      sizeof(glm::mat4) } })) {
        destroy();
        return false;
    }

    render::GraphicsPipelineConfig dbg_cfg {};
    dbg_cfg.shaderStages.push_back({ dbg_vert_shader_.handle(),
                                     vk::ShaderStageFlagBits::eVertex,
                                     "main" });
    dbg_cfg.shaderStages.push_back({ dbg_frag_shader_.handle(),
                                     vk::ShaderStageFlagBits::eFragment,
                                     "main" });
    dbg_cfg.topology = vk::PrimitiveTopology::eLineList;
    dbg_cfg.vertexBindings.push_back(
        { 0, sizeof(LineV), vk::VertexInputRate::eVertex });
    dbg_cfg.vertexAttributes.push_back(
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(LineV, position) });
    dbg_cfg.vertexAttributes.push_back(
        { 1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(LineV, color) });
    dbg_cfg.depthTest = true;
    dbg_cfg.depthWrite = false;
    dbg_cfg.depthCompareOp = vk::CompareOp::eLess;
    dbg_cfg.cullMode = vk::CullModeFlagBits::eNone;
    dbg_cfg.frontFace = vk::FrontFace::eCounterClockwise;
    dbg_cfg.alphaBlend = true;
    if (!dbg_pipeline_.create(ctx, dbg_pipeline_layout_.handle(),
                              info.scene_render_pass, info.subpass_index,
                              dbg_cfg)) {
        LUMEN_LOG_WARN("LightViewportGizmos: 调试线管线创建失败");
        destroy();
        return false;
    }
    debug_ready_ = true;

    line_vertex_buffers_.resize(frames_in_flight_);
    for (uint32_t fi = 0; fi < frames_in_flight_; ++fi) {
        if (!line_vertex_buffers_[fi].create(ctx, kLightDebugVbMaxBytes)) {
            LUMEN_LOG_ERROR("LightViewportGizmos: 调试顶点缓冲创建失败");
            destroy();
            return false;
        }
    }

    return true;
}

void LightViewportGizmos::prepare_frame(const entt::registry &registry,
                                        entt::entity selected_for_debug,
                                        bool draw_icons_for_all_lights,
                                        bool draw_range_direction_for_selected,
                                        uint32_t frame_index) {
    draw_icons_this_frame_ = draw_icons_for_all_lights && icons_ready_;
    line_vertex_count_ = 0;

    if (!draw_range_direction_for_selected || !debug_ready_ ||
        frame_index >= line_vertex_buffers_.size()) {
        return;
    }

    build_selected_light_debug(registry, selected_for_debug, line_scratch_);
    const size_t bytes = line_scratch_.size() * sizeof(LineV);
    if (bytes == 0 || bytes > line_vertex_buffers_[frame_index].size()) {
        return;
    }
    line_vertex_buffers_[frame_index].upload(line_scratch_.data(), bytes);
    line_vertex_count_ = static_cast<uint32_t>(line_scratch_.size());
}

void LightViewportGizmos::record(vk::CommandBuffer cmd, uint32_t frame_index,
                                 const glm::mat4 &view, const glm::mat4 &proj,
                                 const entt::registry &registry) const {
    if (draw_icons_this_frame_) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         icon_pipeline_.handle());
        const vk::Buffer lib_vb = icon_vertex_buffer_.handle();
        const vk::DeviceSize off0 { 0 };
        cmd.bindVertexBuffers(0, { lib_vb }, { off0 });
        cmd.bindIndexBuffer(icon_index_buffer_.handle(), vk::DeviceSize { 0 },
                            icon_index_buffer_.vk_index_type());
        (void)registry;
    }

    if (debug_ready_ && line_vertex_count_ > 0 &&
        frame_index < line_vertex_buffers_.size()) {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         dbg_pipeline_.handle());
        const glm::mat4 vp = proj * view;
        cmd.pushConstants(dbg_pipeline_layout_.handle(),
                          vk::ShaderStageFlagBits::eVertex, 0,
                          sizeof(glm::mat4), glm::value_ptr(vp));
        const vk::Buffer ldb = line_vertex_buffers_[frame_index].handle();
        const vk::DeviceSize z { 0 };
        cmd.bindVertexBuffers(0, { ldb }, { z });
        cmd.draw(line_vertex_count_, 1, 0, 0);
    }
}

} // namespace lumen::ui
