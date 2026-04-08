/**
 * @file shader_reflection.cpp
 * @brief SPIRV-Reflect 解析、`PipelineLayoutBuilder` 与描述符池 / write
 * 头初始化。
 */

#include "vulkan/shader_reflection.hpp"

#include "core/log/logger.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>

#include <spirv-reflect/spirv_reflect.h>

namespace vulkan {

namespace {

[[nodiscard]] std::optional<VkDescriptorType>
spv_reflect_descriptor_type_to_vk(SpvReflectDescriptorType reflectType) {
    switch (reflectType) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    default: return std::nullopt;
    }
}

[[nodiscard]] std::uint32_t align_up_u32(std::uint32_t value,
                                         std::uint32_t alignment) {
    if (alignment == 0U) {
        return value;
    }
    return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] VkFormat spv_reflect_vertex_format_to_vk(SpvReflectFormat sf) {
    switch (sf) {
    case SPV_REFLECT_FORMAT_UNDEFINED: return VK_FORMAT_UNDEFINED;

    case SPV_REFLECT_FORMAT_R16_UINT: return VK_FORMAT_R16_UINT;
    case SPV_REFLECT_FORMAT_R16_SINT: return VK_FORMAT_R16_SINT;
    case SPV_REFLECT_FORMAT_R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;

    case SPV_REFLECT_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
    case SPV_REFLECT_FORMAT_R32_SINT: return VK_FORMAT_R32_SINT;
    case SPV_REFLECT_FORMAT_R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;

    case SPV_REFLECT_FORMAT_R32G32_UINT: return VK_FORMAT_R32G32_UINT;
    case SPV_REFLECT_FORMAT_R32G32_SINT: return VK_FORMAT_R32G32_SINT;
    case SPV_REFLECT_FORMAT_R32G32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;

    case SPV_REFLECT_FORMAT_R32G32B32_UINT: return VK_FORMAT_R32G32B32_UINT;
    case SPV_REFLECT_FORMAT_R32G32B32_SINT: return VK_FORMAT_R32G32B32_SINT;
    case SPV_REFLECT_FORMAT_R32G32B32_SFLOAT: return VK_FORMAT_R32G32B32_SFLOAT;

    case SPV_REFLECT_FORMAT_R32G32B32A32_UINT:
        return VK_FORMAT_R32G32B32A32_UINT;
    case SPV_REFLECT_FORMAT_R32G32B32A32_SINT:
        return VK_FORMAT_R32G32B32A32_SINT;
    case SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;

    default: return VK_FORMAT_UNDEFINED;
    }
}

[[nodiscard]] std::uint32_t vk_vertex_format_total_size_bytes(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: return 2;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT: return 4;
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT: return 8;
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_SFLOAT: return 12;
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    default: return 0;
    }
}

[[nodiscard]] std::uint32_t
vk_vertex_format_component_alignment_bytes(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: return 2;
    default: return 4;
    }
}

[[nodiscard]] bool ranges_byte_overlap(std::uint32_t offA, std::uint32_t sizeA,
                                       std::uint32_t offB, std::uint32_t sizeB) {
    const std::uint64_t endA = static_cast<std::uint64_t>(offA) + sizeA;
    const std::uint64_t endB = static_cast<std::uint64_t>(offB) + sizeB;
    return static_cast<std::uint64_t>(offA) < endB &&
           static_cast<std::uint64_t>(offB) < endA;
}

