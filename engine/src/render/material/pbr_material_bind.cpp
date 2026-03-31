/**
 * @file pbr_material_bind.cpp
 * @brief PBR 材质默认贴图、描述符写入、金属粗糙度合并
 */

#include "render/material/pbr_material_bind.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <stb_image.h>

#include "core/logger.hpp"
#include "render/context.hpp"
#include "render/resource/texture.hpp"
#include "scene/pbr_material.hpp"

namespace lumen::render {
namespace {

constexpr std::uint32_t k_pixel_rgba8 = 4U;

static_assert(sizeof(PbrForwardPushConstants) == 120U,
              "PbrForwardPushConstants 须与 helmet_pbr push_constant 一致");

} // namespace

std::vector<DescriptorBinding> pbr_scene_ibl_descriptor_bindings() {
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

std::vector<DescriptorBinding> pbr_material_texture_descriptor_bindings() {
    return {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
    };
}

void write_pbr_scene_ibl_descriptor_set(
    VkDevice device, VkDescriptorSet set, VkBuffer scene_ubo,
    std::size_t ubo_range, const Texture &irradiance_cube,
    const Texture &prefilter_cube, const Texture &brdf_lut) {

    write_descriptor_set(
        device, set,
        { { .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = scene_ubo,
            .offset = 0,
            .range = ubo_range } },
        { { .binding = 1,
            .imageView = irradiance_cube.view(),
            .sampler = irradiance_cube.sampler(),
            .imageLayout = irradiance_cube.descriptor_layout() },
          { .binding = 2,
            .imageView = prefilter_cube.view(),
            .sampler = prefilter_cube.sampler(),
            .imageLayout = prefilter_cube.descriptor_layout() },
          { .binding = 3,
            .imageView = brdf_lut.view(),
            .sampler = brdf_lut.sampler(),
            .imageLayout = brdf_lut.descriptor_layout() } });
}

void write_pbr_material_descriptor_set(
    VkDevice device, VkDescriptorSet set,
    const lumen::scene::PBRMaterial &material,
    const PbrPlaceholderTextures &placeholders) {

    const Texture &albedo =
        (material.base_color_tex != nullptr &&
         material.base_color_tex->is_valid())
            ? *material.base_color_tex
            : placeholders.albedo();
    const Texture &mr = (material.metallic_roughness_tex != nullptr &&
                         material.metallic_roughness_tex->is_valid())
                            ? *material.metallic_roughness_tex
                            : placeholders.metallic_roughness();
    const Texture &nrm =
        (material.normal_tex != nullptr && material.normal_tex->is_valid())
            ? *material.normal_tex
            : placeholders.normal();
    const Texture &occ =
        (material.occlusion_tex != nullptr &&
         material.occlusion_tex->is_valid())
            ? *material.occlusion_tex
            : placeholders.ao();
    const Texture &em =
        (material.emissive_tex != nullptr && material.emissive_tex->is_valid())
            ? *material.emissive_tex
            : placeholders.emissive();

    write_descriptor_set(
        device, set, {},
        { { .binding = 0,
            .imageView = albedo.view(),
            .sampler = albedo.sampler(),
            .imageLayout = albedo.descriptor_layout() },
          { .binding = 1,
            .imageView = mr.view(),
            .sampler = mr.sampler(),
            .imageLayout = mr.descriptor_layout() },
          { .binding = 2,
            .imageView = nrm.view(),
            .sampler = nrm.sampler(),
            .imageLayout = nrm.descriptor_layout() },
          { .binding = 3,
            .imageView = occ.view(),
            .sampler = occ.sampler(),
            .imageLayout = occ.descriptor_layout() },
          { .binding = 4,
            .imageView = em.view(),
            .sampler = em.sampler(),
            .imageLayout = em.descriptor_layout() } });
}

bool create_metallic_roughness_texture_from_grayscale_files(
    Texture &out_texture, const Context &ctx, const char *metallic_image_path,
    const char *roughness_image_path, VkQueue transfer_queue,
    CommandPool &cmd_pool) {

    int mw = 0;
    int mh = 0;
    int mc = 0;
    stbi_uc *m_data = stbi_load(metallic_image_path, &mw, &mh, &mc, 4);
    if (!m_data || mw <= 0 || mh <= 0) {
        if (m_data) {
            stbi_image_free(m_data);
        }
        LUMEN_LOG_ERROR("merge MR: 金属度图加载失败: {}",
                        metallic_image_path ? metallic_image_path : "");
        return false;
    }

    int rw = 0;
    int rh = 0;
    int rc = 0;
    stbi_uc *r_data = stbi_load(roughness_image_path, &rw, &rh, &rc, 4);
    if (!r_data || rw <= 0 || rh <= 0) {
        stbi_image_free(m_data);
        if (r_data) {
            stbi_image_free(r_data);
        }
        LUMEN_LOG_ERROR("merge MR: 粗糙度图加载失败: {}",
                        roughness_image_path ? roughness_image_path : "");
        return false;
    }

    const int out_w = (std::min)(mw, rw);
    const int out_h = (std::min)(mh, rh);
    std::vector<std::uint8_t> rgba(
        static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * k_pixel_rgba8);

    for (int y = 0; y < out_h; ++y) {
        for (int x = 0; x < out_w; ++x) {
            const int mi = (y * mw + x) * 4;
            const int ri = (y * rw + x) * 4;
            const std::uint8_t met = m_data[mi];
            const std::uint8_t rou = r_data[ri];
            const size_t oi =
                (static_cast<size_t>(y) * static_cast<size_t>(out_w) +
                 static_cast<size_t>(x)) *
                k_pixel_rgba8;
            rgba[oi + 0] = 0;
            rgba[oi + 1] = rou;
            rgba[oi + 2] = met;
            rgba[oi + 3] = 255;
        }
    }

    stbi_image_free(m_data);
    stbi_image_free(r_data);

    return out_texture.create_from_memory(
        ctx, rgba.data(), rgba.size(), static_cast<std::uint32_t>(out_w),
        static_cast<std::uint32_t>(out_h), transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, {}, true);
}

} // namespace lumen::render
