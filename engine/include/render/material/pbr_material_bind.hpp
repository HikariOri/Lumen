/**
 * @file pbr_material_bind.hpp
 * @brief PBR forward：场景 IBL（set=0）与材质贴图（set=1）描述符布局与写入
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/mat4x4.hpp>

#include "render/resource/descriptor.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "scene/pbr_material.hpp"

namespace lumen::render {

class Context;
class CommandPool;

/**
 * @brief 与 `helmet_pbr.*` push_constant 块对齐（std430 规则）
 */
struct PbrForwardPushConstants {
    glm::mat4 model { 1.0F };
    glm::vec4 base_color_factor { 1.0F, 1.0F, 1.0F, 1.0F };
    /// rgb：emissiveFactor；w：额外倍率（编辑器）
    glm::vec4 emissive_factor_and_scale { 0.0F, 0.0F, 0.0F, 1.0F };
    float metallic_factor { 1.0F };
    float roughness_factor { 1.0F };
    std::int32_t debug_view {};
    std::array<std::int32_t, 3> pad { { 0, 0, 0 } };
};

/**
 * @brief set=0：SceneUBO + irradianceCube + prefilterCube + brdfLUT
 */
[[nodiscard]] std::vector<DescriptorBinding> pbr_scene_ibl_descriptor_bindings();

/**
 * @brief set=1：baseColor、metallicRoughness、normal、occlusion、emissive
 */
[[nodiscard]] std::vector<DescriptorBinding>
pbr_material_texture_descriptor_bindings();

void write_pbr_scene_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer scene_ubo,
    std::size_t ubo_range, const Texture &irradiance_cube,
    const Texture &prefilter_cube, const Texture &brdf_lut);

void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet set,
    const lumen::scene::PBRMaterial &material,
    const PbrPlaceholderTextures &placeholders);

/**
 * @brief 将两张灰度（R 通道）金属度/粗糙度贴图合并为 glTF 式 RGBA8 UNORM（G=
 * roughness，B=metallic）
 */
bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &out_texture, const Context &ctx, const char *metallic_image_path,
    const char *roughness_image_path, VkQueue transfer_queue,
    CommandPool &cmd_pool);

} // namespace lumen::render
