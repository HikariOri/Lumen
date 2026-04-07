#pragma once

#include "rhi/buffer.hpp"
#include "rhi/shader_reflection.hpp"
#include "rhi/vulkan.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rhi {

class Device;

/// 为 `ShaderReflection` 中某一 buffer 类 binding 提供 GPU 缓冲与区间（`range` 对 Dynamic UBO 为 stride）。
struct ReflectedBufferBinding {
    std::uint32_t set {};
    std::uint32_t binding {};
    BufferHandle buffer {};
    vk::DeviceSize buffer_offset { 0 };
    vk::DeviceSize range {};
};

/// 按反射池计划建池，并按 `set_layouts_ordered`（set0..setN）重复 `sets_per_layout` 次链接分配描述符集。
[[nodiscard]] bool allocate_reflected_descriptor_pool_and_sets(
    vk::Device device, const ShaderReflection &merged,
    std::span<const vk::DescriptorSetLayout> set_layouts_ordered,
    std::uint32_t sets_per_layout, vk::DescriptorPool &out_pool,
    std::vector<vk::DescriptorSet> &out_sets);

/// 按 `merged.bindings` 对 **buffer 类** 描述符写 `updateDescriptorSets`；`sets_by_set_index[s]` 对应 SPIR-V set `s`。
/// 当前支持 `eUniformBuffer` / `eUniformBufferDynamic` / `eStorageBuffer`。
[[nodiscard]] bool update_reflected_buffer_descriptors(
    vk::Device device, const ShaderReflection &merged,
    std::span<const vk::DescriptorSet> sets_by_set_index, Device &rdev,
    std::span<const ReflectedBufferBinding> buffer_bindings);

} // namespace rhi