[[nodiscard]] std::expected<void, std::string>
reflect_from_module(SpvReflectShaderModule &mod, VkShaderStageFlagBits stage,
                     ShaderReflection &outReflection) {
    outReflection.bindings.clear();
    outReflection.pushConstants.clear();
    outReflection.vertexInputs.clear();

    if (static_cast<std::uint32_t>(mod.shader_stage) !=
        static_cast<std::uint32_t>(stage)) {
        return std::unexpected(std::string(
            "reflect_spirv: SPIR-V module stage does not match stage "
            "argument"));
    }

    const auto vkStage = static_cast<VkShaderStageFlags>(stage);

    std::uint32_t bindCount { 0 };
    spvReflectEnumerateDescriptorBindings(&mod, &bindCount, nullptr);
    std::vector<SpvReflectDescriptorBinding *> descriptorBindings(bindCount);
    if (bindCount > 0) {
        spvReflectEnumerateDescriptorBindings(&mod, &bindCount,
                                              descriptorBindings.data());
    }

    for (SpvReflectDescriptorBinding *descriptorBinding : descriptorBindings) {
        if (descriptorBinding == nullptr) {
            continue;
        }
        const auto maybeVk = spv_reflect_descriptor_type_to_vk(
            descriptorBinding->descriptor_type);
        if (!maybeVk.has_value()) {
            return std::unexpected(
                std::string("unsupported SpvReflectDescriptorType for "
                            "binding ") +
                (descriptorBinding->name != nullptr ? descriptorBinding->name
                                                    : ""));
        }
        DescriptorBindingInfo info {};
        info.set = descriptorBinding->set;
        info.binding = descriptorBinding->binding;
        info.type = *maybeVk;
        info.count = descriptorBinding->count;
        info.stageFlags = vkStage;
        if (descriptorBinding->name != nullptr) {
            info.name = descriptorBinding->name;
        }
        outReflection.bindings.push_back(std::move(info));
    }

    bindCount = 0;
    spvReflectEnumeratePushConstantBlocks(&mod, &bindCount, nullptr);
    std::vector<SpvReflectBlockVariable *> pushBlocks(bindCount);
    if (bindCount > 0) {
        spvReflectEnumeratePushConstantBlocks(&mod, &bindCount,
                                              pushBlocks.data());
    }
    for (SpvReflectBlockVariable *pushVar : pushBlocks) {
        if (pushVar == nullptr) {
            continue;
        }
        PushConstantInfo pci {};
        pci.offset = pushVar->offset;
        pci.size = pushVar->size;
        pci.stageFlags = vkStage;
        outReflection.pushConstants.push_back(pci);
    }

    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        bindCount = 0;
        spvReflectEnumerateInputVariables(&mod, &bindCount, nullptr);
        std::vector<SpvReflectInterfaceVariable *> inputVars(bindCount);
        if (bindCount > 0) {
            spvReflectEnumerateInputVariables(&mod, &bindCount,
                                              inputVars.data());
        }
        for (SpvReflectInterfaceVariable *inputVar : inputVars) {
            if (inputVar == nullptr || inputVar->name == nullptr) {
                continue;
            }
            if (std::strncmp(inputVar->name, "gl_", 3) == 0) {
                continue;
            }
            const VkFormat vertexFormat =
                spv_reflect_vertex_format_to_vk(inputVar->format);
            if (vertexFormat == VK_FORMAT_UNDEFINED) {
                return std::unexpected(
                    std::string("unsupported vertex SpvReflectFormat for "
                                "input ") +
                    inputVar->name);
            }
            VertexInputInfo vi {};
            vi.location = inputVar->location;
            vi.format = vertexFormat;
            vi.name = inputVar->name;
            outReflection.vertexInputs.push_back(std::move(vi));
        }
    }

    return {};
}

} // namespace

std::expected<ShaderReflection, std::string>
reflect_spirv(const std::vector<std::uint32_t> &spirvWords,
              const VkShaderStageFlagBits stage) {
    if (spirvWords.empty()) {
        return std::unexpected(std::string("reflect_spirv: empty SPIR-V"));
    }

    SpvReflectShaderModule mod {};
    const SpvReflectResult createRc = spvReflectCreateShaderModule(
        spirvWords.size() * sizeof(std::uint32_t), spirvWords.data(), &mod);
    if (createRc != SPV_REFLECT_RESULT_SUCCESS) {
        return std::unexpected(
            std::string("reflect_spirv: spvReflectCreateShaderModule failed "
                        "(code ") +
            std::to_string(static_cast<int>(createRc)) + ")");
    }

    ShaderReflection outReflection {};
    if (auto reflectResult = reflect_from_module(mod, stage, outReflection);
        !reflectResult.has_value()) {
        spvReflectDestroyShaderModule(&mod);
        return std::unexpected(reflectResult.error());
    }
    spvReflectDestroyShaderModule(&mod);
    return outReflection;
}

