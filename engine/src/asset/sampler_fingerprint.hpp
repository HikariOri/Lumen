/**
 * @file asset/sampler_fingerprint.hpp
 * @brief `SamplerConfig` 稳定指纹，供纹理缓存键区分采样状态
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "render/resource/sampler.hpp"

namespace lumen::asset {

[[nodiscard]] inline std::uint64_t
sampler_config_fingerprint(const lumen::render::SamplerConfig &c) {
    std::uint64_t h { 1469598103934665603ULL };
    auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(c.magFilter));
    mix(static_cast<std::uint64_t>(c.minFilter));
    mix(static_cast<std::uint64_t>(c.mipmapMode));
    mix(static_cast<std::uint64_t>(c.addressModeU));
    mix(static_cast<std::uint64_t>(c.addressModeV));
    mix(static_cast<std::uint64_t>(c.addressModeW));
    mix(static_cast<std::uint64_t>(c.borderColor));
    std::uint32_t aniso_bits {};
    std::memcpy(&aniso_bits, &c.maxAnisotropy, sizeof(float));
    mix(aniso_bits);
    std::uint32_t min_lod_bits {};
    std::uint32_t max_lod_bits {};
    std::memcpy(&min_lod_bits, &c.minLod, sizeof(float));
    std::memcpy(&max_lod_bits, &c.maxLod, sizeof(float));
    mix(min_lod_bits);
    mix(max_lod_bits);
    return h;
}

} // namespace lumen::asset
