#include "vulkan/shader/reflection/shader_reflection.hpp"

#include "core/log/logger.hpp"
#include "shader_reflection.hpp"
#include <algorithm>
#include <cstring>
#include <spirv_reflect.h>
#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace vulkan::shader::reflection {

namespace {

auto to_vk_descriptor_type_(SpvReflectDescriptorType type) -> VkDescriptorType {
    // clang-format off
        switch (type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER                    : return VK_DESCRIPTOR_TYPE_SAMPLER                    ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER     : return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER     ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE              : return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE              ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE              : return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE              ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER       : return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER       ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER       : return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER       ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER             : return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER             ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER             : return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER             ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC     : return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC     ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC     : return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC     ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT           : return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT           ; 
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR ; 
            default: break;
        }
    // clang-format on
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

/// 与 `spirv_reflect.cpp` 中 `vk_format_vertex_attribute_extent_bytes`
/// 一致，用于 校验 C++ 成员区间是否重叠。
[[nodiscard]] static std::uint32_t
vertex_attribute_extent_bytes(const VkFormat f) noexcept {
    switch (f) {
    case VK_FORMAT_UNDEFINED: return 0U;
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: return 2U;
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT: return 4U;
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16_SFLOAT: return 6U;
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8U;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT: return 4U;
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT: return 8U;
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_SFLOAT: return 12U;
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16U;
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64_SFLOAT: return 8U;
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64_SFLOAT: return 16U;
    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64_SFLOAT: return 24U;
    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_R64G64B64A64_SFLOAT: return 32U;
    default: return 0U;
    }
}

VkFormat to_vk_format(SpvReflectFormat format) {
    // clang-format off
    switch (format) {
        case SPV_REFLECT_FORMAT_UNDEFINED           : return VK_FORMAT_UNDEFINED           ;
        case SPV_REFLECT_FORMAT_R16_UINT            : return VK_FORMAT_R16_UINT            ;
        case SPV_REFLECT_FORMAT_R16_SINT            : return VK_FORMAT_R16_SINT            ;
        case SPV_REFLECT_FORMAT_R16_SFLOAT          : return VK_FORMAT_R16_SFLOAT          ;
        case SPV_REFLECT_FORMAT_R16G16_UINT         : return VK_FORMAT_R16G16_UINT         ;
        case SPV_REFLECT_FORMAT_R16G16_SINT         : return VK_FORMAT_R16G16_SINT         ;
        case SPV_REFLECT_FORMAT_R16G16_SFLOAT       : return VK_FORMAT_R16G16_SFLOAT       ;
        case SPV_REFLECT_FORMAT_R16G16B16_UINT      : return VK_FORMAT_R16G16B16_UINT      ;
        case SPV_REFLECT_FORMAT_R16G16B16_SINT      : return VK_FORMAT_R16G16B16_SINT      ;
        case SPV_REFLECT_FORMAT_R16G16B16_SFLOAT    : return VK_FORMAT_R16G16B16_SFLOAT    ;
        case SPV_REFLECT_FORMAT_R16G16B16A16_UINT   : return VK_FORMAT_R16G16B16A16_UINT   ;
        case SPV_REFLECT_FORMAT_R16G16B16A16_SINT   : return VK_FORMAT_R16G16B16A16_SINT   ;
        case SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT : return VK_FORMAT_R16G16B16A16_SFLOAT ;
        case SPV_REFLECT_FORMAT_R32_UINT            : return VK_FORMAT_R32_UINT            ;
        case SPV_REFLECT_FORMAT_R32_SINT            : return VK_FORMAT_R32_SINT            ;
        case SPV_REFLECT_FORMAT_R32_SFLOAT          : return VK_FORMAT_R32_SFLOAT          ;
        case SPV_REFLECT_FORMAT_R32G32_UINT         : return VK_FORMAT_R32G32_UINT         ;
        case SPV_REFLECT_FORMAT_R32G32_SINT         : return VK_FORMAT_R32G32_SINT         ;
        case SPV_REFLECT_FORMAT_R32G32_SFLOAT       : return VK_FORMAT_R32G32_SFLOAT       ;
        case SPV_REFLECT_FORMAT_R32G32B32_UINT      : return VK_FORMAT_R32G32B32_UINT      ;
        case SPV_REFLECT_FORMAT_R32G32B32_SINT      : return VK_FORMAT_R32G32B32_SINT      ;
        case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT    : return VK_FORMAT_R32G32B32_SFLOAT    ;
        case SPV_REFLECT_FORMAT_R32G32B32A32_UINT   : return VK_FORMAT_R32G32B32A32_UINT   ;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SINT   : return VK_FORMAT_R32G32B32A32_SINT   ;
        case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT : return VK_FORMAT_R32G32B32A32_SFLOAT ;
        case SPV_REFLECT_FORMAT_R64_UINT            : return VK_FORMAT_R64_UINT            ;
        case SPV_REFLECT_FORMAT_R64_SINT            : return VK_FORMAT_R64_SINT            ;
        case SPV_REFLECT_FORMAT_R64_SFLOAT          : return VK_FORMAT_R64_SFLOAT          ;
        case SPV_REFLECT_FORMAT_R64G64_UINT         : return VK_FORMAT_R64G64_UINT         ;
        case SPV_REFLECT_FORMAT_R64G64_SINT         : return VK_FORMAT_R64G64_SINT         ;
        case SPV_REFLECT_FORMAT_R64G64_SFLOAT       : return VK_FORMAT_R64G64_SFLOAT       ;
        case SPV_REFLECT_FORMAT_R64G64B64_UINT      : return VK_FORMAT_R64G64B64_UINT      ;
        case SPV_REFLECT_FORMAT_R64G64B64_SINT      : return VK_FORMAT_R64G64B64_SINT      ;
        case SPV_REFLECT_FORMAT_R64G64B64_SFLOAT    : return VK_FORMAT_R64G64B64_SFLOAT    ;
        case SPV_REFLECT_FORMAT_R64G64B64A64_UINT   : return VK_FORMAT_R64G64B64A64_UINT   ;
        case SPV_REFLECT_FORMAT_R64G64B64A64_SINT   : return VK_FORMAT_R64G64B64A64_SINT   ;
        case SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT : return VK_FORMAT_R64G64B64A64_SFLOAT ;
    default: return VK_FORMAT_UNDEFINED;
    }
    // clang-format on
}

void merge_layout_bindings(
    std::vector<VkDescriptorSetLayoutBinding> &dst,
    const std::vector<VkDescriptorSetLayoutBinding> &incoming) {
    for (const auto &b : incoming) {
        auto it =
            std::find_if(dst.begin(), dst.end(), [&](const auto &existing) {
                return existing.binding == b.binding;
            });
        if (it == dst.end()) {
            dst.push_back(b);
            continue;
        }
        if (it->descriptorType != b.descriptorType ||
            it->descriptorCount != b.descriptorCount) {
            LUMEN_LOG_ERROR(
                "Shader reflection merge: descriptor binding conflict (same "
                "binding index, type or count mismatch)");
            continue;
        }
        it->stageFlags |= b.stageFlags;
    }
    std::sort(dst.begin(), dst.end(), [](const auto &a, const auto &b) {
        return a.binding < b.binding;
    });
}

void merge_push_constants(std::vector<PushConstantRange> &dst,
                          const std::vector<PushConstantRange> &incoming) {
    for (const auto &pc : incoming) {
        auto it = std::find_if(dst.begin(), dst.end(), [&](const auto &e) {
            return e.offset == pc.offset && e.size == pc.size;
        });
        if (it == dst.end()) {
            dst.push_back(pc);
        } else {
            it->stages |= pc.stages;
        }
    }
}

} // namespace

LayoutCache &LayoutCache::instance() {
    static LayoutCache instance;
    return instance;
}

VkDescriptorSetLayout
LayoutCache::get_layout(VkDevice device, const DescriptorSetLayoutInfo &info) {
    auto key = LayoutKey {};
    key.set = info.set;

    for (const auto &b : info.bindings) {
        uint8_t h[sizeof(b.binding) + sizeof(b.descriptorType) +
                  sizeof(b.descriptorCount)];
        memcpy(h, &b.binding, sizeof(b.binding));
        memcpy(h + 4, &b.descriptorType, sizeof(b.descriptorType));
        memcpy(h + 8, &b.descriptorCount, sizeof(b.descriptorCount));
        key.hashData.insert(key.hashData.end(), h, h + sizeof(h));
    }

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }

    VkDescriptorSetLayoutCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)info.bindings.size(),
        .pBindings = info.bindings.data()
    };

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create descriptor set layout");
        return VK_NULL_HANDLE;
    }

    cache_[key] = layout;
    return layout;
}

void LayoutCache::clear(VkDevice device) {
    for (auto &[k, l] : cache_) {
        vkDestroyDescriptorSetLayout(device, l, nullptr);
    }
    cache_.clear();
}

bool ShaderReflection::reflect(VkShaderStageFlags stage,
                               std::vector<std::byte> spirv_code,
                               const ReflectOptions &options) noexcept {
    return reflect(stage,
                   reinterpret_cast<const std::uint32_t *>(spirv_code.data()),
                   spirv_code.size(), options);
}

bool ShaderReflection::reflect(VkShaderStageFlags stage,
                               std::vector<std::uint32_t> spirv_code,
                               const ReflectOptions &options) noexcept {
    return reflect(stage, spirv_code.data(),
                   spirv_code.size() * sizeof(std::uint32_t), options);
}

bool ShaderReflection::reflect(VkShaderStageFlags stage,
                               const std::uint32_t *spirv_code,
                               std::size_t codeSize,
                               const ReflectOptions &options) noexcept {

    SpvReflectShaderModule module {};

    // Descriptors
    const auto result =
        spvReflectCreateShaderModule(codeSize, spirv_code, &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create shader module");
        return false;
    }

    std::uint32_t count {};
    {
        spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        std::vector<SpvReflectDescriptorBinding *> reflected_bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count,
                                              reflected_bindings.data());

        for (const auto *rb : reflected_bindings) {
            VkDescriptorType vk_type =
                to_vk_descriptor_type_(rb->descriptor_type);
            if (vk_type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
                LUMEN_LOG_ERROR(
                    "SPIR-V reflection: unsupported descriptor type "
                    "(spirv-reflect "
                    "descriptor_type not mapped to VkDescriptorType)");
                continue;
            }
            vk_type = apply_ring_uniform_rule_(vk_type, rb->set, options);

            DescriptorBinding db {};
            db.set = rb->set;
            db.binding = rb->binding;
            db.type = vk_type;
            db.count = rb->count;
            db.stages = stage;
            db.name = rb->name != nullptr ? rb->name : "";
            bindings_.push_back(db);

            auto &info = setLayoutInfos_[rb->set];
            info.set = rb->set;
            info.bindings.push_back(VkDescriptorSetLayoutBinding {
                .binding = rb->binding,
                .descriptorType = vk_type,
                .descriptorCount = rb->count,
                .stageFlags = stage,
                .pImmutableSamplers = nullptr,
            });
            info.poolSizes.push_back(VkDescriptorPoolSize {
                .type = vk_type, .descriptorCount = rb->count });
        }

        for (auto &[set_index, info] : setLayoutInfos_) {
            (void)set_index;
            consolidate_pool_sizes_(info.poolSizes);
            std::sort(info.bindings.begin(), info.bindings.end(),
                      [](const VkDescriptorSetLayoutBinding &a,
                         const VkDescriptorSetLayoutBinding &b) {
                          return a.binding < b.binding;
                      });
        }
    }

    // Push Constants
    {
        spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
        if (count > 0) {
            std::vector<SpvReflectBlockVariable *> push_blocks(count);
            spvReflectEnumeratePushConstantBlocks(&module, &count,
                                                  push_blocks.data());
            for (const auto *blk : push_blocks) {
                if (blk == nullptr || blk->size == 0) {
                    continue;
                }
                PushConstantRange pcr {};
                pcr.offset = blk->offset;
                pcr.size = blk->size;
                pcr.stages = stage;
                pushConstantRanges_.push_back(pcr);
            }
        }
    }

    // Vertex Input Attributes (vertex shader only)
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        spvReflectEnumerateInputVariables(&module, &count, nullptr);
        std::vector<SpvReflectInterfaceVariable *> vars(count);
        spvReflectEnumerateInputVariables(&module, &count, vars.data());

        for (const auto *v : vars) {
            if (v->location == std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }

            VertexAttribute attr {};
            attr.location = v->location;
            attr.format = to_vk_format(v->format);
            attr.offset = 0;
            vertexAttributes_.push_back(attr);
        }

        std::ranges::sort(vertexAttributes_, [](const VertexAttribute &a,
                                                const VertexAttribute &b) {
            return a.location < b.location;
        });
    }

    spvReflectDestroyShaderModule(&module);
    return true;
}

