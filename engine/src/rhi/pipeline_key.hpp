#pragma once

#include "rhi/vulkan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rhi {

/// 深度/模板状态；若 `GraphicsPipelineKey::depth_stencil` 为空则对应
/// `pDepthStencilState == nullptr`。
struct DepthStencilPipelineKey {
    bool depth_test { false };
    bool depth_write { false };
    vk::CompareOp depth_compare { vk::CompareOp::eLess };
    bool depth_bounds_test { false };
    float min_depth_bounds { 0.F };
    float max_depth_bounds { 1.F };
    bool stencil_test { false };

    [[nodiscard]] bool operator==(const DepthStencilPipelineKey &) const noexcept =
        default;
};

struct ColorBlendAttachmentKey {
    bool blend_enable { false };
    vk::BlendFactor src_color { vk::BlendFactor::eOne };
    vk::BlendFactor dst_color { vk::BlendFactor::eZero };
    vk::BlendOp color_op { vk::BlendOp::eAdd };
    vk::BlendFactor src_alpha { vk::BlendFactor::eOne };
    vk::BlendFactor dst_alpha { vk::BlendFactor::eZero };
    vk::BlendOp alpha_op { vk::BlendOp::eAdd };
    vk::ColorComponentFlags color_write_mask {};

    [[nodiscard]] bool operator==(const ColorBlendAttachmentKey &) const noexcept =
        default;
};

/// Graphics pipeline 的完整 DNA（与 `vk::GraphicsPipelineCreateInfo` 可复现部分对应）。
struct GraphicsPipelineKey {
    std::uint64_t vert_spv_hash {};
    std::uint64_t frag_spv_hash {};
    std::uint64_t pipeline_layout {};
    std::uint64_t render_pass {};
    std::uint32_t subpass {};

    std::vector<vk::VertexInputBindingDescription> vertex_bindings;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;

    vk::PrimitiveTopology topology { vk::PrimitiveTopology::eTriangleList };
    bool dynamic_viewport_scissor { true };

    vk::PolygonMode polygon_mode { vk::PolygonMode::eFill };
    vk::CullModeFlags cull_mode { vk::CullModeFlagBits::eNone };
    vk::FrontFace front_face { vk::FrontFace::eCounterClockwise };
    float line_width { 1.F };

    vk::SampleCountFlagBits rasterization_samples {
        vk::SampleCountFlagBits::e1
    };

    std::optional<DepthStencilPipelineKey> depth_stencil;

    std::vector<ColorBlendAttachmentKey> color_blend_attachments;
    bool color_logic_op_enable { false };
    vk::LogicOp logic_op { vk::LogicOp::eCopy };

    /// 须规范化（例如升序）以保证 `operator==` / 哈希稳定。
    std::vector<vk::DynamicState> dynamic_states;

    [[nodiscard]] bool operator==(const GraphicsPipelineKey &) const noexcept;
};

struct GraphicsPipelineKeyHash {
    [[nodiscard]] std::size_t
    operator()(const GraphicsPipelineKey &k) const noexcept;
};

struct ComputePipelineKey {
    std::uint64_t compute_spv_hash {};
    std::uint64_t pipeline_layout {};

    [[nodiscard]] bool operator==(const ComputePipelineKey &) const noexcept =
        default;
};

struct ComputePipelineKeyHash {
    [[nodiscard]] std::size_t
    operator()(const ComputePipelineKey &k) const noexcept;
};

} // namespace rhi
