#pragma once

#include "rhi/shader_reflection.hpp"
#include "rhi/vulkan.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rhi {

/// 按「单 set 内规范化绑定列表」去重缓存 `VkDescriptorSetLayout`。
class DescriptorSetLayoutCache {
public:
    DescriptorSetLayoutCache() = default;
    DescriptorSetLayoutCache(const DescriptorSetLayoutCache &) = delete;
    DescriptorSetLayoutCache &operator=(const DescriptorSetLayoutCache &) = delete;
    DescriptorSetLayoutCache(DescriptorSetLayoutCache &&) = delete;
    DescriptorSetLayoutCache &operator=(DescriptorSetLayoutCache &&) = delete;
    ~DescriptorSetLayoutCache() = default;

    /// `bindings` 须已属于同一 `set` 且按 `binding` 升序；会忽略各条目的 `set` 字段差异，仅用 binding/type/count/stages。
    [[nodiscard]] vk::DescriptorSetLayout
    get_or_create(vk::Device device,
                  const std::vector<DescriptorBinding> &bindings_sorted);

    void clear(vk::Device device);

private:
    struct SetLayoutKey {
        std::vector<DescriptorBinding> bindings;
        [[nodiscard]] bool operator==(const SetLayoutKey &o) const noexcept;
    };

    struct SetLayoutKeyHash {
        [[nodiscard]] std::size_t operator()(const SetLayoutKey &k) const noexcept;
    };

    std::unordered_map<SetLayoutKey, vk::DescriptorSetLayout, SetLayoutKeyHash>
        layouts_;
};

/// 缓存 `PipelineLayout`（set layout 序列 + push constant ranges）。
class PipelineLayoutCache {
public:
    PipelineLayoutCache() = default;
    PipelineLayoutCache(const PipelineLayoutCache &) = delete;
    PipelineLayoutCache &operator=(const PipelineLayoutCache &) = delete;
    PipelineLayoutCache(PipelineLayoutCache &&) = delete;
    PipelineLayoutCache &operator=(PipelineLayoutCache &&) = delete;
    ~PipelineLayoutCache() = default;

    [[nodiscard]] vk::PipelineLayout
    get_or_create(vk::Device device,
                  const std::vector<vk::DescriptorSetLayout> &set_layouts,
                  const std::vector<vk::PushConstantRange> &push_ranges);

    void clear(vk::Device device);

private:
    struct PipelineLayoutKey {
        std::vector<std::uint64_t> set_layout_handles;
        std::vector<vk::PushConstantRange> push_ranges;
        [[nodiscard]] bool operator==(const PipelineLayoutKey &o) const noexcept;
    };

    struct PipelineLayoutKeyHash {
        [[nodiscard]] std::size_t operator()(const PipelineLayoutKey &k) const noexcept;
    };

    std::unordered_map<PipelineLayoutKey, vk::PipelineLayout, PipelineLayoutKeyHash>
        layouts_;
};

} // namespace rhi