void ShaderReflection::merge(const ShaderReflection &other) {
    for (const auto &incoming : other.bindings_) {
        auto it = std::find_if(bindings_.begin(), bindings_.end(),
                               [&](const DescriptorBinding &b) {
                                   return b.set == incoming.set &&
                                          b.binding == incoming.binding;
                               });
        if (it == bindings_.end()) {
            bindings_.push_back(incoming);
            continue;
        }
        if (it->type != incoming.type || it->count != incoming.count) {
            LUMEN_LOG_ERROR(
                "Shader reflection merge: descriptor binding conflict (set/"
                "binding type or count mismatch)");
            continue;
        }
        it->stages |= incoming.stages;
    }

    merge_push_constants(pushConstantRanges_, other.pushConstantRanges_);

    for (const auto &[set, src_info] : other.setLayoutInfos_) {
        auto &dst = setLayoutInfos_[set];
        dst.set = set;
        merge_layout_bindings(dst.bindings, src_info.bindings);
        merge_pool_sizes_(dst.poolSizes, src_info.poolSizes);
        consolidate_pool_sizes_(dst.poolSizes);
    }
}

auto ShaderReflection::apply_ring_uniform_rule_(VkDescriptorType type,
                                                std::uint32_t set,
                                                const ReflectOptions &options)
    -> VkDescriptorType {
    if (!options.ringUniformMaxSet.has_value()) {
        return type;
    }
    const std::uint32_t max_set = *options.ringUniformMaxSet;
    if (set <= max_set && type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    return type;
}

void ShaderReflection::merge_pool_sizes_(
    std::vector<VkDescriptorPoolSize> &dst,
    const std::vector<VkDescriptorPoolSize> &src) {
    for (const auto &ps : src) {
        auto it = std::find_if(
            dst.begin(), dst.end(),
            [&](const VkDescriptorPoolSize &x) { return x.type == ps.type; });
        if (it == dst.end()) {
            dst.push_back(ps);
        } else {
            it->descriptorCount += ps.descriptorCount;
        }
    }
}

void ShaderReflection::consolidate_pool_sizes_(
    std::vector<VkDescriptorPoolSize> &poolSizes) {
    std::vector<VkDescriptorPoolSize> tmp;
    merge_pool_sizes_(tmp, poolSizes);
    poolSizes = std::move(tmp);
}

void ShaderReflection::create_layouts(VkDevice device) noexcept {
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        return;
    }

    if (setLayoutInfos_.empty()) {
        VkPipelineLayoutCreateInfo pl_ci {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        };
        std::vector<VkPushConstantRange> vk_pc;
        vk_pc.reserve(pushConstantRanges_.size());
        for (const auto &pc : pushConstantRanges_) {
            vk_pc.push_back(VkPushConstantRange {
                .stageFlags = pc.stages,
                .offset = pc.offset,
                .size = pc.size,
            });
        }
        pl_ci.pushConstantRangeCount = static_cast<std::uint32_t>(vk_pc.size());
        pl_ci.pPushConstantRanges = vk_pc.data();
        if (vkCreatePipelineLayout(device, &pl_ci, nullptr,
                                   &pipelineLayout_) != VK_SUCCESS) {
            LUMEN_LOG_ERROR("Failed to create pipeline layout (no sets)");
        }
        return;
    }

    std::uint32_t max_set = 0;
    for (const auto &[s, _] : setLayoutInfos_) {
        max_set = std::max(max_set, s);
    }

    setLayouts_.clear();
    setLayouts_.reserve(static_cast<std::size_t>(max_set) + 1U);

    for (std::uint32_t s = 0; s <= max_set; ++s) {
        if (setLayoutInfos_.find(s) == setLayoutInfos_.end()) {
            setLayoutInfos_[s] = DescriptorSetLayoutInfo { .set = s };
        }

        auto &info = setLayoutInfos_[s];
        VkDescriptorSetLayoutCreateInfo dsl_ci {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = static_cast<std::uint32_t>(info.bindings.size()),
            .pBindings = info.bindings.data(),
        };

        if (vkCreateDescriptorSetLayout(device, &dsl_ci, nullptr,
                                        &info.layout) != VK_SUCCESS) {
            LUMEN_LOG_ERROR("Failed to create descriptor set layout");
            for (VkDescriptorSetLayout created : setLayouts_) {
                vkDestroyDescriptorSetLayout(device, created, nullptr);
            }
            setLayouts_.clear();
            for (auto &[_, inf] : setLayoutInfos_) {
                inf.layout = VK_NULL_HANDLE;
            }
            return;
        }
        setLayouts_.push_back(info.layout);
    }

    std::vector<VkPushConstantRange> vk_pc;
    vk_pc.reserve(pushConstantRanges_.size());
    for (const auto &pc : pushConstantRanges_) {
        vk_pc.push_back(VkPushConstantRange {
            .stageFlags = pc.stages,
            .offset = pc.offset,
            .size = pc.size,
        });
    }

    VkPipelineLayoutCreateInfo pl_ci {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(setLayouts_.size()),
        .pSetLayouts = setLayouts_.data(),
        .pushConstantRangeCount = static_cast<std::uint32_t>(vk_pc.size()),
        .pPushConstantRanges = vk_pc.data(),
    };

    if (vkCreatePipelineLayout(device, &pl_ci, nullptr, &pipelineLayout_) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create pipeline layout");
        for (VkDescriptorSetLayout created : setLayouts_) {
            vkDestroyDescriptorSetLayout(device, created, nullptr);
        }
        setLayouts_.clear();
        for (auto &[_, inf] : setLayoutInfos_) {
            inf.layout = VK_NULL_HANDLE;
        }
    }
}

