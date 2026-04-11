#include <cctype>
#include <array>
#include <charconv>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>  

#include "core/log/logger.hpp"

#include "utils.hpp"

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#if defined(VK_SHADER_STAGE_TASK_BIT)
constexpr VkShaderStageFlags k_task_shader_stage_bit = VK_SHADER_STAGE_TASK_BIT;
#elif defined(VK_SHADER_STAGE_TASK_BIT_EXT)
constexpr VkShaderStageFlags k_task_shader_stage_bit =
    VK_SHADER_STAGE_TASK_BIT_EXT;
#elif defined(VK_SHADER_STAGE_TASK_BIT_NV)
constexpr VkShaderStageFlags k_task_shader_stage_bit =
    VK_SHADER_STAGE_TASK_BIT_NV;
#else
constexpr VkShaderStageFlags k_task_shader_stage_bit = VkShaderStageFlags { 0 };
#endif

#if defined(VK_SHADER_STAGE_MESH_BIT)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit = VK_SHADER_STAGE_MESH_BIT;
#elif defined(VK_SHADER_STAGE_MESH_BIT_EXT)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit =
    VK_SHADER_STAGE_MESH_BIT_EXT;
#elif defined(VK_SHADER_STAGE_MESH_BIT_NV)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit =
    VK_SHADER_STAGE_MESH_BIT_NV;
#else
constexpr VkShaderStageFlags k_mesh_shader_stage_bit = VkShaderStageFlags { 0 };
#endif

std::string get_descriptor_type_name(SpvReflectDescriptorType type) {
    switch (type) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return "sampler";
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return "combined_image_sampler";
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "sampled_image";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "storage_image";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        return "uniform_texel_buffer";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return "storage_texel_buffer";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "uniform_buffer";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "storage_buffer";
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return "uniform_buffer_dynamic";
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return "storage_buffer_dynamic";
    case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return "input_attachment";
    case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        return "acceleration_structure_KHR";
    default: return "unknown";
    }
}

enum class ResourceType {
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructureKHR,
    Unknown
};

struct ShaderResource {
    uint32_t set;
    uint32_t binding;
    ResourceType type;

    uint32_t count;

    uint32_t size;   // UBO size
    uint32_t offset; // push constant

    std::string name;

    VkShaderStageFlags stages;
};

struct ShaderInput {
    uint32_t location;
    VkFormat format;
    std::string name;
};

struct ShaderOutput {
    uint32_t location;
    VkFormat format;
    std::string name;
};

/// 顶点属性：来自反射的 `location` / `format`，`binding` 由本工程分配（SPIR-V 不携带
/// `VkVertexInputBindingDescription`）。
struct VertexAttribute {
    uint32_t location{};
    VkFormat format{ VK_FORMAT_UNDEFINED };

    /// Vulkan vertex buffer binding。约定：`0` = per-vertex，`1` = per-instance。
    uint32_t binding{};
};

/// 顶点缓冲绑定：`stride` / `VkVertexInputRate` 无法从反射得到；`rate` 与 `binding`
/// 按工程约定对齐（见 `vertex_bindings_lumen_convention`）。
struct VertexBinding {
    uint32_t binding{};
    uint32_t stride{};
    VkVertexInputRate rate{ VK_VERTEX_INPUT_RATE_VERTEX };
};

struct PushConstant {
    uint32_t size;
    uint32_t offset;
    VkShaderStageFlags stages;
};

struct ShaderReflection {
    VkShaderStageFlagBits stage;

    std::vector<ShaderResource> resources;
    std::vector<ShaderInput> inputs;
    std::vector<ShaderOutput> outputs;

    std::vector<PushConstant> pushConstant;
};

/// 多阶段着色器反射的合并视图（由 `merge_shader_reflections` 填充）。
struct MergedShaderReflection {
    /// 参与合并的阶段，与 `ShaderResource::stages` 相同的小写名，用 `|` 连接（如
    /// `vertex|fragment`）。
    std::string stage;

    std::vector<ShaderResource> resources;
    std::vector<ShaderInput> vertexInput;
    std::vector<ShaderOutput> fragmentOutput;

    std::vector<PushConstant> pushConstant;
};

/// 每个 set 的 layout 内容指纹（由 `descriptor_set_layout_bindings_by_set_from_merged_reflection`
/// 推导的绑定表；与具体 `VkDescriptorSetLayout` 句柄无关）。
/// 缓存命中时假定「与 `create_pipeline_layout_from_merged_reflection` 所用 set_layouts 顺序一致」；
/// 若句柄不同但绑定定义相同，可安全复用同一 `VkPipelineLayout`（Vulkan 布局兼容）。
using SetLayoutHash = std::uint64_t;
using PushConstantHash = std::uint64_t;

struct PipelineLayoutKey {
    std::vector<SetLayoutHash> set_hashes;
    PushConstantHash push_constant_hash{};

