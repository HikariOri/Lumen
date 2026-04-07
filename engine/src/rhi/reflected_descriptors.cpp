#include "rhi/reflected_descriptors.hpp"

#include "rhi/device.hpp"

#include "core/log/logger.hpp"

#include <algorithm>

namespace rhi {

bool allocate_reflected_descriptor_pool_and_sets(
    const vk::Device device, const ShaderReflection &merged,
    const std::span<const vk::DescriptorSetLayout> set_layouts_ordered,
    const std::uint32_t sets_per_layout, vk::DescriptorPool &out_pool,
    std::vector<vk::DescriptorSet> &out_sets) {
    out_pool = nullptr;
    out_sets.clear();

    if (set_layouts_ordered.empty()) {
        LUMEN_LOG_ERROR(
            "allocate_reflected_descriptor_pool_and_sets: set_layouts 为空");
        return false;
    }

    const std::optional<DescriptorPoolPlan> plan_opt =
        descriptor_pool_plan_from_reflection(merged, sets_per_layout);
    if (!plan_opt.has_value()) {
        LUMEN_LOG_ERROR(
            "allocate_reflected_descriptor_pool_and_sets: descriptor_pool_plan "
            "失败");
        return false;
    }
    const DescriptorPoolPlan &plan = *plan_opt;
    if (plan.pool_sizes.empty() || plan.max_sets == 0u) {
        if (!merged.bindings.empty()) {
            LUMEN_LOG_ERROR("allocate_reflected_descriptor_pool_and_sets: "
                            "有绑定但池计划为空");
            return false;
        }
        return true;
    }

    vk::DescriptorPoolCreateInfo dpci {};
    dpci.maxSets = plan.max_sets;
    dpci.poolSizeCount =
        static_cast<std::uint32_t>(plan.pool_sizes.size());
    dpci.pPoolSizes = plan.pool_sizes.data();
    const vk::Result rdp =
        device.createDescriptorPool(&dpci, nullptr, &out_pool);
    if (rdp != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("allocate_reflected_descriptor_pool_and_sets: "
                        "createDescriptorPool 失败 vk::Result={}",
                        static_cast<int>(rdp));
        out_pool = nullptr;
        return false;
    }

    std::vector<vk::DescriptorSetLayout> chain;
    chain.reserve(set_layouts_ordered.size() * sets_per_layout);
    for (std::uint32_t i = 0; i < sets_per_layout; ++i) {
        for (const vk::DescriptorSetLayout l : set_layouts_ordered) {
            chain.push_back(l);
        }
    }

    out_sets.resize(chain.size());
    vk::DescriptorSetAllocateInfo dsai {};
    dsai.descriptorPool = out_pool;
    dsai.descriptorSetCount = static_cast<std::uint32_t>(chain.size());
    dsai.pSetLayouts = chain.data();
    const vk::Result ras =
        device.allocateDescriptorSets(&dsai, out_sets.data());
    if (ras != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR(
            "allocate_reflected_descriptor_pool_and_sets: "
            "allocateDescriptorSets 失败 vk::Result={}",
            static_cast<int>(ras));
        device.destroyDescriptorPool(out_pool, nullptr);
        out_pool = nullptr;
        out_sets.clear();
        return false;
    }
    return true;
}

bool update_reflected_buffer_descriptors(
    const vk::Device device, const ShaderReflection &merged,
    const std::span<const vk::DescriptorSet> sets_by_set_index,
    Device &rdev, const std::span<const ReflectedBufferBinding> buffer_bindings) {
    if (merged.bindings.empty()) {
        return true;
    }

    std::uint32_t max_set = 0;
    for (const DescriptorBinding &b : merged.bindings) {
        max_set = std::max(max_set, b.set);
    }
    if (sets_by_set_index.size() <= max_set) {
        LUMEN_LOG_ERROR(
            "update_reflected_buffer_descriptors: sets 数量不足以覆盖 set {}",
            max_set);
        return false;
    }

    std::vector<vk::DescriptorBufferInfo> infos;
    infos.reserve(merged.bindings.size());
    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(merged.bindings.size());

    for (const DescriptorBinding &b : merged.bindings) {
        switch (b.descriptor_type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eUniformBufferDynamic:
        case vk::DescriptorType::eStorageBuffer:
            break;
        default:
            LUMEN_LOG_ERROR(
                "update_reflected_buffer_descriptors: 暂不支持的描述符类型 "
                "set={} binding={} type={}",
                b.set, b.binding, static_cast<int>(b.descriptor_type));
            return false;
        }

        const ReflectedBufferBinding *src = nullptr;
        for (const ReflectedBufferBinding &rb : buffer_bindings) {
            if (rb.set == b.set && rb.binding == b.binding) {
                src = &rb;
                break;
            }
        }
        if (src == nullptr) {
            LUMEN_LOG_ERROR(
                "update_reflected_buffer_descriptors: 缺少 ReflectedBufferBinding "
                "set={} binding={}",
                b.set, b.binding);
            return false;
        }

        const BufferResource *br = rdev.try_get(src->buffer);
        if (br == nullptr) {
            LUMEN_LOG_ERROR(
                "update_reflected_buffer_descriptors: try_get(buffer) 失败 "
                "set={} binding={}",
                b.set, b.binding);
            return false;
        }

        vk::DescriptorBufferInfo bi {};
        bi.buffer = br->buffer;
        bi.offset = src->buffer_offset;
        bi.range = src->range;
        infos.push_back(bi);
    }

    for (std::size_t i = 0; i < merged.bindings.size(); ++i) {
        const DescriptorBinding &b = merged.bindings[i];
        vk::WriteDescriptorSet w {};
        w.dstSet = sets_by_set_index[b.set];
        w.dstBinding = b.binding;
        w.dstArrayElement = 0;
        w.descriptorCount = b.descriptor_count;
        w.descriptorType = b.descriptor_type;
        w.pBufferInfo = &infos[i];
        writes.push_back(w);
    }

    device.updateDescriptorSets(
        static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

} // namespace rhi
