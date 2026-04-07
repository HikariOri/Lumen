#include "rhi/shader_module_cache.hpp"

#include "rhi/spirv_hash.hpp"

#include "core/log/logger.hpp"

namespace rhi {

vk::ShaderModule ShaderModuleCache::get_or_create(
    const vk::Device device, const std::span<const std::byte> spirv) {
    if (!device || spirv.empty() || (spirv.size() % 4) != 0) {
        return nullptr;
    }
    const std::uint64_t k = hash_spirv_bytes(spirv);
    const auto it = modules_.find(k);
    if (it != modules_.end()) {
        return it->second;
    }
    vk::ShaderModuleCreateInfo ci {};
    ci.codeSize = spirv.size();
    ci.pCode = reinterpret_cast<const std::uint32_t *>(spirv.data());
    vk::ShaderModule mod {};
    const vk::Result r = device.createShaderModule(&ci, nullptr, &mod);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("ShaderModuleCache::get_or_create 失败 {}",
                        static_cast<int>(r));
        return nullptr;
    }
    modules_.emplace(k, mod);
    return mod;
}

void ShaderModuleCache::clear(const vk::Device device) {
    if (!device) {
        modules_.clear();
        return;
    }
    for (const auto &e : modules_) {
        if (e.second) {
            device.destroyShaderModule(e.second, nullptr);
        }
    }
    modules_.clear();
}

} // namespace rhi
