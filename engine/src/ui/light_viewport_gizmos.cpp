/**
 * @file light_viewport_gizmos.cpp
 */

#include "ui/light_viewport_gizmos.hpp"

#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "scene/components.hpp"
#include "scene/transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace lumen::ui {
namespace {

using LineV = LightViewportGizmos::LineVertex;

constexpr std::size_t kLightDebugVbMaxBytes { 768U * 1024U };
constexpr int kLightDebugCircleSegments { 28 };
constexpr float kLightDebugDirBeamLength { 3.8f };
constexpr float kLightDebugMaxHalfAngleRad { 1.553343f };

struct BillboardVertex {
    glm::vec2 pos;
    glm::vec2 uv;
};

[[nodiscard]] glm::mat4 billboard_mvp(const glm::mat4 &view, const glm::mat4 &proj,
                                      const glm::vec3 &world_pos,
                                      float half_extent) {
    const glm::mat4 inv_view = glm::inverse(view);
    const glm::vec3 cam_pos = glm::vec3(inv_view[3]);
    glm::vec3 n = cam_pos - world_pos;
    if (glm::length(n) < 1e-5f) {
        n = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        n = glm::normalize(n);
    }
    const glm::vec3 up_ref = std::abs(n.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                   : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up_ref, n));
    const glm::vec3 up = glm::normalize(glm::cross(n, right));
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * half_extent, 0.0f);
    model[1] = glm::vec4(up * half_extent, 0.0f);
    model[2] = glm::vec4(n, 0.0f);
    model[3] = glm::vec4(world_pos, 1.0f);
    return proj * view * model;
}

inline glm::vec3 normalize_safe(glm::vec3 v, glm::vec3 fallback) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : fallback;
}

inline glm::vec4 tint_axis_color(glm::vec4 base, glm::vec3 light_rgb) {
    glm::vec4 c = base;
    c.r = std::min(c.r * light_rgb.r, 1.0f);
    c.g = std::min(c.g * light_rgb.g, 1.0f);
    c.b = std::min(c.b * light_rgb.b, 1.0f);
    return c;
}

void append_line(std::vector<LineV> &out, glm::vec3 a, glm::vec3 b,
                 glm::vec4 color) {
    out.push_back({ a, color });
    out.push_back({ b, color });
}

