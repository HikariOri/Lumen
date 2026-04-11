#include "shader/reflection/spirv_reflect.hpp"

#include "core/log/logger.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include <spirv-reflect/spirv_reflect.h>

#ifndef NDEBUG
#define SHADER_REFLECTION_LOG_DEBUG(Msg)                                       \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine()) {                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Debug),  \
                (Msg));                                                        \
        }                                                                      \
    } while (0)
#define SHADER_REFLECTION_LOG_ERROR(Msg)                                       \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine()) {                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Error),  \
                (Msg));                                                        \
        }                                                                      \
    } while (0)
#else
#define SHADER_REFLECTION_LOG_DEBUG(Msg) ((void)0)
#define SHADER_REFLECTION_LOG_ERROR(Msg) ((void)0)
#endif

namespace shader::reflection {

namespace {

[[nodiscard]] constexpr std::uint32_t
align_up_u32(const std::uint32_t x, const std::uint32_t a) noexcept {
    return (x + a - 1U) / a * a;
}

struct DescriptorBindingKey {
    std::uint32_t set {};
    std::uint32_t binding {};

    constexpr friend bool operator<(DescriptorBindingKey a,
                                    DescriptorBindingKey b) noexcept {
        if (a.set != b.set) {
            return a.set < b.set;
        }
        return a.binding < b.binding;
    }
};

[[nodiscard]] const char *resource_type_label(ResourceType t) noexcept {
    switch (t) {
    case ResourceType::UniformBuffer: return "uniform_buffer";
    case ResourceType::StorageBuffer: return "storage_buffer";
    case ResourceType::SampledImage: return "sampled_image";
    case ResourceType::StorageImage: return "storage_image";
    case ResourceType::Sampler: return "sampler";
    case ResourceType::CombinedImageSampler: return "combined_image_sampler";
    case ResourceType::UniformTexelBuffer: return "uniform_texel_buffer";
    case ResourceType::StorageTexelBuffer: return "storage_texel_buffer";
    case ResourceType::UniformBufferDynamic: return "uniform_buffer_dynamic";
    case ResourceType::StorageBufferDynamic: return "storage_buffer_dynamic";
    case ResourceType::InputAttachment: return "input_attachment";
    case ResourceType::AccelerationStructureKHR:
        return "acceleration_structure_khr";
    case ResourceType::Unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] ShaderResource merge_shader_resources(const ShaderResource &dst,
                                                    const ShaderResource &res) {
    ShaderResource out = dst;
    out.count = std::max(dst.count, res.count);
    out.size = std::max(dst.size, res.size);
    out.offset = std::min(dst.offset, res.offset);
    if (!res.name.empty()) {
        out.name = res.name;
    }
    if (res.type != ResourceType::Unknown) {
        out.type = res.type;
    }
    out.stages = dst.stages;
    return out;
}

void append_or_merge_push_constant(std::vector<PushConstant> &out,
                                   const PushConstant &pc) {
    for (PushConstant &existing : out) {
        if (existing.offset == pc.offset && existing.size == pc.size) {
            existing.stages |= pc.stages;
            return;
        }
    }
    out.push_back(pc);
}

[[nodiscard]] std::expected<std::vector<ShaderResource>, ReflectionError>
merge_descriptor_resources(std::span<const ShaderReflection> shaders) {
    std::map<DescriptorBindingKey, ShaderResource> resource_by_key;
    for (const ShaderReflection &shader : shaders) {
        const auto stage_bits = static_cast<VkShaderStageFlags>(shader.stage());
        for (const ShaderResource &res : shader.resources()) {
            const DescriptorBindingKey key { res.set, res.binding };
            ShaderResource res_cleared = res;
            res_cleared.stages = 0;
            auto [it, inserted] = resource_by_key.try_emplace(key, res_cleared);
            ShaderResource &dst = it->second;
            if (!inserted) {
                if (dst.type != ResourceType::Unknown &&
                    res.type != ResourceType::Unknown && dst.type != res.type) {
                    return std::unexpected(
                        std::string { "descriptor merge: set " } +
                        std::to_string(key.set) + ", binding " +
                        std::to_string(key.binding) +
                        ": conflicting ResourceType " +
                        resource_type_label(dst.type) + " vs " +
                        resource_type_label(res.type));
                }
                dst = merge_shader_resources(dst, res);
            }
            dst.stages |= stage_bits;
        }
    }
    std::vector<ShaderResource> out;
    out.reserve(resource_by_key.size());
    for (const auto &entry : resource_by_key) {
        out.push_back(entry.second);
    }
    return out;
}

[[nodiscard]] std::expected<VkDescriptorSetLayout, ReflectionError>
create_descriptor_set_layout_from_bindings(
    VkDevice device, const std::vector<VkDescriptorSetLayoutBinding> &bindings,
    const VkAllocationCallbacks *allocator) {
    VkDescriptorSetLayoutCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult r =
        vkCreateDescriptorSetLayout(device, &ci, allocator, &layout);
    if (r != VK_SUCCESS) {
        return std::unexpected(
            std::string { "vkCreateDescriptorSetLayout failed, VkResult=" } +
            std::to_string(static_cast<int>(r)));
    }
    return layout;
}

} // namespace

// -----------------------------------------------------------------------------
// 阶段字符串、合并反射、顶点 / 描述符 / 管线布局（实现顺序依依赖关系排列）
// -----------------------------------------------------------------------------

namespace detail {

std::string shader_stage_flags_to_string(const VkShaderStageFlags v) {
    std::string out;
    auto push = [&](const char *name) {
        if (!out.empty()) {
            out += '|';
        }
        out += name;
    };
    if ((v & VK_SHADER_STAGE_VERTEX_BIT) != 0U) {
        push("vertex");
    }
    if ((v & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) != 0U) {
        push("tessellation_control");
    }
    if ((v & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) != 0U) {
        push("tessellation_evaluation");
    }
    if ((v & VK_SHADER_STAGE_GEOMETRY_BIT) != 0U) {
        push("geometry");
    }
    if ((v & VK_SHADER_STAGE_FRAGMENT_BIT) != 0U) {
        push("fragment");
    }
    if ((v & VK_SHADER_STAGE_COMPUTE_BIT) != 0U) {
        push("compute");
    }
    if (k_task_shader_stage_bit != VkShaderStageFlags { 0 } &&
        (v & k_task_shader_stage_bit) != 0U) {
        push("task");
    }
    if (k_mesh_shader_stage_bit != VkShaderStageFlags { 0 } &&
        (v & k_mesh_shader_stage_bit) != 0U) {
        push("mesh");
    }
    if ((v & VK_SHADER_STAGE_RAYGEN_BIT_KHR) != 0U) {
        push("raygen");
    }
    if ((v & VK_SHADER_STAGE_ANY_HIT_BIT_KHR) != 0U) {
        push("any_hit");
    }
    if ((v & VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) != 0U) {
        push("closest_hit");
    }
    if ((v & VK_SHADER_STAGE_MISS_BIT_KHR) != 0U) {
        push("miss");
    }
    if ((v & VK_SHADER_STAGE_INTERSECTION_BIT_KHR) != 0U) {
        push("intersection");
    }
    if ((v & VK_SHADER_STAGE_CALLABLE_BIT_KHR) != 0U) {
        push("callable");
    }
    return out;
}

} // namespace detail

[[nodiscard]] static std::vector<VertexBinding> default_vertex_bindings() {
    return { VertexBinding { 0U, 0U, VK_VERTEX_INPUT_RATE_VERTEX },
             VertexBinding { 1U, 0U, VK_VERTEX_INPUT_RATE_INSTANCE } };
}

[[nodiscard]] static VertexAttribute
vertex_attribute_from_shader_input(const ShaderInput &in,
                                   const uint32_t binding) noexcept {
    return VertexAttribute { in.location, in.format, binding };
}

[[nodiscard]] static std::expected<VkDescriptorType, ReflectionError>
resource_type_to_vk_descriptor_type(ResourceType t) {
    switch (t) {
    case ResourceType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ResourceType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ResourceType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ResourceType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ResourceType::CombinedImageSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case ResourceType::UniformTexelBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case ResourceType::StorageTexelBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case ResourceType::UniformBufferDynamic:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    case ResourceType::StorageBufferDynamic:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    case ResourceType::InputAttachment:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    case ResourceType::AccelerationStructureKHR:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    case ResourceType::Unknown:
    default:
        return std::unexpected(std::string {
            "resource_type_to_vk_descriptor_type: Unknown ResourceType" });
    }
}

[[nodiscard]] static std::expected<VkDescriptorType, ReflectionError>
vk_descriptor_type_for_merged_resource(const std::uint32_t set_index,
                                       ResourceType resource_type) {
    const auto base = resource_type_to_vk_descriptor_type(resource_type);
    if (!base) {
        return base;
    }
    VkDescriptorType dt = *base;
    if (set_index <= 1U && dt == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    return dt;
}

namespace {

[[nodiscard]] std::expected<
    std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>>,
    ReflectionError>
descriptor_bindings_by_set_impl(const MergedShaderReflection &merged) {
    std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>> by_set;
    for (const ShaderResource &res : merged.resources()) {
        const auto dt =
            vk_descriptor_type_for_merged_resource(res.set, res.type);
        if (!dt) {
            return std::unexpected(dt.error());
        }
        VkDescriptorSetLayoutBinding b {};
        b.binding = res.binding;
        b.descriptorType = *dt;
        b.descriptorCount = std::max(1U, res.count);
        b.stageFlags = res.stages;
        b.pImmutableSamplers = nullptr;
        by_set[res.set].push_back(b);
    }
    for (auto &entry : by_set) {
        auto &bindings = entry.second;
        std::sort(bindings.begin(), bindings.end(),
                  [](const VkDescriptorSetLayoutBinding &a,
                     const VkDescriptorSetLayoutBinding &b) {
                      return a.binding < b.binding;
                  });
        for (std::size_t i = 1U; i < bindings.size(); ++i) {
            if (bindings[i].binding == bindings[i - 1U].binding) {
                return std::unexpected(std::string {
                    "descriptor_bindings_by_set_impl: duplicate binding "
                    "in merged.resources (merge bug?)" });
            }
        }
    }
    return by_set;
}

} // namespace

void MergedShaderReflection::set_vulkan_device(
    const VkDevice device, const VkAllocationCallbacks *allocator) {
    destroy();
    vkDevice_ = device;
    vkAllocator_ = allocator;
}

[[nodiscard]] std::expected<MergedShaderReflection, ReflectionError>
MergedShaderReflection::merge(const std::vector<ShaderReflection> &shaders,
                              const VkDevice device,
                              const VkAllocationCallbacks *allocator) {
    MergedShaderReflection out {};

    VkShaderStageFlags combined_stages = 0U;
    for (const ShaderReflection &s : shaders) {
        combined_stages |= static_cast<VkShaderStageFlags>(s.stage());
    }
    out.stages_ = combined_stages;

    const auto merged_resources = merge_descriptor_resources(shaders);
    if (!merged_resources) {
        return std::unexpected(merged_resources.error());
    }
    out.resources_ = *merged_resources;

    for (const ShaderReflection &s : shaders) {
        const auto bits = static_cast<VkShaderStageFlags>(s.stage());
        if ((bits & VK_SHADER_STAGE_VERTEX_BIT) != 0U) {
            out.vertexInput_ = s.inputs();
            break;
        }
    }

    for (const ShaderReflection &s : shaders) {
        const auto bits = static_cast<VkShaderStageFlags>(s.stage());
        if ((bits & VK_SHADER_STAGE_FRAGMENT_BIT) != 0U) {
            out.fragmentOutput_ = s.outputs();
            break;
        }
    }

    for (const ShaderReflection &s : shaders) {
        for (const PushConstant &pc : s.pushConstants()) {
            append_or_merge_push_constant(out.pushConstants_, pc);
        }
    }

    out.descriptorBindingsBySetCache_.emplace(
        descriptor_bindings_by_set_impl(out));
    if (!*out.descriptorBindingsBySetCache_) {
        return std::unexpected(out.descriptorBindingsBySetCache_->error());
    }

    out.rebuild_push_constant_ranges_for_pipeline();

    {
        std::string log_line =
            "spirv_reflect: MergedShaderReflection::merge stage ";
        log_line += detail::shader_stage_flags_to_string(out.stages_);
        log_line += " resources ";
        log_line += std::to_string(out.resources_.size());
        log_line += " vertex_input ";
        log_line += std::to_string(out.vertexInput_.size());
        log_line += " fragment_output ";
        log_line += std::to_string(out.fragmentOutput_.size());
        log_line += " push_constant ";
        log_line += std::to_string(out.pushConstants_.size());
        SHADER_REFLECTION_LOG_DEBUG(log_line);
    }

    out.vkDevice_ = device;
    out.vkAllocator_ = allocator;

    out.create_pipeline_layout();
    out.create_descriptor_set_layouts();
    out.packed_vertex_input_state();

    return out;
}

[[nodiscard]] std::expected<std::vector<VkDescriptorSetLayoutBinding>,
                            ReflectionError>
MergedShaderReflection::descriptor_bindings_for_set(
    const std::uint32_t set_index) const {
    if (!descriptorBindingsBySetCache_.has_value()) {
        descriptorBindingsBySetCache_.emplace(
            descriptor_bindings_by_set_impl(*this));
    }
    const auto &cached = *descriptorBindingsBySetCache_;
    if (!cached) {
        return std::unexpected(cached.error());
    }
    const auto it = cached->find(set_index);
    if (it == cached->end()) {
        return std::vector<VkDescriptorSetLayoutBinding> {};
    }
    return it->second;
}

[[nodiscard]] std::expected<VkDescriptorSetLayout, ReflectionError>
MergedShaderReflection::create_descriptor_set_layout_for_set_internal(
    VkDevice device, const std::uint32_t set_index,
    const VkAllocationCallbacks *allocator) const {
    const auto bindings = descriptor_bindings_for_set(set_index);
    if (!bindings) {
        return std::unexpected(bindings.error());
    }
    return create_descriptor_set_layout_from_bindings(device, *bindings,
                                                      allocator);
}

void MergedShaderReflection::destroy() const noexcept {
    if (descriptorSetLayoutsDevice_ != VK_NULL_HANDLE) {
        for (VkDescriptorSetLayout h : descriptorSetLayouts_) {
            if (h != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(descriptorSetLayoutsDevice_, h,
                                             descriptorSetLayoutsAllocator_);
            }
        }
    }
    descriptorSetLayouts_.clear();
    descriptorSetLayoutsDevice_ = VK_NULL_HANDLE;
    descriptorSetLayoutsAllocator_ = nullptr;
}

[[nodiscard]] std::expected<void, ReflectionError>
MergedShaderReflection::rebuild_descriptor_set_layouts_storage(
    VkDevice device, const VkAllocationCallbacks *allocator) const {
    destroy();
    if (resources_.empty()) {
        return {};
    }
    std::uint32_t max_set = 0U;
    for (const ShaderResource &res : resources_) {
        max_set = std::max(max_set, res.set);
    }
    std::vector<bool> seen(static_cast<std::size_t>(max_set) + 1U, false);
    for (const ShaderResource &res : resources_) {
        seen[res.set] = true;
    }
    for (std::uint32_t s = 0U; s <= max_set; ++s) {
        if (!seen[static_cast<std::size_t>(s)]) {
            return std::unexpected(std::format(
                "create_descriptor_set_layouts: descriptor sets must be "
                "contiguous from 0 to {}; missing set {}",
                max_set, s));
        }
    }
    descriptorSetLayouts_.reserve(static_cast<std::size_t>(max_set) + 1U);
    for (std::uint32_t s = 0U; s <= max_set; ++s) {
        const auto layout =
            create_descriptor_set_layout_for_set_internal(device, s, allocator);
        if (!layout) {
            for (VkDescriptorSetLayout h : descriptorSetLayouts_) {
                if (h != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, h, allocator);
                }
            }
            descriptorSetLayouts_.clear();
            descriptorSetLayoutsDevice_ = VK_NULL_HANDLE;
            descriptorSetLayoutsAllocator_ = nullptr;
            return std::unexpected(layout.error());
        }
        descriptorSetLayouts_.push_back(*layout);
    }
    descriptorSetLayoutsDevice_ = device;
    descriptorSetLayoutsAllocator_ = allocator;
    return {};
}

[[nodiscard]] std::expected<std::span<const VkDescriptorSetLayout>,
                            ReflectionError>
MergedShaderReflection::create_descriptor_set_layouts(
    const VkAllocationCallbacks *allocator) const {
    const VkDevice device = vkDevice_;
    const VkAllocationCallbacks *const alloc =
        allocator != nullptr ? allocator : vkAllocator_;
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string {
            "create_descriptor_set_layouts: VkDevice not set; use "
            "merge(..., device) or set_vulkan_device(device)" });
    }
    if (const auto r = rebuild_descriptor_set_layouts_storage(device, alloc);
        !r) {
        return std::unexpected(r.error());
    }
    return std::span<const VkDescriptorSetLayout>(descriptorSetLayouts_.data(),
                                                  descriptorSetLayouts_.size());
}

