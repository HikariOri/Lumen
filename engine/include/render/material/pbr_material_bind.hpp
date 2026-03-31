/**
 * @file pbr_material_bind.hpp
 * @brief Forward PBR：`note/forward_shader.md` 四组 set 的描述符布局与写入
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "render/resource/descriptor.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "scene/pbr_material.hpp"

namespace lumen::render {

class Context;
class CommandPool;

inline constexpr std::string_view k_pbr_forward_vert_spv_relative {
    "shaders/pbr_forward.vert.spv"
};
inline constexpr std::string_view k_pbr_forward_frag_spv_relative {
    "shaders/pbr_forward.frag.spv"
};

/**
 * @brief 与 `pbr_forward.frag` push_constant 块对齐（std430，仅片元阶段）
 */
struct PbrForwardPushConstants {
    std::int32_t debug_view {};
    std::array<std::int32_t, 3> pad {};
};

/** Set 0：FrameUBO + IBL（irradiance / prefilter / BRDF LUT） */
[[nodiscard]] std::vector<DescriptorBinding> pbr_frame_ibl_descriptor_bindings();

/** Set 1：MaterialUBO + 五张贴图 */
[[nodiscard]] std::vector<DescriptorBinding>
pbr_material_descriptor_bindings();

/** @deprecated 使用 pbr_material_descriptor_bindings */
[[nodiscard]] inline std::vector<DescriptorBinding>
pbr_material_texture_descriptor_bindings() {
    return pbr_material_descriptor_bindings();
}

/** Set 2：ObjectUBO（动态 UBO，每 draw 变偏移） */
[[nodiscard]] std::vector<DescriptorBinding> pbr_object_descriptor_bindings();

/** Set 3：LightUBO */
[[nodiscard]] std::vector<DescriptorBinding> pbr_light_descriptor_bindings();

void write_pbr_frame_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer frame_ubo,
    std::size_t ubo_range, const Texture &irradiance_cube,
    const Texture &prefilter_cube, const Texture &brdf_lut);

void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer material_ubo,
    std::size_t material_ubo_range, const lumen::scene::PBRMaterial &material,
    const PbrPlaceholderTextures &placeholders);

void write_pbr_object_descriptor_set_dynamic(
    VkDevice device, VkDescriptorSet set, VkBuffer object_ubo,
    std::size_t range_one_object);

void write_pbr_light_descriptor_set(VkDevice device, VkDescriptorSet set,
                                    VkBuffer light_ubo,
                                    std::size_t light_ubo_range);

/** 旧名兼容（等同 Frame + IBL） */
[[nodiscard]] inline std::vector<DescriptorBinding>
pbr_scene_ibl_descriptor_bindings() {
    return pbr_frame_ibl_descriptor_bindings();
}

/** @deprecated 使用 pbr_frame_ibl_descriptor_set */
inline void write_pbr_scene_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer frame_ubo,
    std::size_t ubo_range, const Texture &irradiance_cube,
    const Texture &prefilter_cube, const Texture &brdf_lut) {
    write_pbr_frame_ibl_descriptor_set(device, set, frame_ubo, ubo_range,
                                       irradiance_cube, prefilter_cube,
                                       brdf_lut);
}

bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &out_texture, const Context &ctx, const char *metallic_image_path,
    const char *roughness_image_path, VkQueue transfer_queue,
    CommandPool &cmd_pool);

} // namespace lumen::render
