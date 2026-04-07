#pragma once

#include "rhi/vulkan.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace rhi {

/// SPIR-V 内容哈希 → `vk::ShaderModule`，避免重复 `createShaderModule`。
class ShaderModuleCache {
public:
    ShaderModuleCache() = default;
    ShaderModuleCache(const ShaderModuleCache &) = delete;
    ShaderModuleCache &operator=(const ShaderModuleCache &) = delete;
    ShaderModuleCache(ShaderModuleCache &&) = delete;
    ShaderModuleCache &operator=(ShaderModuleCache &&) = delete;
    ~ShaderModuleCache() = default;

    [[nodiscard]] vk::ShaderModule
    get_or_create(vk::Device device, std::span<const std::byte> spirv);

    void clear(vk::Device device);

private:
    std::unordered_map<std::uint64_t, vk::ShaderModule> modules_;
};

} // namespace rhi