void MergedShaderReflection::rebuild_push_constant_ranges_for_pipeline() {
    pushConstantRangesForPipeline_.clear();
    pushConstantRangesForPipeline_.reserve(pushConstants_.size());
    for (const PushConstant &pc : pushConstants_) {
        if (pc.size == 0U) {
            continue;
        }
        VkPushConstantRange r {};
        r.stageFlags = pc.stages;
        r.offset = pc.offset;
        r.size = pc.size;
        pushConstantRangesForPipeline_.push_back(r);
    }
}

[[nodiscard]] std::expected<VkPipelineLayout, ReflectionError>
MergedShaderReflection::create_pipeline_layout(
    const VkAllocationCallbacks *allocator) const {
    const VkDevice device = vkDevice_;
    const VkAllocationCallbacks *const alloc =
        allocator != nullptr ? allocator : vkAllocator_;
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string { "create_pipeline_layout: VkDevice not set; use "
                          "merge(..., device) or set_vulkan_device(device)" });
    }
    if (!resources_.empty()) {
        const bool layouts_ready = !descriptorSetLayouts_.empty() &&
                                   descriptorSetLayoutsDevice_ == device &&
                                   descriptorSetLayoutsAllocator_ == alloc;
        if (!layouts_ready) {
            if (const auto br =
                    rebuild_descriptor_set_layouts_storage(device, alloc);
                !br) {
                return std::unexpected(br.error());
            }
        }
    } else {
        destroy();
    }

    VkPipelineLayoutCreateInfo pl {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount =
        static_cast<std::uint32_t>(descriptorSetLayouts_.size());
    pl.pSetLayouts =
        descriptorSetLayouts_.empty() ? nullptr : descriptorSetLayouts_.data();
    pl.pushConstantRangeCount =
        static_cast<std::uint32_t>(pushConstantRangesForPipeline_.size());
    pl.pPushConstantRanges = pushConstantRangesForPipeline_.empty()
                                 ? nullptr
                                 : pushConstantRangesForPipeline_.data();

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    const VkResult pr =
        vkCreatePipelineLayout(device, &pl, alloc, &pipeline_layout);
    if (pr != VK_SUCCESS) {
        SHADER_REFLECTION_LOG_ERROR(std::format(
            "spirv_reflect: vkCreatePipelineLayout failed set_layouts {} "
            "push_ranges {} VkResult {}",
            descriptorSetLayouts_.size(), pushConstantRangesForPipeline_.size(),
            static_cast<int>(pr)));
        return std::unexpected(
            std::string { "vkCreatePipelineLayout failed, VkResult=" } +
            std::to_string(static_cast<int>(pr)));
    }
    return pipeline_layout;
}

