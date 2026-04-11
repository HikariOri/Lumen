/**
 * @file shader/reflection/spirv_reflect_json.hpp
 * @brief `shader::reflection` 与 nlohmann::json 之间的转换声明（实现见 spirv_reflect_json.cpp）。
 * @note 本模块已避免使用 `std::exception`；但 `void from_json(...)` 中若键缺失或类型不匹配，
 *       `nlohmann::json::at` / `get_ref` 仍可能抛出 `nlohmann::json::exception`。
 */
#pragma once

#include <expected>
#include <string_view>

#include <nlohmann/json.hpp>

#include "shader/reflection/spirv_reflect.hpp"

namespace shader::reflection {

using json = nlohmann::json;

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

/**
 * @brief ASCII 大小写不敏感比较。
 * @param a 左操作数。
 * @param b 右操作数。
 * @return 长度与逐字符（tolower）均相等时为 true。
 */
[[nodiscard]] bool ascii_equals_ignore_case(std::string_view a,
                                            std::string_view b) noexcept;

/**
 * @brief 将 VkFormat 序列化为 JSON 字符串（名或 RAW_ 整数形式）。
 */
[[nodiscard]] std::string vk_format_to_string(VkFormat v);

/**
 * @brief 从短名或 RAW_ 数字解析 VkFormat。
 * @param s 枚举名或 `RAW_<uint32>`。
 * @return 成功为格式值；失败为错误说明。
 */
[[nodiscard]] std::expected<VkFormat, ReflectionError>
vk_format_try_from_string(std::string_view s);

void to_json(json &j, VkFormat v);

void from_json(const json &j, VkFormat &v);

void to_json(json &j, VkShaderStageFlags v);

void from_json(const json &j, VkShaderStageFlags &v);

/**
 * @brief 去掉阶段名字符串两端的空白。
 */
[[nodiscard]] std::string_view trim_stage_token(std::string_view s) noexcept;

/**
 * @brief 将单个小写阶段名解析为 VkShaderStageFlags 单 bit。
 * @param name 如 `vertex`、`fragment`。
 * @return 成功为对应位；未知名或当前设备不支持 mesh/task 时返回错误。
 */
[[nodiscard]] std::expected<VkShaderStageFlags, ReflectionError>
shader_stage_flag_try_from_name(std::string_view name);

/**
 * @brief 解析 ShaderResource::stages 的 JSON：无符号整数、`"a|b"` 或字符串数组。
 * @param j JSON 值。
 * @return 成功为位掩码；类型或内容不合法时返回错误。
 */
[[nodiscard]] std::expected<VkShaderStageFlags, ReflectionError>
parse_shader_resource_stages_try_from_json(const json &j);

void to_json(json &j, VkShaderStageFlagBits v);

void from_json(const json &j, VkShaderStageFlagBits &v);

void to_json(json &j, const ShaderResource &r);

void from_json(const json &j, ShaderResource &r);

void to_json(json &j, const ShaderInput &i);

void from_json(const json &j, ShaderInput &i);

void to_json(json &j, const ShaderOutput &o);

void from_json(const json &j, ShaderOutput &o);

void to_json(json &j, const PushConstant &p);

void from_json(const json &j, PushConstant &p);

void to_json(json &j, const ShaderReflection &r);

void from_json(const json &j, ShaderReflection &r);

void to_json(json &j, const MergedShaderReflection &m);

void from_json(const json &j, MergedShaderReflection &m);

} // namespace shader::reflection
