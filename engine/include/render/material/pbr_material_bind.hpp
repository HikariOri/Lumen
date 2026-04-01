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

#include "render/material/material.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"

namespace lumen::render {

class Context;
class CommandPool;

inline constexpr std::string_view PBR_FORWARD_VERT_SPV_RELATIVE {
    "shaders/pbr_forward.vert.spv"
};
inline constexpr std::string_view PBR_FORWARD_FRAG_SPV_RELATIVE {
    "shaders/pbr_forward.frag.spv"
};

/**
 * 与 `shaders/glsl/pbr_forward.frag` 中 DEBUG_* 一致（见 `note/shader
 * debug.md`）；写入 `PbrFrameUbo::debugMode`
 */
inline constexpr std::int32_t PBR_DEBUG_NONE { 0 };
inline constexpr std::int32_t PBR_DEBUG_NORMAL_WS { 1 };
inline constexpr std::int32_t PBR_DEBUG_NORMAL_TS { 2 };
inline constexpr std::int32_t PBR_DEBUG_DEPTH { 3 };
inline constexpr std::int32_t PBR_DEBUG_ALBEDO { 10 };
inline constexpr std::int32_t PBR_DEBUG_METALLIC { 11 };
inline constexpr std::int32_t PBR_DEBUG_ROUGHNESS { 12 };
inline constexpr std::int32_t PBR_DEBUG_AO { 13 };
inline constexpr std::int32_t PBR_DEBUG_DIRECT_DIFFUSE { 20 };
inline constexpr std::int32_t PBR_DEBUG_DIRECT_SPECULAR { 21 };
inline constexpr std::int32_t PBR_DEBUG_IBL_DIFFUSE { 22 };
inline constexpr std::int32_t PBR_DEBUG_IBL_SPECULAR { 23 };
inline constexpr std::int32_t PBR_DEBUG_EMISSIVE { 24 };
inline constexpr std::int32_t PBR_DEBUG_FINAL_NO_IBL { 30 };
inline constexpr std::int32_t PBR_DEBUG_FINAL_NO_DIRECT { 31 };
inline constexpr std::int32_t PBR_DEBUG_HEAT_LIGHT_INTENSITY { 50 };
inline constexpr std::int32_t PBR_DEBUG_HEAT_NDOTL { 51 };
inline constexpr std::int32_t PBR_DEBUG_HEAT_LIGHT_COUNT { 52 };

/** 4×4 分屏：第 i 格写入 FrameUBO 的 debugMode（与上表一致） */
inline constexpr std::array<std::int32_t, 16> PBR_FORWARD_DEBUG_TILE_MODES { {
    PBR_DEBUG_NONE,
    PBR_DEBUG_NORMAL_WS,
    PBR_DEBUG_NORMAL_TS,
    PBR_DEBUG_DEPTH,
    PBR_DEBUG_ALBEDO,
    PBR_DEBUG_METALLIC,
    PBR_DEBUG_ROUGHNESS,
    PBR_DEBUG_AO,
    PBR_DEBUG_DIRECT_DIFFUSE,
    PBR_DEBUG_DIRECT_SPECULAR,
    PBR_DEBUG_IBL_DIFFUSE,
    PBR_DEBUG_IBL_SPECULAR,
    PBR_DEBUG_EMISSIVE,
    PBR_DEBUG_FINAL_NO_IBL,
    PBR_DEBUG_FINAL_NO_DIRECT,
    PBR_DEBUG_HEAT_LIGHT_INTENSITY,
} };

/** Set 0：FrameUBO + IBL（irradiance / prefilter / BRDF LUT） */
[[nodiscard]] std::vector<DescriptorBinding>
pbr_frame_ibl_descriptor_bindings();

/** Set 1：MaterialUBO + 五张贴图 */
[[nodiscard]] std::vector<DescriptorBinding> pbr_material_descriptor_bindings();

/** @deprecated 使用 pbr_material_descriptor_bindings */
[[nodiscard]] inline std::vector<DescriptorBinding>
pbr_material_texture_descriptor_bindings() {
    return pbr_material_descriptor_bindings();
}

/** Set 2：ObjectUBO（动态 UBO，每 draw 变偏移） */
[[nodiscard]] std::vector<DescriptorBinding> pbr_object_descriptor_bindings();

/** Set 3：LightUBO */
[[nodiscard]] std::vector<DescriptorBinding> pbr_light_descriptor_bindings();

void write_pbr_frame_ibl_descriptor_set(VkDevice device, VkDescriptorSet set,
                                        VkBuffer frameUbo,
                                        std::size_t uboRange,
                                        const Texture &irradianceCube,
                                        const Texture &prefilterCube,
                                        const Texture &brdfLut);

void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer materialUbo,
    std::size_t materialUboRange, const Material &material,
    const PbrPlaceholderTextures &placeholders);

void write_pbr_object_descriptor_set_dynamic(VkDevice device,
                                             VkDescriptorSet set,
                                             VkBuffer objectUbo,
                                             std::size_t rangeOneObject);

void write_pbr_light_descriptor_set(VkDevice device, VkDescriptorSet set,
                                    VkBuffer lightUbo,
                                    std::size_t lightUboRange);

/** 旧名兼容（等同 Frame + IBL） */
[[nodiscard]] inline std::vector<DescriptorBinding>
pbr_scene_ibl_descriptor_bindings() {
    return pbr_frame_ibl_descriptor_bindings();
}

/** @deprecated 使用 pbr_frame_ibl_descriptor_set */
inline void write_pbr_scene_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer frameUbo,
    std::size_t uboRange, const Texture &irradianceCube,
    const Texture &prefilterCube, const Texture &brdfLut) {
    write_pbr_frame_ibl_descriptor_set(device, set, frameUbo, uboRange,
                                       irradianceCube, prefilterCube, brdfLut);
}

bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &outTexture, const Context &ctx, const char *metallicImagePath,
    const char *roughnessImagePath, VkQueue transferQueue,
    CommandPool &cmdPool);

} // namespace lumen::render