std::expected<VertexInputState, std::string>
build_vertex_input_state(const ShaderReflection &vertexReflection,
                         const std::uint32_t vertexBindingSlot) {
    VertexInputState state {};

    std::vector<const VertexInputInfo *> sortedPtrs;
    sortedPtrs.reserve(vertexReflection.vertexInputs.size());
    for (const auto &vi : vertexReflection.vertexInputs) {
        sortedPtrs.push_back(&vi);
    }
    std::ranges::sort(sortedPtrs,
                      [](const VertexInputInfo *a, const VertexInputInfo *b) {
                          return a->location < b->location;
                      });

    std::uint32_t offsetBytes { 0 };
    for (const VertexInputInfo *inputPtr : sortedPtrs) {
        const VkFormat vertexFormat = inputPtr->format;
        const std::uint32_t componentAlign =
            vk_vertex_format_component_alignment_bytes(vertexFormat);
        const std::uint32_t attributeSize =
            vk_vertex_format_total_size_bytes(vertexFormat);
        if (attributeSize == 0) {
            return std::unexpected(
                std::string("build_vertex_input_state: unsupported vertex "
                            "format size"));
        }
        offsetBytes = align_up_u32(offsetBytes, componentAlign);
        VkVertexInputAttributeDescription attr {};
        attr.location = inputPtr->location;
        attr.binding = vertexBindingSlot;
        attr.format = vertexFormat;
        attr.offset = offsetBytes;
        state.attributes.push_back(attr);
        offsetBytes += attributeSize;
    }

    if (state.attributes.empty()) {
        return std::unexpected(
            std::string("build_vertex_input_state: no vertex inputs"));
    }

    const std::uint32_t stride = align_up_u32(offsetBytes, 4U);
    state.stride = stride;

    VkVertexInputBindingDescription bindingDesc {};
    bindingDesc.binding = vertexBindingSlot;
    bindingDesc.stride = stride;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    state.bindings.push_back(bindingDesc);
    return state;
}

void PipelineLayoutBuilder::clear() noexcept {
    mergedBindings_.clear();
    mergedPushConstants_.clear();
}

std::expected<void, std::string>
PipelineLayoutBuilder::add(const ShaderReflection &reflection) {
    for (const auto &binding : reflection.bindings) {
        const BindingKey key { .set = binding.set, .binding = binding.binding };
        const auto existingIt = mergedBindings_.find(key);
        if (existingIt == mergedBindings_.end()) {
            mergedBindings_.insert({ key, binding });
        } else {
            if (existingIt->second.type != binding.type) {
                return std::unexpected(
                    std::string("PipelineLayoutBuilder: descriptor type mismatch "
                                "for (set=") +
                    std::to_string(key.set) +
                    ", binding=" + std::to_string(key.binding) + ")");
            }
            existingIt->second.stageFlags |= binding.stageFlags;
            existingIt->second.count =
                std::max(existingIt->second.count, binding.count);
            if (existingIt->second.name.empty() && !binding.name.empty()) {
                existingIt->second.name = binding.name;
            }
        }
    }

    for (const auto &pc : reflection.pushConstants) {
        auto sameRange = std::ranges::find_if(
            mergedPushConstants_, [&](const PushConstantInfo &existing) {
                return existing.offset == pc.offset && existing.size == pc.size;
            });
        if (sameRange != mergedPushConstants_.end()) {
            sameRange->stageFlags |= pc.stageFlags;
            continue;
        }
        for (const auto &existing : mergedPushConstants_) {
            if (ranges_byte_overlap(existing.offset, existing.size, pc.offset,
                                    pc.size)) {
                return std::unexpected(
                    std::string("PipelineLayoutBuilder: overlapping push "
                                "constant ranges with different (offset,size)"));
            }
        }
        mergedPushConstants_.push_back(pc);
    }

    std::ranges::sort(mergedPushConstants_,
                      [](const PushConstantInfo &a, const PushConstantInfo &b) {
                          return a.offset < b.offset;
                      });

    return {};
}

std::expected<std::vector<VkDescriptorSetLayout>, std::string>
PipelineLayoutBuilder::create_descriptor_set_layouts(
    const VkDevice device) const {
    std::vector<VkDescriptorSetLayout> layouts;
    if (mergedBindings_.empty()) {
        return layouts;
    }

    std::uint32_t maxSet { 0 };
    for (const auto &[key, _] : mergedBindings_) {
        maxSet = std::max(maxSet, key.set);
    }

    std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>>
        bindingsBySet;
    for (const auto &[key, mergedBinding] : mergedBindings_) {
        VkDescriptorSetLayoutBinding layoutBinding {};
        layoutBinding.binding = mergedBinding.binding;
        layoutBinding.descriptorType = mergedBinding.type;
        layoutBinding.descriptorCount = mergedBinding.count;
        layoutBinding.stageFlags = mergedBinding.stageFlags;
        layoutBinding.pImmutableSamplers = nullptr;
        bindingsBySet[key.set].push_back(layoutBinding);
    }
    for (auto &entry : bindingsBySet) {
        std::ranges::sort(entry.second,
                           [](const VkDescriptorSetLayoutBinding &a,
                              const VkDescriptorSetLayoutBinding &b) {
                               return a.binding < b.binding;
                           });
    }

    layouts.assign(static_cast<std::size_t>(maxSet) + 1U, VK_NULL_HANDLE);

    for (std::uint32_t setIndex { 0 }; setIndex <= maxSet; ++setIndex) {
        VkDescriptorSetLayoutCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        const auto setIt = bindingsBySet.find(setIndex);
        if (setIt != bindingsBySet.end()) {
            createInfo.bindingCount =
                static_cast<std::uint32_t>(setIt->second.size());
            createInfo.pBindings = setIt->second.data();
        }
        if (vkCreateDescriptorSetLayout(device, &createInfo, nullptr,
                                        &layouts[setIndex]) != VK_SUCCESS) {
            LUMEN_LOG_ERROR(
                "PipelineLayoutBuilder: vkCreateDescriptorSetLayout failed");
            for (std::uint32_t k { 0 }; k < setIndex; ++k) {
                if (layouts[k] != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, layouts[k], nullptr);
                    layouts[k] = VK_NULL_HANDLE;
                }
            }
            layouts.clear();
            return std::unexpected(
                std::string("PipelineLayoutBuilder: vkCreateDescriptorSetLayout "
                            "failed"));
        }
    }
    return layouts;
}