    friend bool operator==(const PipelineLayoutKey &a,
                           const PipelineLayoutKey &b) noexcept {
        return a.set_hashes == b.set_hashes &&
               a.push_constant_hash == b.push_constant_hash;
    }
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    ResourceType,
    {
        { ResourceType::UniformBuffer, "uniform_buffer" },
        { ResourceType::StorageBuffer, "storage_buffer" },
        { ResourceType::SampledImage, "sampled_image" },
        { ResourceType::StorageImage, "storage_image" },
        { ResourceType::Sampler, "sampler" },
        { ResourceType::CombinedImageSampler, "combined_image_sampler" },
        { ResourceType::UniformTexelBuffer, "uniform_texel_buffer" },
        { ResourceType::StorageTexelBuffer, "storage_texel_buffer" },
        { ResourceType::UniformBufferDynamic, "uniform_buffer_dynamic" },
        { ResourceType::StorageBufferDynamic, "storage_buffer_dynamic" },
        { ResourceType::InputAttachment, "input_attachment" },
        { ResourceType::AccelerationStructureKHR,
          "acceleration_structure_khr" },
        { ResourceType::Unknown, "unknown" },
    })

namespace {

struct VkFormatJsonEntry {
    std::string_view name;
    VkFormat format;
};

constexpr auto k_vk_format_json = std::to_array<VkFormatJsonEntry>({
    { "UNDEFINED", VK_FORMAT_UNDEFINED },
    { "R16_UINT", VK_FORMAT_R16_UINT },
    { "R16_SINT", VK_FORMAT_R16_SINT },
    { "R16_SFLOAT", VK_FORMAT_R16_SFLOAT },
    { "R16G16_UINT", VK_FORMAT_R16G16_UINT },
    { "R16G16_SINT", VK_FORMAT_R16G16_SINT },
    { "R16G16_SFLOAT", VK_FORMAT_R16G16_SFLOAT },
    { "R16G16B16_UINT", VK_FORMAT_R16G16B16_UINT },
    { "R16G16B16_SINT", VK_FORMAT_R16G16B16_SINT },
    { "R16G16B16_SFLOAT", VK_FORMAT_R16G16B16_SFLOAT },
    { "R16G16B16A16_UINT", VK_FORMAT_R16G16B16A16_UINT },
    { "R16G16B16A16_SINT", VK_FORMAT_R16G16B16A16_SINT },
    { "R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT },
    { "R32_UINT", VK_FORMAT_R32_UINT },
    { "R32_SINT", VK_FORMAT_R32_SINT },
    { "R32_SFLOAT", VK_FORMAT_R32_SFLOAT },
    { "R32G32_UINT", VK_FORMAT_R32G32_UINT },
    { "R32G32_SINT", VK_FORMAT_R32G32_SINT },
    { "R32G32_SFLOAT", VK_FORMAT_R32G32_SFLOAT },
    { "R32G32B32_UINT", VK_FORMAT_R32G32B32_UINT },
    { "R32G32B32_SINT", VK_FORMAT_R32G32B32_SINT },
    { "R32G32B32_SFLOAT", VK_FORMAT_R32G32B32_SFLOAT },
    { "R32G32B32A32_UINT", VK_FORMAT_R32G32B32A32_UINT },
    { "R32G32B32A32_SINT", VK_FORMAT_R32G32B32A32_SINT },
    { "R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT },
    { "R64_UINT", VK_FORMAT_R64_UINT },
    { "R64_SINT", VK_FORMAT_R64_SINT },
    { "R64_SFLOAT", VK_FORMAT_R64_SFLOAT },
    { "R64G64_UINT", VK_FORMAT_R64G64_UINT },
    { "R64G64_SINT", VK_FORMAT_R64G64_SINT },
    { "R64G64_SFLOAT", VK_FORMAT_R64G64_SFLOAT },
    { "R64G64B64_UINT", VK_FORMAT_R64G64B64_UINT },
    { "R64G64B64_SINT", VK_FORMAT_R64G64B64_SINT },
    { "R64G64B64_SFLOAT", VK_FORMAT_R64G64B64_SFLOAT },
    { "R64G64B64A64_UINT", VK_FORMAT_R64G64B64A64_UINT },
    { "R64G64B64A64_SINT", VK_FORMAT_R64G64B64A64_SINT },
    { "R64G64B64A64_SFLOAT", VK_FORMAT_R64G64B64A64_SFLOAT },
});

} // namespace

[[nodiscard]] inline bool ascii_iequal(std::string_view a,
                                       std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::string vk_format_to_string(VkFormat v) {
    for (const auto &e : k_vk_format_json) {
        if (e.format == v) {
            return std::string { e.name };
        }
    }
    return std::string { "RAW_" } +
           std::to_string(static_cast<std::uint32_t>(v));
}

[[nodiscard]] inline VkFormat vk_format_from_string(std::string_view s) {
    if (s.size() > 4U && ascii_iequal(s.substr(0U, 4U), "RAW_")) {
        std::uint32_t value = 0U;
        const char *const first = s.data() + 4U;
        const char *const last = s.data() + s.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc {} || ptr != last) {
            throw std::invalid_argument("invalid VkFormat RAW_ string");
        }
        return static_cast<VkFormat>(value);
    }
    for (const auto &e : k_vk_format_json) {
        if (ascii_iequal(s, e.name)) {
            return e.format;
        }
    }
    throw std::invalid_argument("unknown VkFormat string");
}

inline void to_json(json &j, VkFormat v) { j = vk_format_to_string(v); }

inline void from_json(const json &j, VkFormat &v) {
    if (j.is_number_unsigned()) {
        v = static_cast<VkFormat>(j.get<std::uint32_t>());
        return;
    }
    const auto &s = j.get_ref<const std::string &>();
    v = vk_format_from_string(s);
}

inline void to_json(json &j, VkShaderStageFlags v) {
    j = static_cast<std::uint32_t>(v);
}

inline void from_json(const json &j, VkShaderStageFlags &v) {
    v = static_cast<VkShaderStageFlags>(j.get<std::uint32_t>());
}

[[nodiscard]] inline std::string_view trim_stage_token(std::string_view s) noexcept {
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] inline std::string shader_stage_flags_to_string(VkShaderStageFlags v) {
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

[[nodiscard]] inline VkShaderStageFlags shader_stage_flag_from_name(std::string_view name) {
    if (name == "vertex") {
        return VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (name == "tessellation_control") {
        return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    if (name == "tessellation_evaluation") {
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    if (name == "geometry") {
        return VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (name == "fragment") {
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (name == "compute") {
        return VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (name == "task") {
        if (k_task_shader_stage_bit == VkShaderStageFlags { 0 }) {
            throw std::invalid_argument("task shader stage not supported");
        }
        return k_task_shader_stage_bit;
    }
    if (name == "mesh") {
        if (k_mesh_shader_stage_bit == VkShaderStageFlags { 0 }) {
            throw std::invalid_argument("mesh shader stage not supported");
        }
        return k_mesh_shader_stage_bit;
    }
    if (name == "raygen") {
        return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    }
    if (name == "any_hit") {
        return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    }
    if (name == "closest_hit") {
        return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    }
    if (name == "miss") {
        return VK_SHADER_STAGE_MISS_BIT_KHR;
    }
    if (name == "intersection") {
        return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    }
    if (name == "callable") {
        return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    }
    throw std::invalid_argument("unknown shader stage name");
}

[[nodiscard]] inline VkShaderStageFlags
shader_resource_stages_from_json(const json &j) {
    if (j.is_number_unsigned()) {
        return static_cast<VkShaderStageFlags>(j.get<std::uint32_t>());
    }
    if (j.is_string()) {
        const auto &s = j.get_ref<const std::string &>();
        VkShaderStageFlags flags = 0U;
        std::size_t start = 0U;
        for (;;) {
            const auto sep = s.find('|', start);
            const auto end = (sep == std::string::npos) ? s.size() : sep;
            const auto tok =
                trim_stage_token(std::string_view { s.data() + start,
                                                      end - start });
            if (!tok.empty()) {
                flags |= shader_stage_flag_from_name(tok);
            }
            if (sep == std::string::npos) {
                break;
            }
            start = sep + 1U;
        }
        return flags;
    }
    if (j.is_array()) {
        VkShaderStageFlags flags = 0U;
        for (const auto &el : j) {
            const auto &name = el.get_ref<const std::string &>();
            flags |= shader_stage_flag_from_name(name);
        }
        return flags;
    }
    throw std::invalid_argument(
        "resource stages must be a pipe-separated string, string array, or "
        "unsigned integer");
}

inline void to_json(json &j, VkShaderStageFlagBits v) {
    j = shader_stage_flags_to_string(static_cast<VkShaderStageFlags>(v));
}

inline void from_json(const json &j, VkShaderStageFlagBits &v) {
    const VkShaderStageFlags flags = shader_resource_stages_from_json(j);
    const auto u = static_cast<std::uint32_t>(flags);
    if (u != 0U && (u & (u - 1U)) != 0U) {
        throw std::invalid_argument(
            "VkShaderStageFlagBits JSON must be exactly one stage flag");
    }
    v = static_cast<VkShaderStageFlagBits>(flags);
}

inline void to_json(json &j, const ShaderResource &r) {
    j = json { { "set", r.set },   { "binding", r.binding },
               { "type", r.type }, { "count", r.count },
               { "size", r.size }, { "offset", r.offset },
               { "name", r.name },
               { "stages", shader_stage_flags_to_string(r.stages) } };
}

inline void from_json(const json &j, ShaderResource &r) {
    j.at("set").get_to(r.set);
    j.at("binding").get_to(r.binding);
    j.at("type").get_to(r.type);
    j.at("count").get_to(r.count);
    j.at("size").get_to(r.size);
    j.at("offset").get_to(r.offset);
    j.at("name").get_to(r.name);
    r.stages = shader_resource_stages_from_json(j.at("stages"));
}

inline void to_json(json &j, const ShaderInput &i) {
    j = json { { "location", i.location },
               { "format", i.format },
               { "name", i.name } };
}

inline void from_json(const json &j, ShaderInput &i) {
    j.at("location").get_to(i.location);
    j.at("format").get_to(i.format);
    j.at("name").get_to(i.name);
}

inline void to_json(json &j, const ShaderOutput &o) {
    j = json { { "location", o.location },
               { "format", o.format },
               { "name", o.name } };
}

inline void from_json(const json &j, ShaderOutput &o) {
    j.at("location").get_to(o.location);
    j.at("format").get_to(o.format);
    j.at("name").get_to(o.name);
}

inline void to_json(json &j, const PushConstant &p) {
    j = json { { "size", p.size },
               { "offset", p.offset },
               { "stages", p.stages } };
}

inline void from_json(const json &j, PushConstant &p) {
    j.at("size").get_to(p.size);
    j.at("offset").get_to(p.offset);
    j.at("stages").get_to(p.stages);
}

inline void to_json(json &j, const ShaderReflection &r) {
    j = json { { "stage", r.stage },
               { "resources", r.resources },
               { "inputs", r.inputs },
               { "outputs", r.outputs },
               { "push_constant", r.pushConstant } };
}

inline void from_json(const json &j, ShaderReflection &r) {
    j.at("stage").get_to(r.stage);
    j.at("resources").get_to(r.resources);
    j.at("inputs").get_to(r.inputs);
    j.at("outputs").get_to(r.outputs);
    j.at("push_constant").get_to(r.pushConstant);
}

inline void to_json(json &j, const MergedShaderReflection &m) {
    j = json { { "stage", m.stage },
               { "resources", m.resources },
               { "vertex_input", m.vertexInput },
               { "fragment_output", m.fragmentOutput },
               { "push_constant", m.pushConstant } };
}

inline void from_json(const json &j, MergedShaderReflection &m) {
    j.at("stage").get_to(m.stage);
    j.at("resources").get_to(m.resources);
    j.at("vertex_input").get_to(m.vertexInput);
    j.at("fragment_output").get_to(m.fragmentOutput);
    j.at("push_constant").get_to(m.pushConstant);
}

namespace {

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
    case ResourceType::UniformBuffer:
        return "uniform_buffer";
    case ResourceType::StorageBuffer:
        return "storage_buffer";
    case ResourceType::SampledImage:
        return "sampled_image";
    case ResourceType::StorageImage:
        return "storage_image";
    case ResourceType::Sampler:
        return "sampler";
    case ResourceType::CombinedImageSampler:
        return "combined_image_sampler";
    case ResourceType::UniformTexelBuffer:
        return "uniform_texel_buffer";
    case ResourceType::StorageTexelBuffer:
        return "storage_texel_buffer";
    case ResourceType::UniformBufferDynamic:
        return "uniform_buffer_dynamic";
    case ResourceType::StorageBufferDynamic:
        return "storage_buffer_dynamic";
    case ResourceType::InputAttachment:
        return "input_attachment";
    case ResourceType::AccelerationStructureKHR:
        return "acceleration_structure_khr";
    case ResourceType::Unknown:
        return "unknown";
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

inline void append_or_merge_push_constant(std::vector<PushConstant> &out,
                                          const PushConstant &pc) {
    for (PushConstant &existing : out) {
        if (existing.offset == pc.offset && existing.size == pc.size) {
            existing.stages |= pc.stages;
            return;
        }
    }
    out.push_back(pc);
}

[[nodiscard]] std::vector<ShaderResource>
merge_descriptor_resources(std::span<const ShaderReflection> shaders) {
    std::map<DescriptorBindingKey, ShaderResource> resource_by_key;
    for (const ShaderReflection &shader : shaders) {
        const auto stage_bits =
            static_cast<VkShaderStageFlags>(shader.stage);
        for (const ShaderResource &res : shader.resources) {
            const DescriptorBindingKey key { res.set, res.binding };
            ShaderResource res_cleared = res;
            res_cleared.stages = 0;
            auto [it, inserted] = resource_by_key.try_emplace(key, res_cleared);
            ShaderResource &dst = it->second;
            if (!inserted) {
                if (dst.type != ResourceType::Unknown &&
                    res.type != ResourceType::Unknown &&
                    dst.type != res.type) {
                    throw std::invalid_argument(
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

} // namespace

[[nodiscard]] MergedShaderReflection
merge_shader_reflections(std::span<const ShaderReflection> shaders) {
    MergedShaderReflection merged {};

    std::string stage_tokens;
    for (const ShaderReflection &s : shaders) {
        const std::string token =
            shader_stage_flags_to_string(static_cast<VkShaderStageFlags>(s.stage));
        if (!token.empty()) {
            if (!stage_tokens.empty()) {
                stage_tokens += '|';
            }
            stage_tokens += token;
        }
    }
    merged.stage = std::move(stage_tokens);

    merged.resources = merge_descriptor_resources(shaders);

    for (const ShaderReflection &s : shaders) {
        const auto bits = static_cast<VkShaderStageFlags>(s.stage);
        if ((bits & VK_SHADER_STAGE_VERTEX_BIT) != 0U) {
            merged.vertexInput = s.inputs;
            break;
        }
    }   

    for (const ShaderReflection &s : shaders) {
        const auto bits = static_cast<VkShaderStageFlags>(s.stage);
        if ((bits & VK_SHADER_STAGE_FRAGMENT_BIT) != 0U) {
            merged.fragmentOutput = s.outputs;
            break;
        }
    }

    for (const ShaderReflection &s : shaders) {
        for (const PushConstant &pc : s.pushConstant) {
            append_or_merge_push_constant(merged.pushConstant, pc);
        }
    }

    return merged;
}

/// 约定：binding `0` → `VK_VERTEX_INPUT_RATE_VERTEX`，binding `1` →
/// `VK_VERTEX_INPUT_RATE_INSTANCE`。`stride==0` 表示由 `make_packed_vertex_input_state`
/// 按属性紧密排布自动计算。
[[nodiscard]] std::vector<VertexBinding> vertex_bindings_lumen_convention() {
    return { VertexBinding { 0U, 0U, VK_VERTEX_INPUT_RATE_VERTEX },
             VertexBinding { 1U, 0U, VK_VERTEX_INPUT_RATE_INSTANCE } };
}

[[nodiscard]] VertexAttribute vertex_attribute_from_shader_input(
    const ShaderInput &in, const uint32_t binding) noexcept {
    return VertexAttribute { in.location, in.format, binding };
}

/// 默认将全部顶点输入放到 **binding 0**（per-vertex）；实例数据需调用方自行构造
/// `VertexAttribute`（`binding = 1`）并合并后再打包。
[[nodiscard]] std::vector<VertexAttribute>
vertex_attributes_from_merged_reflection_binding0(
    const MergedShaderReflection &merged) {
    std::vector<VertexAttribute> out;
    out.reserve(merged.vertexInput.size());
    for (const ShaderInput &in : merged.vertexInput) {
        out.push_back(vertex_attribute_from_shader_input(in, 0U));
    }
    return out;
}

[[nodiscard]] VkDescriptorType
resource_type_to_vk_descriptor_type(ResourceType t) {
    switch (t) {
    case ResourceType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ResourceType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::SampledImage:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ResourceType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ResourceType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
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
        throw std::invalid_argument(
            "resource_type_to_vk_descriptor_type: Unknown ResourceType");
    }
}

/// 工程约定：**set 0、set 1** 的 uniform buffer 在管线里使用 **dynamic** 偏移绑定。
[[nodiscard]] VkDescriptorType vk_descriptor_type_for_merged_resource(
    std::uint32_t set_index, ResourceType resource_type) {
    VkDescriptorType dt = resource_type_to_vk_descriptor_type(resource_type);
    if (set_index <= 1U &&
        dt == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    return dt;
}

/// 一次遍历 `merged.resources`，按 **set** 分组生成各 `VkDescriptorSetLayout` 的 binding 列表。
/// `merge_descriptor_resources` 已保证 `(set, binding)` 唯一，此处仅做防御性校验。
/// set 0 / set 1 的 uniform buffer 使用 **res.set** 参与 `vk_descriptor_type_for_merged_resource`。
[[nodiscard]] std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>>
descriptor_set_layout_bindings_by_set_from_merged_reflection(
    const MergedShaderReflection &merged) {
    std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>> by_set;
    for (const ShaderResource &res : merged.resources) {
        VkDescriptorSetLayoutBinding b {};
        b.binding = res.binding;
        b.descriptorType =
            vk_descriptor_type_for_merged_resource(res.set, res.type);
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
                throw std::invalid_argument(
                    "descriptor_set_layout_bindings_by_set_from_merged_reflection: "
                    "duplicate binding in merged.resources (merge bug?)");
            }
        }
    }
    return by_set;
}

/// 单个 set 的 binding 列表；若该 set 无资源则返回空 vector。
[[nodiscard]] std::vector<VkDescriptorSetLayoutBinding>
descriptor_set_layout_bindings_from_merged_reflection(
    const MergedShaderReflection &merged, std::uint32_t set_index) {
    const auto by_set =
        descriptor_set_layout_bindings_by_set_from_merged_reflection(merged);
    const auto it = by_set.find(set_index);
    if (it == by_set.end()) {
        return {};
    }
    return it->second;
}

namespace {

[[nodiscard]] VkDescriptorSetLayout create_descriptor_set_layout_from_bindings(
    VkDevice device,
    const std::vector<VkDescriptorSetLayoutBinding> &bindings,
    const VkAllocationCallbacks *allocator) {
    VkDescriptorSetLayoutCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult r =
        vkCreateDescriptorSetLayout(device, &ci, allocator, &layout);
    if (r != VK_SUCCESS) {
        throw std::runtime_error(
            std::string { "vkCreateDescriptorSetLayout failed, VkResult=" } +
            std::to_string(static_cast<int>(r)));
    }
    return layout;
}

} // namespace

/// 为 `set_index` 创建 `VkDescriptorSetLayout`（无该 set 的资源时创建 **0 个 binding** 的 layout）。
[[nodiscard]] VkDescriptorSetLayout
create_descriptor_set_layout_from_merged_reflection(
    VkDevice device, const MergedShaderReflection &merged,
    std::uint32_t set_index, const VkAllocationCallbacks *allocator = nullptr) {
    const auto bindings =
        descriptor_set_layout_bindings_from_merged_reflection(merged, set_index);
    return create_descriptor_set_layout_from_bindings(device, bindings,
                                                      allocator);
}

/// 按 set **一次性**创建全部 `VkDescriptorSetLayout`（与 `merged.resources` 中出现的 set 一致）。
/// 任一 `vkCreate` 失败：已创建的 layout 会 `vkDestroy` 后再抛 `std::runtime_error`。
[[nodiscard]] std::map<std::uint32_t, VkDescriptorSetLayout>
create_descriptor_set_layouts_by_set_from_merged_reflection(
    VkDevice device, const MergedShaderReflection &merged,
    const VkAllocationCallbacks *allocator = nullptr) {
    const auto by_set =
        descriptor_set_layout_bindings_by_set_from_merged_reflection(merged);
    std::map<std::uint32_t, VkDescriptorSetLayout> layouts;
    for (const auto &[set_idx, bindings] : by_set) {
        try {
            layouts.emplace(
                set_idx, create_descriptor_set_layout_from_bindings(
                           device, bindings, allocator));
        } catch (...) {
            for (const auto &kv : layouts) {
                if (kv.second != VK_NULL_HANDLE) {
                    vkDestroyDescriptorSetLayout(device, kv.second, allocator);
                }
            }
            throw;
        }
    }
    return layouts;
}

/// 由 `MergedShaderReflection` 与**已创建**的 `VkDescriptorSetLayout` 序列求解 `VkPipelineLayout`。
/// `set_layouts[i]` 对应 shader `layout(set=i)`（须与前面按反射推断 layout 时一致，一般为 **连续**
/// 下标 0…S；若中间某 set 无绑定，应传入该处 **空 layout**）。
/// **push constant** 仍来自 `merged.pushConstant` → `VkPushConstantRange`（`size==0` 跳过）。
/// `vkCreatePipelineLayout` 非 `VK_SUCCESS` 时抛 `std::runtime_error`（不销毁传入的 set layout）。
[[nodiscard]] VkPipelineLayout create_pipeline_layout_from_merged_reflection(
    VkDevice device, const MergedShaderReflection &merged,
    std::span<const VkDescriptorSetLayout> set_layouts,
    const VkAllocationCallbacks *allocator = nullptr) {
    std::vector<VkPushConstantRange> push_ranges;
    push_ranges.reserve(merged.pushConstant.size());
    for (const PushConstant &pc : merged.pushConstant) {
        if (pc.size == 0U) {
            continue;
        }
        VkPushConstantRange r {};
        r.stageFlags = pc.stages;
        r.offset = pc.offset;
        r.size = pc.size;
        push_ranges.push_back(r);
    }

    VkPipelineLayoutCreateInfo pl {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = static_cast<std::uint32_t>(set_layouts.size());
    pl.pSetLayouts =
        set_layouts.empty() ? nullptr : set_layouts.data();
    pl.pushConstantRangeCount =
        static_cast<std::uint32_t>(push_ranges.size());
    pl.pPushConstantRanges =
        push_ranges.empty() ? nullptr : push_ranges.data();

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    const VkResult pr = vkCreatePipelineLayout(device, &pl, allocator,
                                               &pipeline_layout);
    if (pr != VK_SUCCESS) {
        throw std::runtime_error(
            std::string { "vkCreatePipelineLayout failed, VkResult=" } +
            std::to_string(static_cast<int>(pr)));
    }
    return pipeline_layout;
}

[[nodiscard]] inline std::uint64_t hash_combine_u64(std::uint64_t h,
                                                    std::uint64_t v) noexcept {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

[[nodiscard]] SetLayoutHash hash_descriptor_set_layout_bindings(
    const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const VkDescriptorSetLayoutBinding &b : bindings) {
        h = hash_combine_u64(h, static_cast<std::uint64_t>(b.binding));
        h = hash_combine_u64(h, static_cast<std::uint64_t>(b.descriptorType));
        h = hash_combine_u64(h, static_cast<std::uint64_t>(b.descriptorCount));
        h = hash_combine_u64(h, static_cast<std::uint64_t>(b.stageFlags));
    }
    return h;
}

/// 与 `create_pipeline_layout_from_merged_reflection` 中 push range 构造一致：`size==0` 不参与。
[[nodiscard]] PushConstantHash hash_push_constants_for_pipeline_layout_key(
    const std::vector<PushConstant> &push_constants) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const PushConstant &pc : push_constants) {
        if (pc.size == 0U) {
            continue;
        }
        h = hash_combine_u64(h, static_cast<std::uint64_t>(pc.offset));
        h = hash_combine_u64(h, static_cast<std::uint64_t>(pc.size));
        h = hash_combine_u64(h, static_cast<std::uint64_t>(pc.stages));
    }
    return h;
}

[[nodiscard]] PipelineLayoutKey
make_pipeline_layout_key(const MergedShaderReflection &merged) {
    PipelineLayoutKey key;
    const auto by_set =
        descriptor_set_layout_bindings_by_set_from_merged_reflection(merged);
    std::uint32_t set_count = 0;
    if (!merged.resources.empty()) {
        std::uint32_t max_set = 0;
        for (const ShaderResource &r : merged.resources) {
            max_set = std::max(max_set, r.set);
        }
        set_count = max_set + 1U;
    }
    key.set_hashes.reserve(set_count);
    for (std::uint32_t s = 0; s < set_count; ++s) {
        const auto it = by_set.find(s);
        const std::vector<VkDescriptorSetLayoutBinding> empty;
        const std::vector<VkDescriptorSetLayoutBinding> &bindings =
            (it == by_set.end()) ? empty : it->second;
        key.set_hashes.push_back(hash_descriptor_set_layout_bindings(bindings));
    }
    key.push_constant_hash =
        hash_push_constants_for_pipeline_layout_key(merged.pushConstant);
    return key;
}

namespace std {
template <>
struct hash<PipelineLayoutKey> {
    std::size_t operator()(const PipelineLayoutKey &k) const noexcept {
        std::uint64_t h = k.push_constant_hash;
        for (SetLayoutHash sh : k.set_hashes) {
            h ^= sh + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return static_cast<std::size_t>(h);
    }
};
} // namespace std

using PipelineLayoutCache = std::unordered_map<PipelineLayoutKey, VkPipelineLayout>;

/// 以 `merged` 的绑定表 + push constant 指纹查缓存；未命中则创建并写入 `cache`。
/// 仅当 `set_layouts.size()` 与反射推导的 set 数（`key.set_hashes.size()`）一致时才使用缓存，
/// 以免相同反射、不同 `setLayoutCount` 时误复用 `VkPipelineLayout`。
[[nodiscard]] VkPipelineLayout get_or_create_pipeline_layout(
    PipelineLayoutCache &cache, VkDevice device,
    const MergedShaderReflection &merged,
    std::span<const VkDescriptorSetLayout> set_layouts,
    const VkAllocationCallbacks *allocator = nullptr) {
    const PipelineLayoutKey key = make_pipeline_layout_key(merged);
    const bool can_cache =
        (set_layouts.size() == key.set_hashes.size());
    if (can_cache) {
        if (const auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
    }
    VkPipelineLayout pl = create_pipeline_layout_from_merged_reflection(
        device, merged, set_layouts, allocator);
    if (can_cache) {
        cache.emplace(key, pl);
    }
    return pl;
}

void destroy_pipeline_layout_cache(VkDevice device, PipelineLayoutCache &cache,
                                   const VkAllocationCallbacks *allocator = nullptr) {
    for (const auto &kv : cache) {
        if (kv.second != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, kv.second, allocator);
        }
    }
    cache.clear();
}

[[nodiscard]] ResourceType
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

[[nodiscard]] VkFormat convert_format(SpvReflectFormat f) noexcept {
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
[[nodiscard]] std::uint32_t
vk_format_vertex_attribute_extent_bytes(const VkFormat f) noexcept {
    switch (f) {
    case VK_FORMAT_UNDEFINED:
        return 0U;
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT:
        return 2U;
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
        return 4U;
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16_SFLOAT:
        return 6U;
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8U;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
        return 4U;
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8U;
    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12U;
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16U;
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64_SFLOAT:
        return 8U;
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64_SFLOAT:
        return 16U;
    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64_SFLOAT:
        return 24U;
    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_R64G64B64A64_SFLOAT:
        return 32U;
    default:
        return 0U;
    }
}

[[nodiscard]] constexpr std::uint32_t
align_up_u32(const std::uint32_t x, const std::uint32_t a) noexcept {
    return (x + a - 1U) / a * a;
}

[[nodiscard]] VkVertexInputRate
lumen_vertex_input_rate_for_binding(const std::uint32_t binding) {
    if (binding == 0U) {
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }
    if (binding == 1U) {
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    throw std::invalid_argument(
        "vertex attribute binding must be 0 (per-vertex) or 1 (per-instance) "
        "for Lumen shader_reflect vertex layout convention");
}

struct PackedVertexInputState {
    std::vector<VkVertexInputBindingDescription> vk_bindings;
    std::vector<VkVertexInputAttributeDescription> vk_attributes;
    VkPipelineVertexInputStateCreateInfo create_info {};

    PackedVertexInputState() {
        create_info.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    }
};

[[nodiscard]] VkVertexInputRate vertex_input_rate_from_specs_or_convention(
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

[[nodiscard]] std::uint32_t vertex_stride_from_specs_or_computed(
    const std::uint32_t binding, const std::uint32_t computed_stride,
    const std::span<const VertexBinding> binding_specs) noexcept {
    for (const VertexBinding &vb : binding_specs) {
        if (vb.binding == binding && vb.stride != 0U) {
            return vb.stride;
        }
    }
    return computed_stride;
}

/// 由 `VertexAttribute` 生成 `VkPipelineVertexInputStateCreateInfo` 及附属数组（生命周期由
/// `PackedVertexInputState` 持有）。同一 `binding` 内按 `location` 递增紧密排 offset；
/// `binding_specs` 中非零 `stride` 覆盖自动计算的 stride，`rate` 覆盖约定（仍应与 binding
/// 语义一致）。
[[nodiscard]] PackedVertexInputState make_packed_vertex_input_state(
    const std::span<const VertexAttribute> attrs,
    const std::span<const VertexBinding> binding_specs) {
    PackedVertexInputState out;
    if (attrs.empty()) {
        out.create_info.vertexBindingDescriptionCount = 0U;
        out.create_info.pVertexBindingDescriptions = nullptr;
        out.create_info.vertexAttributeDescriptionCount = 0U;
        out.create_info.pVertexAttributeDescriptions = nullptr;
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
            throw std::invalid_argument(
                "make_packed_vertex_input_state: VK_FORMAT_UNDEFINED attribute");
        }
        (void)lumen_vertex_input_rate_for_binding(a.binding);
    }

    out.vk_attributes.reserve(sorted.size());
    out.vk_bindings.reserve(2U);

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
                throw std::invalid_argument(
                    "make_packed_vertex_input_state: unsupported VkFormat for "
                    "vertex attribute size");
            }
            VkVertexInputAttributeDescription ad {};
            ad.location = a.location;
            ad.binding = b;
            ad.format = a.format;
            ad.offset = offset;
            out.vk_attributes.push_back(ad);
            offset += extent;
            ++j;
        }
        const std::uint32_t computed_stride = align_up_u32(offset, 4U);
        if (computed_stride == 0U) {
            throw std::invalid_argument(
                "make_packed_vertex_input_state: computed stride is zero");
        }
        VkVertexInputBindingDescription bd {};
        bd.binding = b;
        bd.stride = vertex_stride_from_specs_or_computed(b, computed_stride,
                                                          binding_specs);
        bd.inputRate =
            vertex_input_rate_from_specs_or_convention(b, binding_specs);
        out.vk_bindings.push_back(bd);
        i = j;
    }

    out.create_info.vertexBindingDescriptionCount =
        static_cast<std::uint32_t>(out.vk_bindings.size());
    out.create_info.pVertexBindingDescriptions = out.vk_bindings.data();
    out.create_info.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(out.vk_attributes.size());
    out.create_info.pVertexAttributeDescriptions = out.vk_attributes.data();
    return out;
}

[[nodiscard]] inline PackedVertexInputState make_packed_vertex_input_state(
    const std::span<const VertexAttribute> attrs) {
    const std::vector<VertexBinding> defs = vertex_bindings_lumen_convention();
    return make_packed_vertex_input_state(attrs, defs);
}

[[nodiscard]] ShaderReflection
reflect_spirv(const std::vector<std::byte> &spirv,
              VkShaderStageFlagBits stage) {
    ShaderReflection reflection {};
    reflection.stage = stage;

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
            resource.stages = static_cast<VkShaderStageFlags>(reflection.stage);
            resource.offset = binding->block.offset;

            reflection.resources.push_back(resource);
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
        pushConstant.stages = static_cast<VkShaderStageFlags>(reflection.stage);
        reflection.pushConstant.push_back(pushConstant);
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

        if (inputVariable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
            continue;
        }

        input.format = convert_format(inputVariable->format);
        input.location = inputVariable->location;
        input.name = inputVariable->name;

        reflection.inputs.push_back(input);
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

        if (outputVariable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
            continue;
        }

        ShaderOutput output {};
        output.location = outputVariable->location;
        output.format = convert_format(outputVariable->format);
        output.name = outputVariable->name;
        reflection.outputs.push_back(output);
    }

    spvReflectDestroyShaderModule(&module);

    return reflection;
}

int main(int argc, char *argv[]) {
    const auto vertex_shader = load_spirv("./shaders/shader_reflect.vert.spv");
    const auto fragment_shader = load_spirv("./shaders/shader_reflect.frag.spv");

    const ShaderReflection reflection =
        reflect_spirv(vertex_shader, VK_SHADER_STAGE_VERTEX_BIT);
    const ShaderReflection fragment_reflection =
        reflect_spirv(fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT);

    const std::array<ShaderReflection, 2> parts { reflection,
                                                  fragment_reflection };
    const MergedShaderReflection merged = merge_shader_reflections(parts);

    const json j = reflection;
    // std::println("{}", j.dump(4));

    const json fragment_j = fragment_reflection;
    // std::println("{}", fragment_j.dump(4));

    const json merged_j = merged;
    std::println("{}", merged_j.dump(4));

    return 0;
}
