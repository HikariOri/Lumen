#include "rhi/shader_reflection.hpp"

#include "rhi/descriptor_layout_cache.hpp"

#include "core/log/logger.hpp"

#include <spirv_cross.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>

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
    b.descriptor_type = dtype;
    b.descriptor_count = count;
    b.stages = stage;
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

} // namespace rhi
