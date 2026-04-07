#pragma once

#include "rhi/device.hpp"
#include "rhi/shader_reflection.hpp"
#include "rhi/vulkan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rhi {

class DescriptorSetLayoutCache;
class Device;
class GraphicsPipelineCache;
class PipelineLayoutCache;
class RenderGraph;
class Swapchain;
struct RgGpuExecuteFrame;

/// 单 Pass 在 `compile(..., RgGpuCompileContext*)` 之后可用的呈现/图形资源（管线、布局、描述符、swapchain framebuffer）。
struct RgGpuCompiledPass {
    vk::RenderPass render_pass {};
    vk::PipelineLayout pipeline_layout {};
    vk::Pipeline pipeline {};
    vk::DescriptorPool descriptor_pool {};
    /// 与管线布局 set 编号一致：`descriptor_sets_by_set[s]` 对应 `create_reflected_pipeline_layouts` 的第 `s` 套。
    std::vector<vk::DescriptorSet> descriptor_sets_by_set {};
    std::vector<vk::Framebuffer> framebuffers;
    ShaderReflection merged_reflection {};
};

struct RgGpuPassBuildCache {
    std::uint64_t vert_hash {};
    std::uint64_t frag_hash {};
    int swap_format {};
    vk::Extent2D extent {};
    std::uint32_t image_count {};
    std::vector<std::uintptr_t> uniform_vk_buffers;
    bool pipeline_valid { false };
};

/// 与 `RenderGraph::compile(&ctx)` 配合：在 barrier 编译成功后按 Pass 的 `RgPass::gpu` 创建或增量更新 Vulkan 资源。
struct RgGpuCompileContext {
    Device *rhi_device {};
    vk::Device vk_device {};
    const Swapchain *swapchain {};
    DescriptorSetLayoutCache *dsl_cache {};
    PipelineLayoutCache *pl_cache {};

    std::vector<std::optional<RgGpuCompiledPass>> compiled;
    std::vector<RgGpuPassBuildCache> pass_caches;

    void destroy_all(vk::Device dev, GraphicsPipelineCache *gfx_cache);
};

void destroy_rg_gpu_compiled_pass(vk::Device dev, RgGpuCompiledPass &p,
                                  GraphicsPipelineCache *gfx_cache);

[[nodiscard]] bool compile_render_graph_gpu_phase(RenderGraph &rg,
                                                  RgGpuCompileContext &ctx);

class CommandBuffer;

/// 内置录制：swapchain 单颜色附件 + 可选 `draw` / `drawIndexed`
/// （`RgPassGpuDesc::draw_vertex_count` 或 `draw_index_count`）。
[[nodiscard]] bool rg_record_builtin_present_graphics_pass(
    const RenderGraph &rg, std::size_t pass_index, CommandBuffer &cmd,
    const RgGpuCompiledPass &compiled, const RgGpuExecuteFrame &frame);

} // namespace rhi
