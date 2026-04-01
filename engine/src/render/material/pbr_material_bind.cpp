/**
 * @file pbr_material_bind.cpp
 * @brief Forward PBR 描述符写入、材质默认贴图、金属粗糙度合并
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

constexpr std::uint32_t RGBA8_BYTES_PER_PIXEL = 4U;

} // namespace

std::vector<DescriptorBinding> pbr_frame_ibl_descriptor_bindings() {
    return {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .count = 1,
          .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 1,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 2,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 3,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
}

std::vector<DescriptorBinding> pbr_material_descriptor_bindings() {
    return {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 1,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 2,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 3,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 4,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 5,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
}

std::vector<DescriptorBinding> pbr_object_descriptor_bindings() {
    return { { .binding = 0,
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
              .count = 1,
              .stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
          } };
}

std::vector<DescriptorBinding> pbr_light_descriptor_bindings() {
    return { { .binding = 0,
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .count = 1,
              .stages = VK_SHADER_STAGE_FRAGMENT_BIT } };
}

void write_pbr_frame_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer frameUbo,
    std::size_t uboRange, const Texture &irradianceCube,
    const Texture &prefilterCube, const Texture &brdfLut) {

    write_descriptor_set(
        device, set,
        { { .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = frameUbo,
            .offset = 0,
            .range = uboRange } },
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

void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer materialUbo,
    std::size_t materialUboRange, const Material &material,
    const PbrPlaceholderTextures &placeholders) {

    const Texture &albedo =
        (material.baseColorTex != nullptr &&
         material.baseColorTex->is_valid())
            ? *material.baseColorTex
            : placeholders.albedo();
    const Texture &mr = (material.metallicRoughnessTex != nullptr &&
                         material.metallicRoughnessTex->is_valid())
                            ? *material.metallicRoughnessTex
                            : placeholders.metallic_roughness();
    const Texture &nrm =
        (material.normalTex != nullptr && material.normalTex->is_valid())
            ? *material.normalTex
            : placeholders.normal();
    const Texture &occ =
        (material.occlusionTex != nullptr &&
         material.occlusionTex->is_valid())
            ? *material.occlusionTex
            : placeholders.ao();
    const Texture &em =
        (material.emissiveTex != nullptr && material.emissiveTex->is_valid())
            ? *material.emissiveTex
            : placeholders.emissive();

    write_descriptor_set(
        device, set,
        { { .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = materialUbo,
            .offset = 0,
            .range = materialUboRange } },
        { { .binding = 1,
            .imageView = albedo.view(),
            .sampler = albedo.sampler(),
            .imageLayout = albedo.descriptor_layout() },
          { .binding = 2,
            .imageView = mr.view(),
            .sampler = mr.sampler(),
            .imageLayout = mr.descriptor_layout() },
          { .binding = 3,
            .imageView = nrm.view(),
            .sampler = nrm.sampler(),
            .imageLayout = nrm.descriptor_layout() },
          { .binding = 4,
            .imageView = occ.view(),
            .sampler = occ.sampler(),
            .imageLayout = occ.descriptor_layout() },
          { .binding = 5,
            .imageView = em.view(),
            .sampler = em.sampler(),
            .imageLayout = em.descriptor_layout() } });
}

void write_pbr_object_descriptor_set_dynamic(
    VkDevice device, VkDescriptorSet set, VkBuffer objectUbo,
    std::size_t rangeOneObject) {
    write_descriptor_set(
        device, set,
        { { .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .buffer = objectUbo,
            .offset = 0,
            .range = rangeOneObject } },
        {});
}

void write_pbr_light_descriptor_set(VkDevice device, VkDescriptorSet set,
                                    VkBuffer lightUbo,
                                    std::size_t lightUboRange) {
    write_descriptor_buffer(device, set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            lightUbo, 0, lightUboRange);
}

bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &outTexture, const Context &ctx, const char *metallicImagePath,
    const char *roughnessImagePath, VkQueue transferQueue,
    CommandPool &cmdPool) {

    int mw = 0;
    int mh = 0;
    int mc = 0;
    stbi_uc *m_data = stbi_load(metallicImagePath, &mw, &mh, &mc, 4);
    if (!m_data || mw <= 0 || mh <= 0) {
        if (m_data) {
            stbi_image_free(m_data);
        }
        LUMEN_LOG_ERROR("merge MR: 金属度图加载失败: {}",
                        metallicImagePath ? metallicImagePath : "");
        return false;
    }

    int rw = 0;
    int rh = 0;
    int rc = 0;
    stbi_uc *r_data = stbi_load(roughnessImagePath, &rw, &rh, &rc, 4);
    if (!r_data || rw <= 0 || rh <= 0) {
        stbi_image_free(m_data);
        if (r_data) {
            stbi_image_free(r_data);
        }
        LUMEN_LOG_ERROR("merge MR: 粗糙度图加载失败: {}",
                        roughnessImagePath ? roughnessImagePath : "");
        return false;
    }

    const int out_w = (std::min)(mw, rw);
    const int out_h = (std::min)(mh, rh);
    std::vector<std::uint8_t> rgba(
        static_cast<size_t>(out_w) * static_cast<size_t>(out_h) *
        RGBA8_BYTES_PER_PIXEL);

    for (int y = 0; y < out_h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            const int mi = (y * mw + x) * 4;
            const int ri = (y * rw + x) * 4;
            const std::uint8_t met = m_data[mi];
            const std::uint8_t rou = r_data[ri];
            const size_t oi =
                (static_cast<size_t>(y) * static_cast<size_t>(out_w) +
                 static_cast<size_t>(x)) *
                RGBA8_BYTES_PER_PIXEL;
            rgba[oi + 0] = 0;
            rgba[oi + 1] = rou;
            rgba[oi + 2] = met;
            rgba[oi + 3] = 255;
        }
    }

    stbi_image_free(m_data);
    stbi_image_free(r_data);

    return outTexture.create_from_memory(
        ctx, rgba.data(), rgba.size(), static_cast<std::uint32_t>(out_w),
        static_cast<std::uint32_t>(out_h), transferQueue, cmdPool,
        VK_FORMAT_R8G8B8A8_UNORM, {}, true);
}

} // namespace lumen::render
