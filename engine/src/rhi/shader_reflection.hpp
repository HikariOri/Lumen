#pragma once

#include "rhi/vulkan.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhi {

class DescriptorSetLayoutCache;
class PipelineLayoutCache;

/// UBO 在管线布局中的存储策略（在 `create_reflected_pipeline_layouts` 前固化；
/// 与 `descriptor_type` 中 Static / Dynamic UBO 一致）。
enum class UniformBufferBindingMode : std::uint8_t { Static, Dynamic };

/// 单条描述符绑定（跨 stage 合并后 `stages` 可能多 flag）。
struct DescriptorBinding {
    std::uint32_t set {};
    std::uint32_t binding {};
    /// SPIR-V / GLSL 中该描述符对应资源的名称（如 `uniform Block { } instance` 的 instance 名）。
    std::string resource_name {};
    vk::DescriptorType descriptor_type { vk::DescriptorType::eUniformBuffer };
    std::uint32_t descriptor_count { 1 };
    vk::ShaderStageFlags stages {};
    /// 仅对 uniform buffer 族有意义；反射默认为 `Static`，`promote_uniform_binding_to_dynamic_by_name` 置为 `Dynamic`。
    UniformBufferBindingMode uniform_buffer_mode {
        UniformBufferBindingMode::Static};
};

/// 单阶段 SPIR-V 反射结果。
struct ShaderReflection {
    std::vector<DescriptorBinding> bindings;
    std::vector<vk::PushConstantRange> push_constant_ranges;
};

/// 顶点着色器 `stage input` 反射为 **单 binding、交错布局**（按 `Location` 升序排布，`stride` 为紧凑对齐后的顶点步长）。
struct ReflectedVertexInput {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
};

/// 仅解析 **顶点** 阶段 `Input` 接口（跳过内置变量）；不支持矩阵顶点属性、结构体展开等复杂形态。
[[nodiscard]] std::optional<ReflectedVertexInput>
reflect_vertex_input_interleaved(std::span<const std::uint32_t> vert_spirv_words);

/// 由 `ShaderReflection` 推导的 `vk::DescriptorPool` 容量（`maxSets` + 按类型计数）。
struct DescriptorPoolPlan {
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    std::uint32_t max_sets {};
};

/// 按合并反射与「每个 set 索引并行套数」计算池大小；与 `create_reflected_pipeline_layouts` 相同要求 **set 连续** `0..max`。
/// `sets_per_layout == 0` 返回 `std::nullopt`；`bindings` 为空返回 `pool_sizes` 空且 `max_sets==0`。
[[nodiscard]] std::optional<DescriptorPoolPlan>
descriptor_pool_plan_from_reflection(const ShaderReflection &merged,
                                     std::uint32_t sets_per_layout);

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

/// 将指定 `resource_name` 的 UBO 绑定的 `descriptor_type` 改为 `eUniformBufferDynamic`，并置
/// `uniform_buffer_mode = Dynamic`（须在 `create_reflected_pipeline_layouts` 之前调用）。
/// 名称须在合并后的反射中 **唯一** 匹配一条 `eUniformBuffer`；否则返回 `false`。
[[nodiscard]] bool promote_uniform_binding_to_dynamic_by_name(
    ShaderReflection &reflection, std::string_view resource_name);

/// 按 set 连续 0..N 从缓存构建 `DescriptorSetLayout`，再构建 `PipelineLayout`。
/// `out_set_layouts` 顺序与 set 编号一致；无绑定时二者均为空句柄合法。
[[nodiscard]] bool create_reflected_pipeline_layouts(
    vk::Device device, const ShaderReflection &merged,
    DescriptorSetLayoutCache &set_cache, PipelineLayoutCache &pl_cache,
    std::vector<vk::DescriptorSetLayout> &out_set_layouts,
    vk::PipelineLayout &out_pipeline_layout);

} // namespace rhi