// --- SPIR-V / VkFormat 转换（供 ShaderReflection::from_spirv
// 与顶点打包使用）---

[[nodiscard]] static ResourceType
convert_descriptor_type(SpvReflectDescriptorType t) noexcept {
    switch (t) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return ResourceType::Sampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return ResourceType::CombinedImageSampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return ResourceType::SampledImage;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return ResourceType::StorageImage;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return ResourceType::UniformTexelBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return ResourceType::StorageTexelBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return ResourceType::UniformBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        return ResourceType::StorageBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return ResourceType::UniformBufferDynamic;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return ResourceType::StorageBufferDynamic;
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return ResourceType::InputAttachment;
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return ResourceType::AccelerationStructureKHR;
    default: return ResourceType::Unknown;
    }
}

[[nodiscard]] static VkFormat convert_format(SpvReflectFormat f) noexcept {
    // clang-format off
    switch (f) {
        case  SPV_REFLECT_FORMAT_UNDEFINED            :    return VK_FORMAT_UNDEFINED             ;
        case  SPV_REFLECT_FORMAT_R16_UINT             :    return VK_FORMAT_R16_UINT              ;
        case  SPV_REFLECT_FORMAT_R16_SINT             :    return VK_FORMAT_R16_SINT              ;
        case  SPV_REFLECT_FORMAT_R16_SFLOAT           :    return VK_FORMAT_R16_SFLOAT            ;
        case  SPV_REFLECT_FORMAT_R16G16_UINT          :    return VK_FORMAT_R16G16_UINT           ;
        case  SPV_REFLECT_FORMAT_R16G16_SINT          :    return VK_FORMAT_R16G16_SINT           ;
        case  SPV_REFLECT_FORMAT_R16G16_SFLOAT        :    return VK_FORMAT_R16G16_SFLOAT         ;
        case  SPV_REFLECT_FORMAT_R16G16B16_UINT       :    return VK_FORMAT_R16G16B16_UINT        ;
        case  SPV_REFLECT_FORMAT_R16G16B16_SINT       :    return VK_FORMAT_R16G16B16_SINT        ;
        case  SPV_REFLECT_FORMAT_R16G16B16_SFLOAT     :    return VK_FORMAT_R16G16B16_SFLOAT      ;
        case  SPV_REFLECT_FORMAT_R16G16B16A16_UINT    :    return VK_FORMAT_R16G16B16A16_UINT     ;
        case  SPV_REFLECT_FORMAT_R16G16B16A16_SINT    :    return VK_FORMAT_R16G16B16A16_SINT     ;
        case  SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT  :    return VK_FORMAT_R16G16B16A16_SFLOAT   ;
        case  SPV_REFLECT_FORMAT_R32_UINT             :    return VK_FORMAT_R32_UINT              ;
        case  SPV_REFLECT_FORMAT_R32_SINT             :    return VK_FORMAT_R32_SINT              ;
        case  SPV_REFLECT_FORMAT_R32_SFLOAT           :    return VK_FORMAT_R32_SFLOAT            ;
        case  SPV_REFLECT_FORMAT_R32G32_UINT          :    return VK_FORMAT_R32G32_UINT           ;
        case  SPV_REFLECT_FORMAT_R32G32_SINT          :    return VK_FORMAT_R32G32_SINT           ;
        case  SPV_REFLECT_FORMAT_R32G32_SFLOAT        :    return VK_FORMAT_R32G32_SFLOAT         ;
        case  SPV_REFLECT_FORMAT_R32G32B32_UINT       :    return VK_FORMAT_R32G32B32_UINT        ;
        case  SPV_REFLECT_FORMAT_R32G32B32_SINT       :    return VK_FORMAT_R32G32B32_SINT        ;
        case  SPV_REFLECT_FORMAT_R32G32B32_SFLOAT     :    return VK_FORMAT_R32G32B32_SFLOAT      ;
        case  SPV_REFLECT_FORMAT_R32G32B32A32_UINT    :    return VK_FORMAT_R32G32B32A32_UINT     ;
        case  SPV_REFLECT_FORMAT_R32G32B32A32_SINT    :    return VK_FORMAT_R32G32B32A32_SINT     ;
        case  SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT  :    return VK_FORMAT_R32G32B32A32_SFLOAT   ;
        case  SPV_REFLECT_FORMAT_R64_UINT             :    return VK_FORMAT_R64_UINT              ;
        case  SPV_REFLECT_FORMAT_R64_SINT             :    return VK_FORMAT_R64_SINT              ;
        case  SPV_REFLECT_FORMAT_R64_SFLOAT           :    return VK_FORMAT_R64_SFLOAT            ;
        case  SPV_REFLECT_FORMAT_R64G64_UINT          :    return VK_FORMAT_R64G64_UINT           ;
        case  SPV_REFLECT_FORMAT_R64G64_SINT          :    return VK_FORMAT_R64G64_SINT           ;
        case  SPV_REFLECT_FORMAT_R64G64_SFLOAT        :    return VK_FORMAT_R64G64_SFLOAT         ;
        case  SPV_REFLECT_FORMAT_R64G64B64_UINT       :    return VK_FORMAT_R64G64B64_UINT        ;
        case  SPV_REFLECT_FORMAT_R64G64B64_SINT       :    return VK_FORMAT_R64G64B64_SINT        ;
        case  SPV_REFLECT_FORMAT_R64G64B64_SFLOAT     :    return VK_FORMAT_R64G64B64_SFLOAT      ;
        case  SPV_REFLECT_FORMAT_R64G64B64A64_UINT    :    return VK_FORMAT_R64G64B64A64_UINT     ;
        case  SPV_REFLECT_FORMAT_R64G64B64A64_SINT    :    return VK_FORMAT_R64G64B64A64_SINT     ;
        case  SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT  :    return VK_FORMAT_R64G64B64A64_SFLOAT   ;
    }
    // clang-format on
    return VK_FORMAT_UNDEFINED;
}