void ShaderReflection::destroy_layouts(VkDevice device) noexcept {
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    for (auto &[_, info] : setLayoutInfos_) {
        if (info.layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, info.layout, nullptr);
            info.layout = VK_NULL_HANDLE;
        }
    }
    setLayouts_.clear();
}

auto ShaderReflection::create_pools(VkDevice device,
                                    std::uint32_t maxSetsPerSet) const
    -> std::unordered_map<std::uint32_t, VkDescriptorPool> {
    std::unordered_map<std::uint32_t, VkDescriptorPool> pools;
    for (const auto &[set, info] : setLayoutInfos_) {
        if (info.poolSizes.empty()) {
            continue;
        }
        VkDescriptorPoolCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = maxSetsPerSet,
            .poolSizeCount = static_cast<std::uint32_t>(info.poolSizes.size()),
            .pPoolSizes = info.poolSizes.data(),
        };
        VkDescriptorPool pool {};
        if (vkCreateDescriptorPool(device, &ci, nullptr, &pool) != VK_SUCCESS) {
            LUMEN_LOG_ERROR("Failed to create descriptor pool for set");
            continue;
        }
        pools[set] = pool;
    }
    return pools;
}

auto ShaderReflection::create_descriptor_pool(VkDevice device,
                                              std::uint32_t maxSets) const
    -> VkDescriptorPool {
    std::vector<VkDescriptorPoolSize> sizes;
    for (const auto &[_, info] : setLayoutInfos_) {
        merge_pool_sizes_(sizes, info.poolSizes);
    }
    if (sizes.empty()) {
        return VK_NULL_HANDLE;
    }
    VkDescriptorPoolCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = maxSets,
        .poolSizeCount = static_cast<std::uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };
    VkDescriptorPool pool {};
    if (vkCreateDescriptorPool(device, &ci, nullptr, &pool) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create descriptor pool");
        return VK_NULL_HANDLE;
    }
    return pool;
}

