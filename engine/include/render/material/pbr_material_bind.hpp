/**
 * @file pbr_material_bind.hpp
 * @brief 前向 PBR：`note/forward_shader.md` 中四组 descriptor set
 * 的布局声明与写入接口
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

inline constexpr std::string_view PICK_ID_VERT_SPV_RELATIVE {
    "shaders/pick_id.vert.spv"
};
inline constexpr std::string_view PICK_ID_FRAG_SPV_RELATIVE {
    "shaders/pick_id.frag.spv"
};

inline constexpr std::string_view PICK_ID_VISUALIZE_VERT_SPV_RELATIVE {
    "shaders/pick_id_visualize.vert.spv"
};
inline constexpr std::string_view PICK_ID_VISUALIZE_FRAG_SPV_RELATIVE {
    "shaders/pick_id_visualize.frag.spv"
};

/**
 * @name PBR 片元调试模式
 * @brief 与 `shaders/glsl/pbr_forward.frag` 中 DEBUG_* 及 `note/shader
 * debug.md` 一致； 写入 `PbrFrameUbo::debugMode`（FrameUBO）。
 * @{
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
/** @} */

/**
 * @brief 4×4 分屏时，每一格对应的 FrameUBO `debugMode`（与 @c PBR_DEBUG_*
 * 一致）
 */
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

/**
 * @brief 构建 **Set 0** 的绑定布局：帧级 `PbrFrameUbo` + IBL（irradiance /
 * prefilter 立方体 + BRDF LUT）
 * @return 供 `DescriptorSetLayout::create` 使用的 binding 列表
 */
[[nodiscard]] std::vector<DescriptorBinding>
pbr_frame_ibl_descriptor_bindings();

/**
 * @brief 构建 **Set 1** 的绑定布局：`PbrMaterialUbo` + 五路 PBR
 * 贴图（albedo、MR、normal、AO、emissive）
 * @return 供 `DescriptorSetLayout::create` 使用的 binding 列表
 */
[[nodiscard]] std::vector<DescriptorBinding> pbr_material_descriptor_bindings();

/**
 * @brief 构建 **Set 2** 的绑定布局：每物体动态偏移的
 * `PbrObjectUbo`（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`）
 * @return 供 `DescriptorSetLayout::create` 使用的 binding 列表
 */
[[nodiscard]] std::vector<DescriptorBinding> pbr_object_descriptor_bindings();

/**
 * @brief 构建 **Set 3** 的绑定布局：场景光源 `PbrLightUbo`
 * @return 供 `DescriptorSetLayout::create` 使用的 binding 列表
 */
[[nodiscard]] std::vector<DescriptorBinding> pbr_light_descriptor_bindings();

/**
 * @brief 写入 **Set 0**：绑定 FrameUBO 与三张 IBL 相关纹理（combined image
 * sampler）
 * @param device            逻辑设备
 * @param descriptorSet     已分配的 descriptor set
 * @param frameUbo          含 `PbrFrameUbo` 的 uniform buffer
 * @param frameUboRange     上述 UBO 的字节范围（通常为一帧结构体大小）
 * @param irradianceCube    漫反射 IBL 立方体贴图
 * @param prefilterCube     预过滤环境镜面立方体贴图
 * @param brdfLut           BRDF 积分 LUT（2D）
 */
void write_pbr_frame_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet descriptorSet, VkBuffer frameUbo,
    std::size_t frameUboRange, const Texture &irradianceCube,
    const Texture &prefilterCube, const Texture &brdfLut);

/**
 * @brief 写入 **Set 1**：绑定 `PbrMaterialUbo` 与五张贴图；若 `Material`
 * 中某槽为空或无效则回退到 @p placeholders
 * @param device             逻辑设备
 * @param descriptorSet      已分配的 descriptor set
 * @param materialUbo        含 `PbrMaterialUbo` 的 uniform buffer
 * @param materialUboRange   上述 UBO 的字节范围
 * @param material           CPU 侧材质（因子 + 贴图指针）
 * @param placeholders       引擎默认 1×1 占位纹理，避免绑定未初始化图像
 */
void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet descriptorSet, VkBuffer materialUbo,
    std::size_t materialUboRange, const Material &material,
    const PbrPlaceholderTextures &placeholders);

/**
 * @brief 写入 **Set 2**：仅绑定动态 UBO；实际物体数据通过
 * `vkCmdBindDescriptorSets` 的 @c pDynamicOffsets 选择偏移
 * @param device           逻辑设备
 * @param descriptorSet    已分配的 descriptor set
 * @param objectUbo         backing store（可含多份 `PbrObjectUbo` 连续排列）
 * @param perObjectRange   单份 `PbrObjectUbo` 的字节跨度（须满足设备
 * `minUniformBufferOffsetAlignment`）
 */
void write_pbr_object_descriptor_set_dynamic(VkDevice device,
                                             VkDescriptorSet descriptorSet,
                                             VkBuffer objectUbo,
                                             std::size_t perObjectRange);

/**
 * @brief 写入 **Set 3**：绑定 `PbrLightUbo` 所在 uniform buffer
 * @param device          逻辑设备
 * @param descriptorSet   已分配的 descriptor set
 * @param lightUbo        光源 UBO
 * @param lightUboRange   上述 UBO 的字节范围
 */
void write_pbr_light_descriptor_set(VkDevice device,
                                    VkDescriptorSet descriptorSet,
                                    VkBuffer lightUbo,
                                    std::size_t lightUboRange);

/**
 * @brief 从两张灰度图（金属度、粗糙度各一张）合并为一张 **glTF MR** 纹理：R
 * 保留、G=粗糙度、B=金属度、A=255
 * @param[out] outTexture    输出的 `Texture`（`VK_FORMAT_R8G8B8A8_UNORM`）
 * @param ctx                渲染上下文
 * @param metallicPath       金属度图像路径（磁盘路径，由调用方保证可读）
 * @param roughnessPath      粗糙度图像路径
 * @param transferQueue      用于上传的传输/图形队列
 * @param commandPool        用于提交临时 copy 命令的 command pool
 * @return 成功创建并上传则为 @c true
 */
bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &outTexture, const Context &ctx, const char *metallicPath,
    const char *roughnessPath, VkQueue transferQueue, CommandPool &commandPool);

} // namespace lumen::render