/// 与 `convert_format` 输出一致，用于紧密排布时的属性跨度（字节）。
[[nodiscard]] static std::uint32_t
vk_format_vertex_attribute_extent_bytes(const VkFormat f) noexcept {
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

[[nodiscard]] static std::expected<VkVertexInputRate, ReflectionError>
vertex_input_rate_for_binding(const std::uint32_t binding) {
    if (binding == 0U) {
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }
    if (binding == 1U) {
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    return std::unexpected(
        std::string { "vertex attribute binding must be 0 (per-vertex) or 1 "
                      "(per-instance) for shader::reflection vertex layout "
                      "convention" });
}

[[nodiscard]] static VkVertexInputRate resolve_vertex_input_rate(
    const std::uint32_t binding,
    const std::span<const VertexBinding> binding_specs) noexcept {
    for (const VertexBinding &vb : binding_specs) {
        if (vb.binding == binding) {
            return vb.rate;
        }
    }
    return binding == 0U ? VK_VERTEX_INPUT_RATE_VERTEX
                         : VK_VERTEX_INPUT_RATE_INSTANCE;
}

[[nodiscard]] static std::uint32_t resolve_vertex_stride(
    const std::uint32_t binding, const std::uint32_t computed_stride,
    const std::span<const VertexBinding> binding_specs) noexcept {
    for (const VertexBinding &vb : binding_specs) {
        if (vb.binding == binding && vb.stride != 0U) {
            return vb.stride;
        }
    }
    return computed_stride;
}

[[nodiscard]] static std::expected<PackedVertexInputState, ReflectionError>
pack_vertex_input_state(const std::span<const VertexAttribute> attrs,
                        const std::span<const VertexBinding> binding_specs) {
    PackedVertexInputState out;
    if (attrs.empty()) {
        out.rebind_pipeline_vertex_input_state_create_info();
        return out;
    }

    std::vector<VertexAttribute> sorted(attrs.begin(), attrs.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const VertexAttribute &a, const VertexAttribute &b) {
                  if (a.binding != b.binding) {
                      return a.binding < b.binding;
                  }
                  return a.location < b.location;
              });

    for (const VertexAttribute &a : sorted) {
        if (a.format == VK_FORMAT_UNDEFINED) {
            return std::unexpected(
                std::string { "pack_vertex_input_state: "
                              "VK_FORMAT_UNDEFINED attribute" });
        }
        const auto rate_ok = vertex_input_rate_for_binding(a.binding);
        if (!rate_ok) {
            return std::unexpected(rate_ok.error());
        }
    }

    out.attributes.reserve(sorted.size());
    out.binds.reserve(2U);

    for (std::size_t i = 0; i < sorted.size();) {
        const std::uint32_t b = sorted[i].binding;
        std::uint32_t offset = 0U;
        std::size_t j = i;
        while (j < sorted.size() && sorted[j].binding == b) {
            const VertexAttribute &a = sorted[j];
            offset = align_up_u32(offset, 4U);
            const std::uint32_t extent =
                vk_format_vertex_attribute_extent_bytes(a.format);
            if (extent == 0U) {
                return std::unexpected(
                    std::string { "pack_vertex_input_state: unsupported "
                                  "VkFormat for vertex attribute size" });
            }
            VkVertexInputAttributeDescription ad {};
            ad.location = a.location;
            ad.binding = b;
            ad.format = a.format;
            ad.offset = offset;
            out.attributes.push_back(ad);
            offset += extent;
            ++j;
        }
        const std::uint32_t computed_stride = align_up_u32(offset, 4U);
        if (computed_stride == 0U) {
            return std::unexpected(
                std::string { "pack_vertex_input_state: computed stride "
                              "is zero" });
        }
        VkVertexInputBindingDescription bd {};
        bd.binding = b;
        bd.stride = resolve_vertex_stride(b, computed_stride, binding_specs);
        bd.inputRate = resolve_vertex_input_rate(b, binding_specs);
        out.binds.push_back(bd);
        i = j;
    }

    out.rebind_pipeline_vertex_input_state_create_info();
    return out;
}

// --- 单模块 SPIR-V 反射入口 ---

[[nodiscard]] ShaderReflection
ShaderReflection::from_spirv(const std::span<const std::byte> spirv,
                             VkShaderStageFlagBits stage) {
    ShaderReflection reflection {};
    reflection.stage_ = stage;

    SHADER_REFLECTION_LOG_DEBUG(
        std::format("spirv_reflect: from_spirv bytes {} stage {}", spirv.size(),
                    detail::shader_stage_flags_to_string(
                        static_cast<VkShaderStageFlags>(stage))));

    SpvReflectShaderModule module {};
    spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module);

    std::uint32_t count = 0U;
    spvReflectEnumerateEntryPointDescriptorSets(&module, "main", &count,
                                                nullptr);
    std::vector<SpvReflectDescriptorSet *> sets(count);
    spvReflectEnumerateEntryPointDescriptorSets(&module, "main", &count,
                                                sets.data());

    for (const auto *set : sets) {
        for (std::uint32_t i = 0; i < set->binding_count; ++i) {
            const auto *binding = set->bindings[i];

            ShaderResource resource {};

            resource.set = set->set;
            resource.binding = binding->binding;
            resource.count = binding->count;
            resource.type = convert_descriptor_type(binding->descriptor_type);
            resource.name = binding->name;
            resource.size = binding->block.size;
            resource.stages =
                static_cast<VkShaderStageFlags>(reflection.stage_);
            resource.offset = binding->block.offset;

            reflection.resources_.push_back(resource);
        }
    }

    spvReflectEnumerateEntryPointPushConstantBlocks(&module, "main", &count,
                                                    nullptr);
    std::vector<SpvReflectBlockVariable *> pushConstantBlocks(count);
    spvReflectEnumerateEntryPointPushConstantBlocks(&module, "main", &count,
                                                    pushConstantBlocks.data());

    for (const auto *pushConstantBlock : pushConstantBlocks) {
        PushConstant pushConstant {};
        pushConstant.size = pushConstantBlock->size;
        pushConstant.offset = pushConstantBlock->offset;
        pushConstant.stages =
            static_cast<VkShaderStageFlags>(reflection.stage_);
        reflection.pushConstants_.push_back(pushConstant);
    }

    spvReflectEnumerateEntryPointInputVariables(&module, "main", &count,
                                                nullptr);
    std::vector<SpvReflectInterfaceVariable *> inputVariables(count);
    spvReflectEnumerateEntryPointInputVariables(&module, "main", &count,
                                                inputVariables.data());

    for (const auto *inputVariable : inputVariables) {
        ShaderInput input {};
        if (inputVariable->location == UINT32_MAX) {
            continue;
        }

        if ((inputVariable->decoration_flags &
             SPV_REFLECT_DECORATION_BUILT_IN) != 0U) {
            continue;
        }

        input.format = convert_format(inputVariable->format);
        input.location = inputVariable->location;
        input.name = inputVariable->name;

        reflection.inputs_.push_back(input);
    }

    spvReflectEnumerateEntryPointOutputVariables(&module, "main", &count,
                                                 nullptr);
    std::vector<SpvReflectInterfaceVariable *> outputVariables(count);
    spvReflectEnumerateEntryPointOutputVariables(&module, "main", &count,
                                                 outputVariables.data());

    for (const auto *outputVariable : outputVariables) {
        if (outputVariable->location == UINT32_MAX) {
            continue;
        }

        if ((outputVariable->decoration_flags &
             SPV_REFLECT_DECORATION_BUILT_IN) != 0U) {
            continue;
        }

        ShaderOutput output {};
        output.location = outputVariable->location;
        output.format = convert_format(outputVariable->format);
        output.name = outputVariable->name;
        reflection.outputs_.push_back(output);
    }

    spvReflectDestroyShaderModule(&module);

    SHADER_REFLECTION_LOG_DEBUG(std::format(
        "spirv_reflect: from_spirv done resources {} inputs {} outputs {} "
        "push {}",
        reflection.resources_.size(), reflection.inputs_.size(),
        reflection.outputs_.size(), reflection.pushConstants_.size()));

    return reflection;
}

