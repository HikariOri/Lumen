#include "shader/reflection/spirv_reflect_json.hpp"

#include "core/log/logger.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#ifndef NDEBUG
#define SHADER_REFLECTION_LOG_WARN(Msg)                                        \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine()) {                         \
            _l->log(::spdlog::source_loc { __FILE__, __LINE__,                 \
                                          SPDLOG_FUNCTION },                   \
                    ::core::log::detail::to_spdlog(::core::log::LogLevel::Warn), \
                    (Msg));                                                    \
        }                                                                      \
    } while (0)
#else
#define SHADER_REFLECTION_LOG_WARN(Msg) ((void)0)
#endif

namespace shader::reflection {


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

[[nodiscard]] bool ascii_equals_ignore_case(std::string_view a,
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

[[nodiscard]] std::string vk_format_to_string(const VkFormat v) {
    for (const auto &e : k_vk_format_json) {
        if (e.format == v) {
            return std::string { e.name };
        }
    }
    return std::string { "RAW_" } +
           std::to_string(static_cast<std::uint32_t>(v));
}

[[nodiscard]] std::expected<VkFormat, ReflectionError>
vk_format_try_from_string(const std::string_view s) {
    if (s.size() > 4U && ascii_equals_ignore_case(s.substr(0U, 4U), "RAW_")) {
        std::uint32_t value = 0U;
        const char *const first = s.data() + 4U;
        const char *const last = s.data() + s.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc {} || ptr != last) {
            SHADER_REFLECTION_LOG_WARN(std::format(
                "spirv_reflect_json: invalid VkFormat RAW_ string {}",
                std::string { s }));
            return std::unexpected(
                ReflectionError { "invalid VkFormat RAW_ string" });
        }
        return static_cast<VkFormat>(value);
    }
    for (const auto &e : k_vk_format_json) {
        if (ascii_equals_ignore_case(s, e.name)) {
            return e.format;
        }
    }
    SHADER_REFLECTION_LOG_WARN(std::format(
        "spirv_reflect_json: unknown VkFormat string {}", std::string { s }));
    return std::unexpected(ReflectionError { "unknown VkFormat string" });
}

void to_json(json &j, VkFormat v) {
    j = vk_format_to_string(v);
}

void from_json(const json &j, VkFormat &v) {
    if (j.is_number_unsigned()) {
        v = static_cast<VkFormat>(j.get<std::uint32_t>());
        return;
    }
    const auto &s = j.get_ref<const std::string &>();
    if (const auto parsed = vk_format_try_from_string(s)) {
        v = *parsed;
    } else {
        v = VK_FORMAT_UNDEFINED;
    }
}

void to_json(json &j, VkShaderStageFlags v) {
    j = static_cast<std::uint32_t>(v);
}

void from_json(const json &j, VkShaderStageFlags &v) {
    v = static_cast<VkShaderStageFlags>(j.get<std::uint32_t>());
}

[[nodiscard]] std::string_view trim_stage_token(std::string_view s) noexcept {
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

[[nodiscard]] std::expected<VkShaderStageFlags, ReflectionError>
shader_stage_flag_try_from_name(const std::string_view name) {
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
            return std::unexpected(
                ReflectionError { "task shader stage not supported" });
        }
        return k_task_shader_stage_bit;
    }
    if (name == "mesh") {
        if (k_mesh_shader_stage_bit == VkShaderStageFlags { 0 }) {
            return std::unexpected(
                ReflectionError { "mesh shader stage not supported" });
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
    return std::unexpected(ReflectionError { "unknown shader stage name" });
}

[[nodiscard]] std::expected<VkShaderStageFlags, ReflectionError>
parse_shader_resource_stages_try_from_json(const json &j) {
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
                const auto bit = shader_stage_flag_try_from_name(tok);
                if (!bit) {
                    return std::unexpected(bit.error());
                }
                flags |= *bit;
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
            const auto bit = shader_stage_flag_try_from_name(name);
            if (!bit) {
                return std::unexpected(bit.error());
            }
            flags |= *bit;
        }
        return flags;
    }
    return std::unexpected(ReflectionError {
        "resource stages must be a pipe-separated string, string array, or "
        "unsigned integer" });
}

