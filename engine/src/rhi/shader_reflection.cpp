#include "rhi/shader_reflection.hpp"

#include "rhi/descriptor_layout_cache.hpp"

#include "core/log/logger.hpp"

#include <spirv_cross.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace rhi {

namespace {

[[nodiscard]] std::uint32_t
descriptor_array_size(const spirv_cross::Compiler &comp,
                      spirv_cross::TypeID type_id) {
    const spirv_cross::SPIRType &t = comp.get_type(type_id);
    if (t.array.empty()) {
        return 1;
    }
    std::uint32_t n = 1;
    for (std::uint32_t dim : t.array) {
        if (dim == 0u) {
            return std::max(1u, n);
        }
        n *= dim;
    }
    return std::max(1u, n);
}

void append_push_merged(std::vector<vk::PushConstantRange> &dst,
                        const std::vector<vk::PushConstantRange> &src) {
    for (const vk::PushConstantRange &p : src) {
        bool found = false;
        for (vk::PushConstantRange &d : dst) {
            if (d.offset == p.offset && d.size == p.size) {
                d.stageFlags |= p.stageFlags;
                found = true;
                break;
            }
        }
        if (!found) {
            dst.push_back(p);
        }
    }
}

[[nodiscard]] std::optional<DescriptorBinding>
reflect_resource(const spirv_cross::Compiler &comp,
                 const spirv_cross::Resource &res, vk::DescriptorType dtype,
                 vk::ShaderStageFlagBits stage) {
    const std::uint32_t set =
        comp.get_decoration(res.id, spv::DecorationDescriptorSet);
    const std::uint32_t binding =
        comp.get_decoration(res.id, spv::DecorationBinding);
    const std::uint32_t count = descriptor_array_size(comp, res.type_id);
    DescriptorBinding b {};
    b.set = set;
    b.binding = binding;
    b.resource_name = comp.get_name(res.id);
    b.descriptor_type = dtype;
    b.descriptor_count = count;
    b.stages = stage;
    if (dtype == vk::DescriptorType::eUniformBuffer) {
        b.uniform_buffer_mode = UniformBufferBindingMode::Static;
    }
    return b;
}

void reflect_stage_resources(spirv_cross::Compiler &comp,
                             ShaderReflection &out,
                             vk::ShaderStageFlagBits stage) {
    const spirv_cross::ShaderResources res = comp.get_shader_resources();

    for (const spirv_cross::Resource &r : res.uniform_buffers) {
        const auto b = reflect_resource(comp, r, vk::DescriptorType::eUniformBuffer,
                                        stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.storage_buffers) {
        const auto b = reflect_resource(comp, r, vk::DescriptorType::eStorageBuffer,
                                        stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.sampled_images) {
        const auto b = reflect_resource(
            comp, r, vk::DescriptorType::eCombinedImageSampler, stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.separate_images) {
        const auto b = reflect_resource(comp, r, vk::DescriptorType::eSampledImage,
                                        stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.separate_samplers) {
        const auto b =
            reflect_resource(comp, r, vk::DescriptorType::eSampler, stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.storage_images) {
        const auto b = reflect_resource(comp, r, vk::DescriptorType::eStorageImage,
                                        stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.subpass_inputs) {
        const auto b = reflect_resource(
            comp, r, vk::DescriptorType::eInputAttachment, stage);
        if (b) {
            out.bindings.push_back(*b);
        }
    }
    for (const spirv_cross::Resource &r : res.push_constant_buffers) {
        const spirv_cross::SmallVector<spirv_cross::BufferRange> active =
            comp.get_active_buffer_ranges(r.id);
        if (!active.empty()) {
            for (const spirv_cross::BufferRange &br : active) {
                vk::PushConstantRange pcr {};
                pcr.stageFlags = stage;
                pcr.offset = static_cast<std::uint32_t>(br.offset);
                pcr.size = static_cast<std::uint32_t>(br.range);
                out.push_constant_ranges.push_back(pcr);
            }
        } else {
            const spirv_cross::SPIRType &bt = comp.get_type(r.base_type_id);
            std::uint32_t sz = static_cast<std::uint32_t>(
                comp.get_declared_struct_size(bt));
            sz = (sz + 3u) & ~3u;
            if (sz > 0) {
                vk::PushConstantRange pcr {};
                pcr.stageFlags = stage;
                pcr.offset = 0;
                pcr.size = sz;
                out.push_constant_ranges.push_back(pcr);
            }
        }
    }
}

} // namespace

std::optional<ShaderReflection>
reflect_spirv(const std::span<const std::uint32_t> words,
              const vk::ShaderStageFlagBits stage) {
    if (words.empty() || (words.size() * sizeof(std::uint32_t)) < sizeof(std::uint32_t) * 5) {
        LUMEN_LOG_ERROR("reflect_spirv: SPIR-V 过短");
        return std::nullopt;
    }
    try {
        std::vector<std::uint32_t> copy(words.begin(), words.end());
        spirv_cross::Compiler comp(std::move(copy));
        ShaderReflection out {};
        reflect_stage_resources(comp, out, stage);
        return out;
    } catch (const std::exception &ex) {
        LUMEN_LOG_ERROR("reflect_spirv: SPIRV-Cross 异常 {}", ex.what());
        return std::nullopt;
    }
}

bool merge_reflection(ShaderReflection &dst, const ShaderReflection &src) {
    for (const DescriptorBinding &sb : src.bindings) {
        auto it = std::find_if(
            dst.bindings.begin(), dst.bindings.end(),
            [&](const DescriptorBinding &d) {
                return d.set == sb.set && d.binding == sb.binding;
            });
        if (it != dst.bindings.end()) {
            if (it->descriptor_type != sb.descriptor_type ||
                it->descriptor_count != sb.descriptor_count) {
                LUMEN_LOG_ERROR(
                    "merge_reflection: set={} binding={} 类型或 count 冲突",
                    sb.set, sb.binding);
                return false;
            }
            if (it->uniform_buffer_mode != sb.uniform_buffer_mode) {
                LUMEN_LOG_ERROR(
                    "merge_reflection: set={} binding={} uniform_buffer_mode 冲突",
                    sb.set, sb.binding);
                return false;
            }
            if (!it->resource_name.empty() && !sb.resource_name.empty() &&
                it->resource_name != sb.resource_name) {
                LUMEN_LOG_ERROR(
                    "merge_reflection: set={} binding={} resource_name 冲突 "
                    "(\"{}\" vs \"{}\")",
                    sb.set, sb.binding, it->resource_name, sb.resource_name);
                return false;
            }
            if (it->resource_name.empty() && !sb.resource_name.empty()) {
                it->resource_name = sb.resource_name;
            }
            it->stages |= sb.stages;
        } else {
            dst.bindings.push_back(sb);
        }
    }
    append_push_merged(dst.push_constant_ranges, src.push_constant_ranges);
    return true;
}

bool merge_vert_frag_reflection(const ShaderReflection &vert,
                                const ShaderReflection &frag,
                                ShaderReflection &out_merged) {
    out_merged = vert;
    return merge_reflection(out_merged, frag);
}

std::optional<DescriptorPoolPlan>
descriptor_pool_plan_from_reflection(const ShaderReflection &merged,
                                     const std::uint32_t sets_per_layout) {
    if (sets_per_layout == 0u) {
        LUMEN_LOG_ERROR(
            "descriptor_pool_plan_from_reflection: sets_per_layout 不能为 0");
        return std::nullopt;
    }
    if (merged.bindings.empty()) {
        return DescriptorPoolPlan {};
    }

    std::map<std::uint32_t, std::vector<DescriptorBinding>> by_set;
    for (const DescriptorBinding &b : merged.bindings) {
        by_set[b.set].push_back(b);
    }
    const std::uint32_t max_set = by_set.rbegin()->first;
    for (std::uint32_t s = 0; s <= max_set; ++s) {
        if (by_set.find(s) == by_set.end()) {
            LUMEN_LOG_ERROR(
                "descriptor_pool_plan_from_reflection: descriptor set 不连续，缺少 "
                "set {}",
                s);
            return std::nullopt;
        }
    }
    const std::uint32_t num_sets = max_set + 1u;

    std::map<vk::DescriptorType, std::uint32_t> by_type;
    for (const DescriptorBinding &b : merged.bindings) {
        by_type[b.descriptor_type] += b.descriptor_count * sets_per_layout;
    }
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    pool_sizes.reserve(by_type.size());
    for (const auto &kv : by_type) {
        vk::DescriptorPoolSize ps {};
        ps.type = kv.first;
        ps.descriptorCount = kv.second;
        pool_sizes.push_back(ps);
    }
    DescriptorPoolPlan plan {};
    plan.pool_sizes = std::move(pool_sizes);
    plan.max_sets = num_sets * sets_per_layout;
    return plan;
}

bool promote_uniform_binding_to_dynamic_by_name(
    ShaderReflection &reflection, const std::string_view resource_name) {
    if (resource_name.empty()) {
        LUMEN_LOG_ERROR(
            "promote_uniform_binding_to_dynamic_by_name: resource_name 为空");
        return false;
    }
    DescriptorBinding *found = nullptr;
    for (DescriptorBinding &b : reflection.bindings) {
        if (b.resource_name != resource_name) {
            continue;
        }
        if (found != nullptr) {
            LUMEN_LOG_ERROR(
                "promote_uniform_binding_to_dynamic_by_name: 名称 \"{}\" 匹配多条绑定",
                resource_name);
            return false;
        }
        found = &b;
    }
    if (found == nullptr) {
        LUMEN_LOG_ERROR(
            "promote_uniform_binding_to_dynamic_by_name: 未找到 resource_name=\"{}\"",
            resource_name);
        return false;
    }
    if (found->descriptor_type != vk::DescriptorType::eUniformBuffer) {
        LUMEN_LOG_ERROR(
            "promote_uniform_binding_to_dynamic_by_name: \"{}\" 非 eUniformBuffer",
            resource_name);
        return false;
    }
    found->descriptor_type = vk::DescriptorType::eUniformBufferDynamic;
    found->uniform_buffer_mode = UniformBufferBindingMode::Dynamic;
    return true;
}

bool create_reflected_pipeline_layouts(
    vk::Device device, const ShaderReflection &merged,
    DescriptorSetLayoutCache &set_cache, PipelineLayoutCache &pl_cache,
    std::vector<vk::DescriptorSetLayout> &out_set_layouts,
    vk::PipelineLayout &out_pipeline_layout) {
    out_set_layouts.clear();
    out_pipeline_layout = nullptr;

    if (!device) {
        LUMEN_LOG_ERROR("create_reflected_pipeline_layouts: device 无效");
        return false;
    }

    if (merged.bindings.empty()) {
        out_pipeline_layout =
            pl_cache.get_or_create(device, {}, merged.push_constant_ranges);
        return static_cast<bool>(out_pipeline_layout);
    }

    std::map<std::uint32_t, std::vector<DescriptorBinding>> by_set;
    for (const DescriptorBinding &b : merged.bindings) {
        by_set[b.set].push_back(b);
    }

    const std::uint32_t max_set = by_set.rbegin()->first;
    for (std::uint32_t s = 0; s <= max_set; ++s) {
        const auto it = by_set.find(s);
        if (it == by_set.end()) {
            LUMEN_LOG_ERROR(
                "create_reflected_pipeline_layouts: descriptor set 不连续，缺少 "
                "set {}",
                s);
            return false;
        }
        std::vector<DescriptorBinding> &vec = it->second;
        std::sort(vec.begin(), vec.end(),
                  [](const DescriptorBinding &a, const DescriptorBinding &b) {
                      return a.binding < b.binding;
                  });
        vk::DescriptorSetLayout dsl = set_cache.get_or_create(device, vec);
        if (!dsl) {
            return false;
        }
        out_set_layouts.push_back(dsl);
    }

    out_pipeline_layout = pl_cache.get_or_create(device, out_set_layouts,
                                                   merged.push_constant_ranges);
    return static_cast<bool>(out_pipeline_layout);
}

namespace {

[[nodiscard]] std::uint32_t align_up_u32(const std::uint32_t v,
                                       const std::uint32_t a) noexcept {
    if (a == 0u) {
        return v;
    }
    return (v + a - 1u) / a * a;
}

/// 返回 `vk::Format` 与 **紧密** 字节宽度（用于交错布局步进）。
[[nodiscard]] std::optional<std::pair<vk::Format, std::uint32_t>>
spirv_vertex_vector_format(const spirv_cross::Compiler &comp,
                           const spirv_cross::TypeID type_id) {
    const spirv_cross::SPIRType &t = comp.get_type(type_id);
    if (!t.array.empty()) {
        return std::nullopt;
    }
    if (t.columns != 1u) {
        return std::nullopt;
    }
    const std::uint32_t n = t.vecsize;
    if (n < 1u || n > 4u) {
        return std::nullopt;
    }
    using BT = spirv_cross::SPIRType::BaseType;
    switch (t.basetype) {
    case BT::Float:
        if (t.width == 16u) {
            switch (n) {
            case 1:
                return std::make_pair(vk::Format::eR16Sfloat, 2u);
            case 2:
                return std::make_pair(vk::Format::eR16G16Sfloat, 4u);
            case 3:
                return std::make_pair(vk::Format::eR16G16B16Sfloat, 6u);
            case 4:
                return std::make_pair(vk::Format::eR16G16B16A16Sfloat, 8u);
            default:
                break;
            }
        }
        if (t.width == 32u) {
            switch (n) {
            case 1:
                return std::make_pair(vk::Format::eR32Sfloat, 4u);
            case 2:
                return std::make_pair(vk::Format::eR32G32Sfloat, 8u);
            case 3:
                return std::make_pair(vk::Format::eR32G32B32Sfloat, 12u);
            case 4:
                return std::make_pair(vk::Format::eR32G32B32A32Sfloat, 16u);
            default:
                break;
            }
        }
        if (t.width == 64u) {
            switch (n) {
            case 1:
                return std::make_pair(vk::Format::eR64Sfloat, 8u);
            case 2:
                return std::make_pair(vk::Format::eR64G64Sfloat, 16u);
            case 3:
                return std::make_pair(vk::Format::eR64G64B64Sfloat, 24u);
            case 4:
                return std::make_pair(vk::Format::eR64G64B64A64Sfloat, 32u);
            default:
                break;
            }
        }
        break;
    case BT::Int:
        if (t.width == 32u) {
            switch (n) {
            case 1:
                return std::make_pair(vk::Format::eR32Sint, 4u);
            case 2:
                return std::make_pair(vk::Format::eR32G32Sint, 8u);
            case 3:
                return std::make_pair(vk::Format::eR32G32B32Sint, 12u);
            case 4:
                return std::make_pair(vk::Format::eR32G32B32A32Sint, 16u);
            default:
                break;
            }
        }
        break;
    case BT::UInt:
        if (t.width == 32u) {
            switch (n) {
            case 1:
                return std::make_pair(vk::Format::eR32Uint, 4u);
            case 2:
                return std::make_pair(vk::Format::eR32G32Uint, 8u);
            case 3:
                return std::make_pair(vk::Format::eR32G32B32Uint, 12u);
            case 4:
                return std::make_pair(vk::Format::eR32G32B32A32Uint, 16u);
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace

std::optional<ReflectedVertexInput>
reflect_vertex_input_interleaved(const std::span<const std::uint32_t> words) {
    if (words.empty() ||
        (words.size() * sizeof(std::uint32_t)) < sizeof(std::uint32_t) * 5u) {
        LUMEN_LOG_ERROR("reflect_vertex_input_interleaved: SPIR-V 过短");
        return std::nullopt;
    }
    try {
        std::vector<std::uint32_t> copy(words.begin(), words.end());
        spirv_cross::Compiler comp(std::move(copy));
        const spirv_cross::ShaderResources res = comp.get_shader_resources();

        struct Item {
            std::uint32_t location {};
            spirv_cross::TypeID type_id {};
        };
        std::vector<Item> items;
        items.reserve(res.stage_inputs.size());

        for (const spirv_cross::Resource &r : res.stage_inputs) {
            if (comp.has_decoration(r.id, spv::DecorationBuiltIn)) {
                continue;
            }
            if (!comp.has_decoration(r.id, spv::DecorationLocation)) {
                LUMEN_LOG_ERROR(
                    "reflect_vertex_input_interleaved: 用户 stage input 缺少 "
                    "Location");
                return std::nullopt;
            }
            const std::uint32_t loc =
                comp.get_decoration(r.id, spv::DecorationLocation);
            items.push_back(Item { loc, r.type_id });
        }

        if (items.empty()) {
            LUMEN_LOG_ERROR("reflect_vertex_input_interleaved: 无用户顶点输入");
            return std::nullopt;
        }

        std::sort(items.begin(), items.end(),
                  [](const Item &a, const Item &b) {
                      return a.location < b.location;
                  });
        for (std::size_t i = 1; i < items.size(); ++i) {
            if (items[i].location == items[i - 1].location) {
                LUMEN_LOG_ERROR(
                    "reflect_vertex_input_interleaved: Location {} 重复",
                    items[i].location);
                return std::nullopt;
            }
        }

        ReflectedVertexInput out {};
        out.attributes.reserve(items.size());
        std::uint32_t cursor = 0u;
        constexpr std::uint32_t k_align = 4u;

        for (const Item &it : items) {
            const std::optional<std::pair<vk::Format, std::uint32_t>> fmt_sz =
                spirv_vertex_vector_format(comp, it.type_id);
            if (!fmt_sz.has_value()) {
                LUMEN_LOG_ERROR(
                    "reflect_vertex_input_interleaved: Location {} 类型不支持",
                    it.location);
                return std::nullopt;
            }
            cursor = align_up_u32(cursor, k_align);
            vk::VertexInputAttributeDescription a {};
            a.location = it.location;
            a.binding = 0;
            a.format = fmt_sz->first;
            a.offset = cursor;
            out.attributes.push_back(a);
            cursor += fmt_sz->second;
        }

        const std::uint32_t stride = align_up_u32(cursor, k_align);
        vk::VertexInputBindingDescription vib {};
        vib.binding = 0;
        vib.stride = stride;
        vib.inputRate = vk::VertexInputRate::eVertex;
        out.bindings.push_back(vib);
        return out;
    } catch (const std::exception &ex) {
        LUMEN_LOG_ERROR("reflect_vertex_input_interleaved: SPIRV-Cross 异常 {}",
                        ex.what());
        return std::nullopt;
    }
}

} // namespace rhi
