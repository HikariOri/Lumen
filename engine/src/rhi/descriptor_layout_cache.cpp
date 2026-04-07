#include "rhi/descriptor_layout_cache.hpp"

#include "core/log/logger.hpp"

#include <functional>

namespace rhi {

namespace {

[[nodiscard]] std::size_t hash_descriptor_binding(const DescriptorBinding &b) noexcept {
    const std::size_t h1 =
        std::hash<std::uint32_t>{}(b.binding) ^
        (std::hash<std::int32_t>{}(static_cast<std::int32_t>(b.descriptor_type))
         << 1);
    const std::size_t h2 = std::hash<std::uint32_t>{}(b.descriptor_count);
    const std::size_t h3 = std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(
        static_cast<VkShaderStageFlags>(b.stages)));
    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

} // namespace

bool DescriptorSetLayoutCache::SetLayoutKey::operator==(
    const SetLayoutKey &o) const noexcept {
    if (bindings.size() != o.bindings.size()) {
        return false;
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const DescriptorBinding &a = bindings[i];
        const DescriptorBinding &b = o.bindings[i];
        if (a.binding != b.binding || a.descriptor_type != b.descriptor_type ||
            a.descriptor_count != b.descriptor_count || a.stages != b.stages) {
            return false;
        }
    }
    return true;
}

std::size_t DescriptorSetLayoutCache::SetLayoutKeyHash::operator()(
    const SetLayoutKey &k) const noexcept {
    std::size_t h = k.bindings.size();
    for (const DescriptorBinding &b : k.bindings) {
        h ^= hash_descriptor_binding(b) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

vk::DescriptorSetLayout DescriptorSetLayoutCache::get_or_create(
    vk::Device device, const std::vector<DescriptorBinding> &bindings_sorted) {
    if (!device) {
        return nullptr;
    }
    SetLayoutKey key {};
    key.bindings = bindings_sorted;
    const auto it = layouts_.find(key);
    if (it != layouts_.end()) {
        return it->second;
    }

    std::vector<vk::DescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(bindings_sorted.size());
    for (const DescriptorBinding &b : bindings_sorted) {
        vk::DescriptorSetLayoutBinding lb {};
        lb.binding = b.binding;
        lb.descriptorType = b.descriptor_type;
        lb.descriptorCount = b.descriptor_count;
        lb.stageFlags = b.stages;
        vk_bindings.push_back(lb);
    }

    vk::DescriptorSetLayoutCreateInfo ci {};
    ci.bindingCount = static_cast<std::uint32_t>(vk_bindings.size());
    ci.pBindings = vk_bindings.data();

    vk::DescriptorSetLayout layout {};
    const vk::Result r =
        device.createDescriptorSetLayout(&ci, nullptr, &layout);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("DescriptorSetLayoutCache: createDescriptorSetLayout "
                        "失败 {}",
                        static_cast<int>(r));
        return nullptr;
    }
    key.bindings = bindings_sorted;
    layouts_.emplace(std::move(key), layout);
    return layout;
}

void DescriptorSetLayoutCache::clear(vk::Device device) {
    if (!device) {
        layouts_.clear();
        return;
    }
    for (auto &e : layouts_) {
        if (e.second) {
            device.destroyDescriptorSetLayout(e.second, nullptr);
            e.second = nullptr;
        }
    }
    layouts_.clear();
}

bool PipelineLayoutCache::PipelineLayoutKey::operator==(
    const PipelineLayoutKey &o) const noexcept {
    if (set_layout_handles != o.set_layout_handles) {
        return false;
    }
    if (push_ranges.size() != o.push_ranges.size()) {
        return false;
    }
    for (std::size_t i = 0; i < push_ranges.size(); ++i) {
        const vk::PushConstantRange &a = push_ranges[i];
        const vk::PushConstantRange &b = o.push_ranges[i];
        if (a.stageFlags != b.stageFlags || a.offset != b.offset ||
            a.size != b.size) {
            return false;
        }
    }
    return true;
}

std::size_t PipelineLayoutCache::PipelineLayoutKeyHash::operator()(
    const PipelineLayoutKey &k) const noexcept {
    std::size_t h = k.set_layout_handles.size();
    for (std::uint64_t v : k.set_layout_handles) {
        h ^= std::hash<std::uint64_t>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    for (const vk::PushConstantRange &p : k.push_ranges) {
        h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(
                 static_cast<VkShaderStageFlags>(p.stageFlags))) ^
            (std::hash<std::uint32_t>{}(p.offset) << 1) ^
            (std::hash<std::uint32_t>{}(p.size) << 2);
        h += 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

vk::PipelineLayout PipelineLayoutCache::get_or_create(
    vk::Device device, const std::vector<vk::DescriptorSetLayout> &set_layouts,
    const std::vector<vk::PushConstantRange> &push_ranges) {
    if (!device) {
        return nullptr;
    }

    PipelineLayoutKey key {};
    key.set_layout_handles.reserve(set_layouts.size());
    for (vk::DescriptorSetLayout l : set_layouts) {
        key.set_layout_handles.push_back(
            reinterpret_cast<std::uint64_t>(static_cast<VkDescriptorSetLayout>(l)));
    }
    key.push_ranges = push_ranges;

    const auto it = layouts_.find(key);
    if (it != layouts_.end()) {
        return it->second;
    }

    vk::PipelineLayoutCreateInfo plci {};
    plci.setLayoutCount = static_cast<std::uint32_t>(set_layouts.size());
    plci.pSetLayouts = set_layouts.data();
    plci.pushConstantRangeCount =
        static_cast<std::uint32_t>(push_ranges.size());
    plci.pPushConstantRanges = push_ranges.data();

    vk::PipelineLayout layout {};
    const vk::Result r =
        device.createPipelineLayout(&plci, nullptr, &layout);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("PipelineLayoutCache: createPipelineLayout 失败 {}",
                        static_cast<int>(r));
        return nullptr;
    }
    layouts_.emplace(std::move(key), layout);
    return layout;
}

void PipelineLayoutCache::clear(vk::Device device) {
    if (!device) {
        layouts_.clear();
        return;
    }
    for (auto &e : layouts_) {
        if (e.second) {
            device.destroyPipelineLayout(e.second, nullptr);
            e.second = nullptr;
        }
    }
    layouts_.clear();
}

} // namespace rhi