[[nodiscard]] std::expected<PackedVertexInputState, ReflectionError>
MergedShaderReflection::packed_vertex_input_state() const {
    if (!packedVertexInputStateCache_.has_value()) {
        std::vector<VertexAttribute> attrs;
        attrs.reserve(vertexInput_.size());
        for (const ShaderInput &in : vertexInput_) {
            attrs.push_back(vertex_attribute_from_shader_input(in, 0U));
        }
        const std::vector<VertexBinding> defs = default_vertex_bindings();
        packedVertexInputStateCache_.emplace(pack_vertex_input_state(
            std::span<const VertexAttribute> { attrs.data(), attrs.size() },
            std::span<const VertexBinding> { defs.data(), defs.size() }));
    }
    const auto &cached = *packedVertexInputStateCache_;
    if (!cached) {
        return std::unexpected(cached.error());
    }
    PackedVertexInputState out = *cached;
    out.rebind_pipeline_vertex_input_state_create_info();
    return out;
}

PackedVertexInputState::PackedVertexInputState() {
    pipelineVertexInputStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
}

void PackedVertexInputState::
    rebind_pipeline_vertex_input_state_create_info() noexcept {
    pipelineVertexInputStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(binds.size());
    pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions =
        binds.empty() ? nullptr : binds.data();
    pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attributes.size());
    pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions =
        attributes.empty() ? nullptr : attributes.data();
}

} // namespace shader::reflection
