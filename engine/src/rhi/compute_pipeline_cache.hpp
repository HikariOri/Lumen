#pragma once

#include "rhi/pipeline_key.hpp"
#include "rhi/shader_module_cache.hpp"
#include "rhi/vulkan.hpp"

#include <span>
#include <unordered_map>

namespace rhi {

class ComputePipelineCache {
public:
    ComputePipelineCache() = default;
    ComputePipelineCache(const ComputePipelineCache &) = delete;
    ComputePipelineCache &operator=(const ComputePipelineCache &) = delete;
    ComputePipelineCache(ComputePipelineCache &&) = delete;
    ComputePipelineCache &operator=(ComputePipelineCache &&) = delete;
    ~ComputePipelineCache() = default;

    [[nodiscard]] vk::Pipeline
    get_or_create(vk::Device device, vk::PipelineCache pipeline_cache,
                  ShaderModuleCache &shader_modules, const ComputePipelineKey &key,
                  std::span<const std::byte> compute_spv);

    void clear(vk::Device device);

private:
    std::unordered_map<ComputePipelineKey, vk::Pipeline, ComputePipelineKeyHash>
        pipelines_;
};

} // namespace rhi