[[nodiscard]] std::unordered_map<uint32_t, VkDescriptorSet>
ShaderReflection::allocateSets(
    VkDevice device,
    const std::unordered_map<uint32_t, VkDescriptorPool> &pools) const {

    std::unordered_map<uint32_t, VkDescriptorSet> sets;
    for (const auto &[set, info] : setLayoutInfos_) {

        auto poolIt = pools.find(set);
        if (poolIt == pools.end()) {
            continue;
        }

        VkDescriptorSetAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = poolIt->second,
            .descriptorSetCount = 1,
            .pSetLayouts = &info.layout
        };
        VkDescriptorSet ds;
        if (vkAllocateDescriptorSets(device, &ai, &ds) != VK_SUCCESS) {
            LUMEN_LOG_ERROR("Failed to allocate descriptor set");
            return {};
        }

        sets[set] = ds;
    }

    return sets;
}

VkPipelineVertexInputStateCreateInfo
ShaderReflection::create_vertex_input_state(uint32_t binding,
                                            VkVertexInputRate rate) const {
    static std::vector<VkVertexInputAttributeDescription> attrs;
    attrs.clear();

    for (const auto &attr : vertexAttributes_) {
        attrs.push_back({
            .location = attr.location,
            .binding = binding,
            .format = attr.format,
            .offset = attr.offset,
        });
    }

    static VkVertexInputBindingDescription bindingDesc {
        .binding = binding,
        .stride = 0, // 你可以后续按结构体填，或在校验时自动填
        .inputRate = rate,
    };

    VkPipelineVertexInputStateCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDesc,
        .vertexAttributeDescriptionCount = (uint32_t)attrs.size(),
        .pVertexAttributeDescriptions = attrs.data(),
    };

    return info;
}