void to_json(json &j, VkShaderStageFlagBits v) {
    j = detail::shader_stage_flags_to_string(static_cast<VkShaderStageFlags>(v));
}

void from_json(const json &j, VkShaderStageFlagBits &v) {
    const auto flags_or = parse_shader_resource_stages_try_from_json(j);
    if (!flags_or) {
        v = static_cast<VkShaderStageFlagBits>(0);
        return;
    }
    const auto u = static_cast<std::uint32_t>(*flags_or);
    if (u != 0U && (u & (u - 1U)) != 0U) {
        v = static_cast<VkShaderStageFlagBits>(0);
        return;
    }
    v = static_cast<VkShaderStageFlagBits>(*flags_or);
}

void to_json(json &j, const ShaderResource &r) {
    j = json { { "set", r.set },   { "binding", r.binding },
               { "type", r.type }, { "count", r.count },
               { "size", r.size }, { "offset", r.offset },
               { "name", r.name },
               { "stages", detail::shader_stage_flags_to_string(r.stages) } };
}

void from_json(const json &j, ShaderResource &r) {
    j.at("set").get_to(r.set);
    j.at("binding").get_to(r.binding);
    j.at("type").get_to(r.type);
    j.at("count").get_to(r.count);
    j.at("size").get_to(r.size);
    j.at("offset").get_to(r.offset);
    j.at("name").get_to(r.name);
    const auto stages_or = parse_shader_resource_stages_try_from_json(j.at("stages"));
    r.stages = stages_or ? *stages_or : VkShaderStageFlags { 0 };
}

void to_json(json &j, const ShaderInput &i) {
    j = json { { "location", i.location },
               { "format", i.format },
               { "name", i.name } };
}

void from_json(const json &j, ShaderInput &i) {
    j.at("location").get_to(i.location);
    j.at("format").get_to(i.format);
    j.at("name").get_to(i.name);
}

void to_json(json &j, const ShaderOutput &o) {
    j = json { { "location", o.location },
               { "format", o.format },
               { "name", o.name } };
}

void from_json(const json &j, ShaderOutput &o) {
    j.at("location").get_to(o.location);
    j.at("format").get_to(o.format);
    j.at("name").get_to(o.name);
}

void to_json(json &j, const PushConstant &p) {
    j = json { { "size", p.size },
               { "offset", p.offset },
               { "stages", p.stages } };
}

void from_json(const json &j, PushConstant &p) {
    j.at("size").get_to(p.size);
    j.at("offset").get_to(p.offset);
    j.at("stages").get_to(p.stages);
}

void to_json(json &j, const ShaderReflection &r) {
    j = json { { "stage", r.stage() },
               { "resources", r.resources() },
               { "inputs", r.inputs() },
               { "outputs", r.outputs() },
               { "push_constant", r.pushConstants() } };
}

void from_json(const json &j, ShaderReflection &r) {
    j.at("stage").get_to(r.stage_);
    j.at("resources").get_to(r.resources_);
    j.at("inputs").get_to(r.inputs_);
    j.at("outputs").get_to(r.outputs_);
    j.at("push_constant").get_to(r.pushConstants_);
}

void to_json(json &j, const MergedShaderReflection &m) {
    j = json { { "stage", detail::shader_stage_flags_to_string(m.stages()) },
               { "resources", m.resources() },
               { "vertex_input", m.vertexInput() },
               { "fragment_output", m.fragmentOutput() },
               { "push_constant", m.pushConstants() } };
}

void from_json(const json &j, MergedShaderReflection &m) {
    m.destroy();
    const auto stages_or =
        parse_shader_resource_stages_try_from_json(j.at("stage"));
    m.stages_ = stages_or ? *stages_or : VkShaderStageFlags { 0 };
    j.at("resources").get_to(m.resources_);
    j.at("vertex_input").get_to(m.vertexInput_);
    j.at("fragment_output").get_to(m.fragmentOutput_);
    j.at("push_constant").get_to(m.pushConstants_);
    m.descriptorBindingsBySetCache_.reset();
    m.packedVertexInputStateCache_.reset();
    m.rebuild_push_constant_ranges_for_pipeline();
}

} // namespace shader::reflection
