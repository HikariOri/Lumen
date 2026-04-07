#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rhi {

/// FNV-1a 64-bit，用于 SPIR-V 字节去重与 `GraphicsPipelineKey` 着色器身份。
[[nodiscard]] inline std::uint64_t hash_spirv_bytes(std::span<const std::byte> data) {
    constexpr std::uint64_t k_offset = 14695981039346656037ULL;
    constexpr std::uint64_t k_prime = 1099511628211ULL;
    std::uint64_t h = k_offset;
    const auto *const p = reinterpret_cast<const std::uint8_t *>(data.data());
    for (std::size_t i = 0; i < data.size(); ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= k_prime;
    }
    return h;
}

} // namespace rhi
