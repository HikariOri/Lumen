/**
 * @file asset/material_fingerprint.cpp
 */

#include "asset/material_fingerprint.hpp"

#include "asset/sampler_fingerprint.hpp"
#include "asset/texture_registry.hpp"

#include "render/material/material.hpp"

#include "render/vulkan.hpp"

#include <cstring>
#include <string>

namespace lumen::asset {
namespace {

void append_f32_bits(std::string &out, float v) {
    std::uint32_t u {};
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(&u, &v, sizeof(float));
    out += std::to_string(u);
    out += ',';
}

void append_path_slot(std::string &out, const std::string &rel,
                      VkFormat fmt) {
    out += normalize_texture_rel_path_key(rel);
    out += '#';
    out += std::to_string(static_cast<std::uint32_t>(fmt));
    out += '#';
    out += std::to_string(
        sampler_config_fingerprint(lumen::render::SamplerConfig {}));
    out += '|';
}

} // namespace

std::string material_load_desc_cache_key(const lumen::render::MaterialLoadDesc &d) {
    std::string k;
    k.reserve(512);
    append_path_slot(k, d.albedo_path, VK_FORMAT_R8G8B8A8_SRGB);
    append_path_slot(k, d.normal_path, VK_FORMAT_R8G8B8A8_UNORM);
    append_path_slot(k, d.metallic_roughness_path, VK_FORMAT_R8G8B8A8_UNORM);
    append_path_slot(k, d.ao_path, VK_FORMAT_R8G8B8A8_UNORM);
    append_path_slot(k, d.emissive_path, VK_FORMAT_R8G8B8A8_SRGB);

    append_f32_bits(k, d.base_color_factor.x);
    append_f32_bits(k, d.base_color_factor.y);
    append_f32_bits(k, d.base_color_factor.z);
    append_f32_bits(k, d.base_color_factor.w);
    append_f32_bits(k, d.metallic_factor);
    append_f32_bits(k, d.roughness_factor);
    append_f32_bits(k, d.ao_factor);
    append_f32_bits(k, d.emissive_factor.x);
    append_f32_bits(k, d.emissive_factor.y);
    append_f32_bits(k, d.emissive_factor.z);
    append_f32_bits(k, d.alpha_cutoff);

    k += 'a';
    k += std::to_string(static_cast<int>(d.alpha_mode));
    k += 'd';
    k += d.double_sided ? '1' : '0';
    k += 's';
    k += d.spec_gloss_texture_in_mr_slot ? '1' : '0';
    return k;
}

} // namespace lumen::asset