std::expected<VkPipelineLayout, std::string>
PipelineLayoutBuilder::create_pipeline_layout(
    const VkDevice device,
    const std::vector<VkDescriptorSetLayout> &setLayouts) const {
    std::vector<VkPushConstantRange> ranges;
    ranges.reserve(mergedPushConstants_.size());
    for (const auto &pc : mergedPushConstants_) {
        VkPushConstantRange range {};
        range.stageFlags = pc.stageFlags;
        range.offset = pc.offset;
        range.size = pc.size;
        ranges.push_back(range);
    }

    VkPipelineLayoutCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    createInfo.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
    createInfo.pSetLayouts = setLayouts.data();
    createInfo.pushConstantRangeCount = static_cast<std::uint32_t>(ranges.size());
    createInfo.pPushConstantRanges = ranges.empty() ? nullptr : ranges.data();

    VkPipelineLayout pipelineLayout { VK_NULL_HANDLE };
    if (vkCreatePipelineLayout(device, &createInfo, nullptr, &pipelineLayout) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("PipelineLayoutBuilder: vkCreatePipelineLayout failed");
        return std::unexpected(
            std::string("PipelineLayoutBuilder: vkCreatePipelineLayout failed"));
    }
    return pipelineLayout;
}

void PipelineLayoutBuilder::destroy_descriptor_set_layouts(
    const VkDevice device, const std::vector<VkDescriptorSetLayout> &layouts) {
    for (VkDescriptorSetLayout layout : layouts) {
        if (layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
    }
}

std::expected<VkDescriptorPool, std::string>
create_descriptor_pool_for_merged_bindings(
    const VkDevice device,
    const std::unordered_map<BindingKey, DescriptorBindingInfo, BindingKeyHash>
        &merged,
    const std::uint32_t maxSets) {
    if (maxSets == 0U) {
        LUMEN_LOG_ERROR(
            "create_descriptor_pool_for_merged_bindings: max_sets must be > 0");
        return std::unexpected(
            std::string(
                "create_descriptor_pool_for_merged_bindings: max_sets must be > "
                "0"));
    }
    if (merged.empty()) {
        return VkDescriptorPool { VK_NULL_HANDLE };
    }

    std::map<VkDescriptorType, std::uint32_t> typeCounts;
    for (const auto &[_, binding] : merged) {
        typeCounts[binding.type] += binding.count * maxSets;
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(typeCounts.size());
    for (const auto &[descriptorType, count] : typeCounts) {
        VkDescriptorPoolSize poolSize {};
        poolSize.type = descriptorType;
        poolSize.descriptorCount = count;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };
    poolCreateInfo.maxSets = maxSets;
    poolCreateInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.empty() ? nullptr : poolSizes.data();

    if (poolSizes.empty()) {
        poolCreateInfo.poolSizeCount = 0;
    }

    VkDescriptorPool pool { VK_NULL_HANDLE };
    if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &pool) !=
        VK_SUCCESS) {
        LUMEN_LOG_ERROR("create_descriptor_pool_for_merged_bindings: "
                        "vkCreateDescriptorPool failed");
        return std::unexpected(
            std::string("create_descriptor_pool_for_merged_bindings: "
                        "vkCreateDescriptorPool failed"));
    }
    return pool;
}

void init_write_descriptor_set(VkWriteDescriptorSet &write,
                               const VkDescriptorSet dstSet,
                               const DescriptorBindingInfo &binding) {
    write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dstSet;
    write.dstBinding = binding.binding;
    write.dstArrayElement = 0;
    write.descriptorCount = binding.count;
    write.descriptorType = binding.type;
}

} // namespace vulkan
