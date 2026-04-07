#pragma once

#include "rhi/pipeline_key.hpp"
#include "rhi/shader_module_cache.hpp"
#include "rhi/vulkan.hpp"

#include <cstddef>
#include <span>
#include <unordered_map>

namespace rhi {

/// 运行时 `vk::GraphicsPipeline` 缓存；`createGraphicsPipelines` 使用驱动级
/// `vk::PipelineCache`。
class GraphicsPipelineCache {
public:
    GraphicsPipelineCache() = default;
    GraphicsPipelineCache(const GraphicsPipelineCache &) = delete;
    GraphicsPipelineCache &operator=(const GraphicsPipelineCache &) = delete;
    GraphicsPipelineCache(GraphicsPipelineCache &&) = delete;
    GraphicsPipelineCache &operator=(GraphicsPipelineCache &&) = delete;
    ~GraphicsPipelineCache() = default;

    /// `vert_spv` / `frag_spv` 须与 `key` 内哈希一致。
    [[nodiscard]] vk::Pipeline
    get_or_create(vk::Device device, vk::PipelineCache pipeline_cache,
                  ShaderModuleCache &shader_modules, const GraphicsPipelineKey &key,
                  std::span<const std::byte> vert_spv,
                  std::span<const std::byte> frag_spv);

    void clear(vk::Device device);

    /// 按句柄移除并 `destroyPipeline`（用于 teardown 时 RenderPass 即将销毁）。
    void erase_pipeline(vk::Device device, vk::Pipeline pipeline);

private:
    std::unordered_map<GraphicsPipelineKey, vk::Pipeline, GraphicsPipelineKeyHash>
        pipelines_;
};

} // namespace rhi
