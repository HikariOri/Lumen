#pragma once

#include "rhi/vulkan.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rhi {

class DescriptorSetLayoutCache;
class PipelineLayoutCache;

/// 单条描述符绑定（跨 stage 合并后 `stages` 可能多 flag）。
struct DescriptorBinding {
    std::uint32_t set {};
    std::uint32_t binding {};
    vk::DescriptorType descriptor_type { vk::DescriptorType::eUniformBuffer };
    std::uint32_t descriptor_count { 1 };
    vk::ShaderStageFlags stages {};
};

/// 单阶段 SPIR-V 反射结果。
struct ShaderReflection {
    std::vector<DescriptorBinding> bindings;
    std::vector<vk::PushConstantRange> push_constant_ranges;
};

/// 解析单阶段 SPIR-V（`words` 为完整模块，含 magic/header）。失败返回 `std::nullopt`。
[[nodiscard]] std::optional<ShaderReflection>
reflect_spirv(std::span<const std::uint32_t> words,
            vk::ShaderStageFlagBits stage);

/// 将 `src` 合并进 `dst`：同 set+binding 则要求 type/count 一致并合并 `stages`。冲突返回 `false`。
[[nodiscard]] bool merge_reflection(ShaderReflection &dst,
                                    const ShaderReflection &src);

/// 合并 VS+FS 反射（先 vert 再 frag）。
[[nodiscard]] bool merge_vert_frag_reflection(
    const ShaderReflection &vert, const ShaderReflection &frag,
    ShaderReflection &out_merged);

/// 按 set 连续 0..N 从缓存构建 `DescriptorSetLayout`，再构建 `PipelineLayout`。
/// `out_set_layouts` 顺序与 set 编号一致；无绑定时二者均为空句柄合法。
[[nodiscard]] bool create_reflected_pipeline_layouts(
    vk::Device device, const ShaderReflection &merged,
    DescriptorSetLayoutCache &set_cache, PipelineLayoutCache &pl_cache,
    std::vector<vk::DescriptorSetLayout> &out_set_layouts,
    vk::PipelineLayout &out_pipeline_layout);

} // namespace rhi