void circle_in_plane(std::vector<LineV> &out, glm::vec3 center,
                     glm::vec3 plane_axis, float radius, glm::vec4 color, int n) {
    if (radius < 1e-4f || n < 3) {
        return;
    }
    plane_axis = normalize_safe(plane_axis, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 ref = std::abs(plane_axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                        : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 u =
        normalize_safe(glm::cross(ref, plane_axis), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 v = glm::cross(plane_axis, u);
    constexpr float k_two_pi { 6.28318530718f };
    for (int i = 0; i < n; ++i) {
        const float a0 = k_two_pi * static_cast<float>(i) / static_cast<float>(n);
        const float a1 =
            k_two_pi * static_cast<float>(i + 1) / static_cast<float>(n);
        const glm::vec3 p0 =
            center + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        const glm::vec3 p1 =
            center + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        append_line(out, p0, p1, color);
    }
}

void wire_sphere(std::vector<LineV> &out, glm::vec3 center, float radius,
                 glm::vec4 color) {
    if (radius < 1e-4f) {
        return;
    }
    circle_in_plane(out, center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color,
                    kLightDebugCircleSegments);
    circle_in_plane(out, center, glm::vec3(0.0f, 1.0f, 0.0f), radius, color,
                    kLightDebugCircleSegments);
    circle_in_plane(out, center, glm::vec3(1.0f, 0.0f, 0.0f), radius, color,
                    kLightDebugCircleSegments);
}

void arrow(std::vector<LineV> &out, glm::vec3 tail, glm::vec3 dir_unit,
           float length, glm::vec4 color) {
    if (length < 1e-4f) {
        return;
    }
    const glm::vec3 tip = tail + dir_unit * length;
    append_line(out, tail, tip, color);
    const float head = std::min(length * 0.18f, 0.35f);
    const glm::vec3 back = tip - dir_unit * head;
    const glm::vec3 ref = std::abs(dir_unit.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                      : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 side0 =
        normalize_safe(glm::cross(ref, dir_unit), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 side1 = glm::cross(dir_unit, side0);
    constexpr float k_two_pi { 6.28318530718f };
    const float spread = head * 0.55f;
    for (int k = 0; k < 3; ++k) {
        const float ang = k_two_pi * static_cast<float>(k) / 3.0f;
        const glm::vec3 wing =
            back + (side0 * std::cos(ang) + side1 * std::sin(ang)) * spread;
        append_line(out, tip, wing, color);
    }
}

void spot_cone(std::vector<LineV> &out, glm::vec3 apex, glm::vec3 emit_axis_unit,
               float range, float inner_half, float outer_half,
               glm::vec3 light_rgb) {
    range = std::max(range, 1e-3f);
    emit_axis_unit =
        normalize_safe(emit_axis_unit, glm::vec3(0.0f, 0.0f, -1.0f));
    float inner_a = std::min(inner_half, outer_half);
    float outer_a = std::max(inner_half, outer_half);
    outer_a = std::min(outer_a, kLightDebugMaxHalfAngleRad);
    inner_a = std::min(inner_a, outer_a);
    const glm::vec3 base_c = apex + emit_axis_unit * range;
    const float r_out = range * std::tan(outer_a);
    const float r_in = range * std::tan(inner_a);
    const glm::vec4 col_out =
        tint_axis_color({ 1.0f, 0.52f, 0.12f, 0.88f }, light_rgb);
    const glm::vec4 col_in =
        tint_axis_color({ 1.0f, 0.88f, 0.35f, 0.55f }, light_rgb);
    const glm::vec3 ref = std::abs(emit_axis_unit.y) < 0.9f
                              ? glm::vec3(0.0f, 1.0f, 0.0f)
                              : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 u = normalize_safe(glm::cross(ref, emit_axis_unit),
                                 glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 v = glm::cross(emit_axis_unit, u);
    constexpr float k_two_pi { 6.28318530718f };
    const int n = kLightDebugCircleSegments;
    for (int i = 0; i < n; ++i) {
        const float a0 = k_two_pi * static_cast<float>(i) / static_cast<float>(n);
        const float a1 =
            k_two_pi * static_cast<float>(i + 1) / static_cast<float>(n);
        for (int pass = 0; pass < 2; ++pass) {
            const float rr = pass == 0 ? r_out : r_in;
            const glm::vec4 cc = pass == 0 ? col_out : col_in;
            if (rr < 1e-4f) {
                continue;
            }
            const glm::vec3 p0 =
                base_c + (u * std::cos(a0) + v * std::sin(a0)) * rr;
            const glm::vec3 p1 =
                base_c + (u * std::cos(a1) + v * std::sin(a1)) * rr;
            append_line(out, p0, p1, cc);
        }
    }
    for (int k = 0; k < 4; ++k) {
        const float ang = k_two_pi * static_cast<float>(k) / 4.0f;
        const glm::vec3 p_base =
            base_c + (u * std::cos(ang) + v * std::sin(ang)) * r_out;
        append_line(out, apex, p_base, col_out);
    }
}

void build_selected_light_debug(const ::entt::registry &registry,
                                ::entt::entity selected,
                                std::vector<LineV> &out) {
    out.clear();
    if (selected == ::entt::null || !registry.valid(selected) ||
        !registry.all_of<lumen::scene::LightComponent>(selected)) {
        return;
    }
    out.reserve(2048);
    const auto &light = registry.get<lumen::scene::LightComponent>(selected);
    const glm::mat4 world = lumen::scene::world_matrix(registry, selected);
    const glm::vec3 wp = glm::vec3(world[3]);
    const glm::mat3 lin = glm::mat3(world);
    switch (light.type) {
    case lumen::scene::LightType::Directional: {
        const glm::vec3 surf_to_light = normalize_safe(
            lin * light.local_direction, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 emit_dir = -surf_to_light;
        const glm::vec4 c =
            tint_axis_color({ 1.0f, 0.92f, 0.28f, 0.92f }, light.color);
        arrow(out, wp, emit_dir, kLightDebugDirBeamLength, c);
        break;
    }
    case lumen::scene::LightType::Point: {
        const float R = std::max(light.range, 1e-2f);
        const glm::vec4 c =
            tint_axis_color({ 0.28f, 0.72f, 1.0f, 0.78f }, light.color);
        wire_sphere(out, wp, R, c);
        break;
    }
    case lumen::scene::LightType::Spot: {
        const glm::vec3 emit_axis =
            normalize_safe(lin * light.local_direction,
                           glm::vec3(0.0f, 0.0f, -1.0f));
        const float range = std::max(light.range, 1e-2f);
        spot_cone(out, wp, emit_axis, range, light.inner_radians,
                  light.outer_radians, light.color);
        break;
    }
    }
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
    icon_sets_[0] = VK_NULL_HANDLE;
    icon_sets_[1] = VK_NULL_HANDLE;
    icon_sets_[2] = VK_NULL_HANDLE;
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

    frames_in_flight_ =
        std::max(1u, std::min(8u, info.max_frames_in_flight));
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

    if (!tex_directional_.create_from_file(
            ctx, info.png_directional_icon, info.graphics_queue, *info.cmd_pool) ||
        !tex_point_.create_from_file(ctx, info.png_point_icon,
                                      info.graphics_queue, *info.cmd_pool) ||
        !tex_spot_.create_from_file(ctx, info.png_spot_icon, info.graphics_queue,
                                    *info.cmd_pool)) {
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
    if (!icon_vertex_buffer_.create(ctx, sizeof(k_quad)) ||
        !icon_index_buffer_.create(ctx, sizeof(k_quad_idx))) {
        destroy();
        return false;
    }
    icon_vertex_buffer_.upload(k_quad, sizeof(k_quad));
    icon_index_buffer_.set_index_type(render::IndexBuffer::IndexType::Uint32);
    icon_index_buffer_.upload(k_quad_idx, sizeof(k_quad_idx));

    if (!icon_desc_layout_.create(
            ctx, { { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        destroy();
        return false;
    }

    if (!icon_pipeline_layout_.create(ctx, { icon_desc_layout_.handle() },
                                        { VkPushConstantRange {
                                              VK_SHADER_STAGE_VERTEX_BIT, 0,
                                              sizeof(glm::mat4) } })) {
        destroy();
        return false;
    }

    if (!icon_desc_pool_.create(
            ctx, { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 } }, 3)) {
        destroy();
        return false;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (!icon_desc_pool_.allocate(ctx.device(), icon_desc_layout_.handle(),
                                      icon_sets_[i])) {
            destroy();
            return false;
        }
    }
    render::write_descriptor_image(
        ctx.device(), icon_sets_[0], 0, tex_directional_.view(),
        tex_directional_.sampler());
    render::write_descriptor_image(ctx.device(), icon_sets_[1], 0,
                                   tex_point_.view(), tex_point_.sampler());
    render::write_descriptor_image(ctx.device(), icon_sets_[2], 0,
                                   tex_spot_.view(), tex_spot_.sampler());

    render::GraphicsPipelineConfig icon_cfg {};
    icon_cfg.shaderStages.push_back(
        { icon_vert_shader_.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    icon_cfg.shaderStages.push_back(
        { icon_frag_shader_.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    icon_cfg.vertexBindings.push_back(
        { 0, sizeof(BillboardVertex), render::VertexInputRate::PerVertex });
    icon_cfg.vertexAttributes.push_back(
        { 0, 0, render::VertexAttributeKind::F32Vec2,
          offsetof(BillboardVertex, pos) });
    icon_cfg.vertexAttributes.push_back(
        { 1, 0, render::VertexAttributeKind::F32Vec2,
          offsetof(BillboardVertex, uv) });
    icon_cfg.depthTest = true;
    icon_cfg.depthWrite = false;
    icon_cfg.depthCompareOp = VK_COMPARE_OP_LESS;
    icon_cfg.cullMode = VK_CULL_MODE_NONE;
    icon_cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
            { VkPushConstantRange { VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof(glm::mat4) } })) {
        destroy();
        return false;
    }

    render::GraphicsPipelineConfig dbg_cfg {};
    dbg_cfg.shaderStages.push_back(
        { dbg_vert_shader_.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    dbg_cfg.shaderStages.push_back(
        { dbg_frag_shader_.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    dbg_cfg.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    dbg_cfg.vertexBindings.push_back(
        { 0, sizeof(LineV), render::VertexInputRate::PerVertex });
    dbg_cfg.vertexAttributes.push_back(
        { 0, 0, render::VertexAttributeKind::F32Vec3,
          offsetof(LineV, position) });
    dbg_cfg.vertexAttributes.push_back(
        { 1, 0, render::VertexAttributeKind::F32Vec4,
          offsetof(LineV, color) });
    dbg_cfg.depthTest = true;
    dbg_cfg.depthWrite = false;
    dbg_cfg.depthCompareOp = VK_COMPARE_OP_LESS;
    dbg_cfg.cullMode = VK_CULL_MODE_NONE;
    dbg_cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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

void LightViewportGizmos::prepare_frame(const ::entt::registry &registry,
                                        ::entt::entity selected_for_debug,
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

void LightViewportGizmos::record(VkCommandBuffer cmd, uint32_t frame_index,
                                 const glm::mat4 &view, const glm::mat4 &proj,
                                 const ::entt::registry &registry) const {
    if (draw_icons_this_frame_) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          icon_pipeline_.handle());
        VkBuffer lib_vb = icon_vertex_buffer_.handle();
        VkDeviceSize off0 { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &lib_vb, &off0);
        vkCmdBindIndexBuffer(cmd, icon_index_buffer_.handle(), 0,
                             icon_index_buffer_.vk_index_type());
        for (const ::entt::entity le :
             registry.view<lumen::scene::LightComponent>()) {
            const auto &lc = registry.get<lumen::scene::LightComponent>(le);
            const glm::mat4 world = lumen::scene::world_matrix(registry, le);
            const glm::vec3 wp = glm::vec3(world[3]);
            const glm::mat4 mvp =
                billboard_mvp(view, proj, wp, icon_half_extent_);
            std::uint32_t set_i = 0;
            switch (lc.type) {
            case lumen::scene::LightType::Directional:
                set_i = 0;
                break;
            case lumen::scene::LightType::Point:
                set_i = 1;
                break;
            case lumen::scene::LightType::Spot:
                set_i = 2;
                break;
            }
            VkDescriptorSet ds = icon_sets_[set_i];
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    icon_pipeline_layout_.handle(), 0, 1, &ds,
                                    0, nullptr);
            vkCmdPushConstants(cmd, icon_pipeline_layout_.handle(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                               glm::value_ptr(mvp));
            vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
        }
    }

    if (debug_ready_ && line_vertex_count_ > 0 &&
        frame_index < line_vertex_buffers_.size()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          dbg_pipeline_.handle());
        const glm::mat4 vp = proj * view;
        vkCmdPushConstants(cmd, dbg_pipeline_layout_.handle(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                           glm::value_ptr(vp));
        VkBuffer ldb = line_vertex_buffers_[frame_index].handle();
        VkDeviceSize z { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &ldb, &z);
        vkCmdDraw(cmd, line_vertex_count_, 1, 0, 0);
    }
}

} // namespace lumen::ui
