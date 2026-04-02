/**
 * @file pbr_material_bind.cpp
 * @brief 前向 PBR 描述符布局/写入实现，以及金属–粗糙度灰度图合并上传
 *
 * @details
 * 各公开 API 的语义与参数说明见 `pbr_material_bind.hpp`。
 */

#include "render/material/pbr_material_bind.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <stb_image.h>

#include "core/logger.hpp"
#include "render/context.hpp"
#include "render/resource/texture.hpp"

namespace lumen::render {
namespace {

constexpr std::uint32_t kBytesPerRgba8Pixel = 4U;

} // namespace

/**
 * @brief 实现 @ref pbr_frame_ibl_descriptor_bindings
 */
std::vector<DescriptorBinding> pbr_frame_ibl_descriptor_bindings() {
    return {
        { .binding = 0,
          .type = vk::DescriptorType::eUniformBuffer,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eVertex |
                    vk::ShaderStageFlagBits::eFragment },
        { .binding = 1,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 2,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 3,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
    };
}

/**
 * @brief 实现 @ref pbr_material_descriptor_bindings
 */
std::vector<DescriptorBinding> pbr_material_descriptor_bindings() {
    return {
        { .binding = 0,
          .type = vk::DescriptorType::eUniformBuffer,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 1,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 2,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 3,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 4,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
        { .binding = 5,
          .type = vk::DescriptorType::eCombinedImageSampler,
          .count = 1,
          .stages = vk::ShaderStageFlagBits::eFragment },
    };
}

/**
 * @brief 实现 @ref pbr_object_descriptor_bindings
 */
std::vector<DescriptorBinding> pbr_object_descriptor_bindings() {
    return { { .binding = 0,
               .type = vk::DescriptorType::eUniformBufferDynamic,
               .count = 1,
               .stages = vk::ShaderStageFlagBits::eVertex |
                         vk::ShaderStageFlagBits::eFragment } };
}

/**
 * @brief 实现 @ref pbr_light_descriptor_bindings
 */
std::vector<DescriptorBinding> pbr_light_descriptor_bindings() {
    return { { .binding = 0,
               .type = vk::DescriptorType::eUniformBuffer,
               .count = 1,
               .stages = vk::ShaderStageFlagBits::eFragment } };
}

/**
 * @brief 实现 @ref write_pbr_frame_ibl_descriptor_set
 */
void write_pbr_frame_ibl_descriptor_set(
    vk::Device device, vk::DescriptorSet descriptorSet, vk::Buffer frameUbo,
    std::size_t frameUboRange, const Texture &irradianceCube,
    const Texture &prefilterCube, const Texture &brdfLut) {

    write_descriptor_set(
        device, descriptorSet,
        { { .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .buffer = frameUbo,
            .offset = 0,
            .range = frameUboRange } },
        { { .binding = 1,
            .imageView = irradianceCube.view(),
            .sampler = irradianceCube.sampler(),
            .imageLayout = irradianceCube.descriptor_layout() },
          { .binding = 2,
            .imageView = prefilterCube.view(),
            .sampler = prefilterCube.sampler(),
            .imageLayout = prefilterCube.descriptor_layout() },
          { .binding = 3,
            .imageView = brdfLut.view(),
            .sampler = brdfLut.sampler(),
            .imageLayout = brdfLut.descriptor_layout() } });
}

/**
 * @brief 实现 @ref write_pbr_material_descriptor_set
 */
void write_pbr_material_descriptor_set(
    vk::Device device, vk::DescriptorSet descriptorSet, vk::Buffer materialUbo,
    std::size_t materialUboRange, const Material &material,
    const PbrPlaceholderTextures &placeholders) {

    const Texture &texAlbedo =
        (material.baseColorTex != nullptr && material.baseColorTex->is_valid())
            ? *material.baseColorTex
            : placeholders.albedo();
    const Texture &texMetallicRoughness =
        (material.metallicRoughnessTex != nullptr &&
         material.metallicRoughnessTex->is_valid())
            ? *material.metallicRoughnessTex
            : placeholders.metallic_roughness();
    const Texture &texNormal =
        (material.normalTex != nullptr && material.normalTex->is_valid())
            ? *material.normalTex
            : placeholders.normal();
    const Texture &texOcclusion =
        (material.occlusionTex != nullptr && material.occlusionTex->is_valid())
            ? *material.occlusionTex
            : placeholders.ao();
    const Texture &texEmissive =
        (material.emissiveTex != nullptr && material.emissiveTex->is_valid())
            ? *material.emissiveTex
            : placeholders.emissive();

    write_descriptor_set(device, descriptorSet,
                         { { .binding = 0,
                             .type = vk::DescriptorType::eUniformBuffer,
                             .buffer = materialUbo,
                             .offset = 0,
                             .range = materialUboRange } },
                         { { .binding = 1,
                             .imageView = texAlbedo.view(),
                             .sampler = texAlbedo.sampler(),
                             .imageLayout = texAlbedo.descriptor_layout() },
                           { .binding = 2,
                             .imageView = texMetallicRoughness.view(),
                             .sampler = texMetallicRoughness.sampler(),
                             .imageLayout = texMetallicRoughness.descriptor_layout() },
                           { .binding = 3,
                             .imageView = texNormal.view(),
                             .sampler = texNormal.sampler(),
                             .imageLayout = texNormal.descriptor_layout() },
                           { .binding = 4,
                             .imageView = texOcclusion.view(),
                             .sampler = texOcclusion.sampler(),
                             .imageLayout = texOcclusion.descriptor_layout() },
                           { .binding = 5,
                             .imageView = texEmissive.view(),
                             .sampler = texEmissive.sampler(),
                             .imageLayout = texEmissive.descriptor_layout() } });
}

/**
 * @brief 实现 @ref write_pbr_object_descriptor_set_dynamic
 */
void write_pbr_object_descriptor_set_dynamic(vk::Device device,
                                             vk::DescriptorSet descriptorSet,
                                             vk::Buffer objectUbo,
                                             std::size_t perObjectRange) {
    write_descriptor_set(device, descriptorSet,
                         { { .binding = 0,
                             .type = vk::DescriptorType::eUniformBufferDynamic,
                             .buffer = objectUbo,
                             .offset = 0,
                             .range = perObjectRange } },
                         {});
}

/**
 * @brief 实现 @ref write_pbr_light_descriptor_set
 */
void write_pbr_light_descriptor_set(vk::Device device,
                                    vk::DescriptorSet descriptorSet,
                                    vk::Buffer lightUbo,
                                    std::size_t lightUboRange) {
    write_descriptor_buffer(device, descriptorSet, 0,
                            vk::DescriptorType::eUniformBuffer, lightUbo, 0,
                            lightUboRange);
}

/**
 * @brief 实现 @ref create_metallic_roughness_texture_from_grayscale_files
 */
bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &outTexture, const Context &ctx, const char *metallicPath,
    const char *roughnessPath, vk::Queue transferQueue,
    CommandPool &commandPool) {

    int metalWidth = 0;
    int metalHeight = 0;
    int metalChannels = 0;
    stbi_uc *metalPixels =
        stbi_load(metallicPath, &metalWidth, &metalHeight, &metalChannels, 4);
    if (!metalPixels || metalWidth <= 0 || metalHeight <= 0) {
        if (metalPixels) {
            stbi_image_free(metalPixels);
        }
        LUMEN_LOG_ERROR("merge MR: 金属度图加载失败: {}",
                        metallicPath ? metallicPath : "");
        return false;
    }

    int roughWidth = 0;
    int roughHeight = 0;
    int roughChannels = 0;
    stbi_uc *roughPixels =
        stbi_load(roughnessPath, &roughWidth, &roughHeight, &roughChannels, 4);
    if (!roughPixels || roughWidth <= 0 || roughHeight <= 0) {
        stbi_image_free(metalPixels);
        if (roughPixels) {
            stbi_image_free(roughPixels);
        }
        LUMEN_LOG_ERROR("merge MR: 粗糙度图加载失败: {}",
                        roughnessPath ? roughnessPath : "");
        return false;
    }

    const int outWidth = (std::min)(metalWidth, roughWidth);
    const int outHeight = (std::min)(metalHeight, roughHeight);
    std::vector<std::uint8_t> outRgba(static_cast<std::size_t>(outWidth) *
                                      static_cast<std::size_t>(outHeight) *
                                      kBytesPerRgba8Pixel);

    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const int idxMetal = (y * metalWidth + x) * 4;
            const int idxRough = (y * roughWidth + x) * 4;
            const std::uint8_t metallicByte = metalPixels[idxMetal];
            const std::uint8_t roughnessByte = roughPixels[idxRough];
            const std::size_t idxOut =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) +
                 static_cast<std::size_t>(x)) *
                kBytesPerRgba8Pixel;
            outRgba[idxOut + 0] = 0;
            outRgba[idxOut + 1] = roughnessByte;
            outRgba[idxOut + 2] = metallicByte;
            outRgba[idxOut + 3] = 255;
        }
    }

    stbi_image_free(metalPixels);
    stbi_image_free(roughPixels);

    return outTexture.create_from_memory(
        ctx, outRgba.data(), outRgba.size(),
        static_cast<std::uint32_t>(outWidth),
        static_cast<std::uint32_t>(outHeight), transferQueue, commandPool,
        vk::Format::eR8G8B8A8Unorm, {}, true);
}

} // namespace lumen::render
