#include "rhi/pipeline_key.hpp"

#include <bit>
#include <utility>

namespace rhi {

namespace {

[[nodiscard]] std::size_t hash_u64(std::size_t h, const std::uint64_t v) noexcept {
    return h ^ (static_cast<std::size_t>(v) + 0x9e3779b97f4a7c15ULL +
                (h << 6) + (h >> 2));
}

template <typename Enum>
[[nodiscard]] std::size_t hash_enum(std::size_t h, const Enum e) noexcept {
    return hash_u64(h,
                    static_cast<std::uint64_t>(std::to_underlying(e)));
}

} // namespace

bool GraphicsPipelineKey::operator==(const GraphicsPipelineKey &o) const noexcept {
    return vert_spv_hash == o.vert_spv_hash && frag_spv_hash == o.frag_spv_hash &&
           pipeline_layout == o.pipeline_layout && render_pass == o.render_pass &&
           subpass == o.subpass && vertex_bindings == o.vertex_bindings &&
           vertex_attributes == o.vertex_attributes && topology == o.topology &&
           dynamic_viewport_scissor == o.dynamic_viewport_scissor &&
           polygon_mode == o.polygon_mode && cull_mode == o.cull_mode &&
           front_face == o.front_face &&
           line_width == o.line_width &&
           rasterization_samples == o.rasterization_samples &&
           depth_stencil == o.depth_stencil &&
           color_blend_attachments == o.color_blend_attachments &&
           color_logic_op_enable == o.color_logic_op_enable &&
           logic_op == o.logic_op && dynamic_states == o.dynamic_states;
}

std::size_t
GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey &k) const noexcept {
    std::size_t h = 0;
    h = hash_u64(h, k.vert_spv_hash);
    h = hash_u64(h, k.frag_spv_hash);
    h = hash_u64(h, k.pipeline_layout);
    h = hash_u64(h, k.render_pass);
    h = hash_u64(h, k.subpass);
    for (const auto &b : k.vertex_bindings) {
        h = hash_u64(h, b.binding);
        h = hash_u64(h, b.stride);
        h = hash_enum(h, b.inputRate);
    }
    for (const auto &a : k.vertex_attributes) {
        h = hash_u64(h, a.location);
        h = hash_u64(h, a.binding);
        h = hash_enum(h, a.format);
        h = hash_u64(h, a.offset);
    }
    h = hash_enum(h, k.topology);
    h = hash_u64(h, k.dynamic_viewport_scissor ? 1u : 0u);
    h = hash_enum(h, k.polygon_mode);
    h = hash_u64(h, static_cast<std::uint64_t>(
                       static_cast<std::uint32_t>(
                           static_cast<VkCullModeFlags>(k.cull_mode))));
    h = hash_enum(h, k.front_face);
    h = hash_u64(h, std::bit_cast<std::uint32_t>(k.line_width));
    h = hash_enum(h, k.rasterization_samples);
    if (k.depth_stencil.has_value()) {
        const DepthStencilPipelineKey &d = *k.depth_stencil;
        h = hash_u64(h, 1u);
        h = hash_u64(h, d.depth_test ? 1u : 0u);
        h = hash_u64(h, d.depth_write ? 1u : 0u);
        h = hash_enum(h, d.depth_compare);
        h = hash_u64(h, d.depth_bounds_test ? 1u : 0u);
        h = hash_u64(h, std::bit_cast<std::uint32_t>(d.min_depth_bounds));
        h = hash_u64(h, std::bit_cast<std::uint32_t>(d.max_depth_bounds));
        h = hash_u64(h, d.stencil_test ? 1u : 0u);
    } else {
        h = hash_u64(h, 0u);
    }
    for (const auto &c : k.color_blend_attachments) {
        h = hash_u64(h, c.blend_enable ? 1u : 0u);
        h = hash_enum(h, c.src_color);
        h = hash_enum(h, c.dst_color);
        h = hash_enum(h, c.color_op);
        h = hash_enum(h, c.src_alpha);
        h = hash_enum(h, c.dst_alpha);
        h = hash_enum(h, c.alpha_op);
        h = hash_u64(h, static_cast<std::uint64_t>(
                           static_cast<std::uint32_t>(
                               static_cast<VkColorComponentFlags>(
                                   c.color_write_mask))));
    }
    h = hash_u64(h, k.color_logic_op_enable ? 1u : 0u);
    h = hash_enum(h, k.logic_op);
    for (const vk::DynamicState ds : k.dynamic_states) {
        h = hash_enum(h, ds);
    }
    return h;
}

std::size_t
ComputePipelineKeyHash::operator()(const ComputePipelineKey &k) const noexcept {
    std::size_t h = 0;
    h = hash_u64(h, k.compute_spv_hash);
    h = hash_u64(h, k.pipeline_layout);
    return h;
}

} // namespace rhi