bool ShaderReflection::validateVertexLayout(
    const std::vector<VertexMemberInfo> &runtimeMembers,
    std::optional<std::size_t> struct_byte_size) const {

    const auto &shader_attrs = vertexAttributes_;

    std::unordered_map<std::uint32_t, VertexMemberInfo> runtime_by_loc;
    runtime_by_loc.reserve(runtimeMembers.size());
    for (const auto &m : runtimeMembers) {
        const auto inserted = runtime_by_loc.insert({ m.location, m }).second;
        if (!inserted) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: duplicate location {} in runtime "
                "description",
                m.location);
            return false;
        }
    }

    std::unordered_map<std::uint32_t, VertexAttribute> shader_by_loc;
    shader_by_loc.reserve(shader_attrs.size());
    for (const auto &a : shader_attrs) {
        const auto inserted = shader_by_loc.insert({ a.location, a }).second;
        if (!inserted) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: duplicate location {} in shader "
                "reflection",
                a.location);
            return false;
        }
    }

    if (shader_by_loc.size() != runtime_by_loc.size()) {
        LUMEN_LOG_ERROR(
            "validateVertexLayout: attribute count mismatch (shader {} vs "
            "runtime {})",
            shader_by_loc.size(), runtime_by_loc.size());
        return false;
    }

    for (const auto &[loc, sh] : shader_by_loc) {
        const auto it = runtime_by_loc.find(loc);
        if (it == runtime_by_loc.end()) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: shader location {} has no matching "
                "runtime member",
                loc);
            return false;
        }
        const auto &rt = it->second;
        if (sh.format != rt.format) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: VkFormat mismatch at location {} "
                "(runtime member \"{}\")",
                loc, rt.name);
            return false;
        }
    }

    for (const auto &[loc, rt] : runtime_by_loc) {
        if (!shader_by_loc.contains(loc)) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: runtime location {} (member \"{}\") "
                "not declared in vertex shader",
                loc, rt.name);
            return false;
        }
    }

    std::vector<VertexMemberInfo> sorted;
    sorted.reserve(runtime_by_loc.size());
    for (const auto &p : runtime_by_loc) {
        sorted.push_back(p.second);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const VertexMemberInfo &a, const VertexMemberInfo &b) {
                  return a.location < b.location;
              });

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const auto ext = vertex_attribute_extent_bytes(sorted[i].format);
        if (ext == 0U) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: unsupported VkFormat at location {}",
                sorted[i].location);
            return false;
        }
        const auto end = sorted[i].offset + static_cast<std::size_t>(ext);
        if (struct_byte_size.has_value() && end > *struct_byte_size) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: member \"{}\" ends past struct size "
                "({} > {})",
                sorted[i].name, end, *struct_byte_size);
            return false;
        }
        if (i + 1 < sorted.size() && end > sorted[i + 1].offset) {
            LUMEN_LOG_ERROR(
                "validateVertexLayout: overlapping vertex members \"{}\" and "
                "\"{}\" (byte ranges collide; check padding/alignment)",
                sorted[i].name, sorted[i + 1].name);
            return false;
        }
    }

    LUMEN_LOG_INFO(
        "validateVertexLayout: OK — {} vertex attribute(s) match shader "
        "reflection",
        sorted.size());
    return true;
}

[[nodiscard]] LayoutKey ShaderReflection::makeLayoutKey(
    uint32_t set,
    const std::vector<VkDescriptorSetLayoutBinding> &bindings) const {
    LayoutKey key {};
    key.set = set;
    for (const auto &b : bindings) {
        uint8_t h[sizeof(b.binding) + sizeof(b.descriptorType) +
                  sizeof(b.descriptorCount)];
        std::memcpy(h, &b.binding, sizeof(b.binding));
        std::memcpy(h + 4, &b.descriptorType, sizeof(b.descriptorType));
        std::memcpy(h + 8, &b.descriptorCount, sizeof(b.descriptorCount));
        key.hashData.insert(key.hashData.end(), h, h + sizeof(h));
    }
    return key;
}

} // namespace vulkan::shader::reflection
