#include "rhi/compute_pipeline_cache.hpp"

#include "rhi/spirv_hash.hpp"

#include "core/log/logger.hpp"

namespace rhi {

namespace {

[[nodiscard]] vk::PipelineLayout layout_from_u64(const std::uint64_t h) {
    return vk::PipelineLayout { reinterpret_cast<VkPipelineLayout>(h) };
}

} // namespace

vk::Pipeline ComputePipelineCache::get_or_create(
    const vk::Device device, const vk::PipelineCache pipeline_cache,
    ShaderModuleCache &shader_modules, const ComputePipelineKey &key,
    const std::span<const std::byte> compute_spv) {
    if (!device) {
        return nullptr;
    }
    const auto it = pipelines_.find(key);
    if (it != pipelines_.end()) {
        return it->second;
    }
    if (hash_spirv_bytes(compute_spv) != key.compute_spv_hash) {
        LUMEN_LOG_ERROR(
            "ComputePipelineCache::get_or_create: SPIR-V 哈希与 key 不一致");
        return nullptr;
    }
    const vk::ShaderModule mod = shader_modules.get_or_create(device, compute_spv);
    if (!mod) {
        return nullptr;
    }

    vk::PipelineShaderStageCreateInfo stage {};
    stage.stage = vk::ShaderStageFlagBits::eCompute;
    stage.module = mod;
    stage.pName = "main";

    vk::ComputePipelineCreateInfo ci {};
    ci.stage = stage;
    ci.layout = layout_from_u64(key.pipeline_layout);

    vk::Pipeline pipe {};
    const vk::Result r =
        device.createComputePipelines(pipeline_cache, 1, &ci, nullptr, &pipe);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("ComputePipelineCache: createComputePipelines 失败 {}",
                        static_cast<int>(r));
        return nullptr;
    }
    pipelines_.emplace(key, pipe);
    return pipe;
}

void ComputePipelineCache::clear(const vk::Device device) {
    if (device) {
        for (const auto &e : pipelines_) {
            if (e.second) {
                device.destroyPipeline(e.second, nullptr);
            }
        }
    }
    pipelines_.clear();
}

} // namespace rhi
