/**
 * @file asset/texture_registry.cpp
 * @brief `TextureRegistry` 实现
 */

#include "asset/texture_registry.hpp"

#include "core/logger.hpp"
#include "core/path.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/texture.hpp"

#include <ghc/filesystem.hpp>

namespace lumen::asset {
namespace {

namespace fs = ghc::filesystem;

[[nodiscard]] bool ends_with_ktx(std::string_view s) {
    return s.ends_with(".ktx") || s.ends_with(".KTX") ||
           s.ends_with(".ktx2") || s.ends_with(".KTX2");
}

} // namespace

std::string normalize_texture_rel_path_key(std::string_view rel_path) {
    fs::path p { std::string { rel_path } };
    std::string g = p.lexically_normal().generic_string();
    while (!g.empty() && (g.front() == '/' || g.front() == '\\')) {
        g.erase(g.begin());
    }
    return g;
}

std::string make_texture_cache_key(std::string_view rel_path_key, VkFormat format,
                                   std::uint64_t sampler_fp) {
    return std::string { rel_path_key } + '#' +
           std::to_string(static_cast<std::uint32_t>(format)) + '#' +
           std::to_string(sampler_fp);
}

std::shared_ptr<lumen::render::Texture>
TextureRegistry::get_or_create_file(lumen::render::Context &ctx,
                                    VkQueue transfer_queue,
                                    lumen::render::CommandPool &cmd_pool,
                                    const std::string_view resource_rel_path,
                                    const VkFormat format,
                                    const lumen::render::SamplerConfig &sampler) {
    if (resource_rel_path.empty()) {
        return nullptr;
    }
    const std::string rel_key = normalize_texture_rel_path_key(resource_rel_path);
    const std::uint64_t sfp = sampler_config_fingerprint(sampler);
    const std::string key = make_texture_cache_key(rel_key, format, sfp);

    std::lock_guard lock { mutex_ };
    if (const auto it = map_.find(key); it != map_.end()) {
        if (auto sp = it->second.lock()) {
            return sp;
        }
        map_.erase(it);
    }

    const std::string full = lumen::core::get_resource_path(std::string { rel_key });
    if (!fs::exists(fs::path(full))) {
        LUMEN_LOG_WARN("TextureRegistry: 文件不存在，跳过: {}", full);
        return nullptr;
    }

    auto tex = std::make_shared<lumen::render::Texture>();
    const bool ktx = ends_with_ktx(full);
    bool ok = false;
    if (ktx) {
        ok = tex->create_from_ktx_file(ctx, full.c_str(), transfer_queue, cmd_pool,
                                       format, sampler);
    } else {
        ok = tex->create_from_file(ctx, full.c_str(), transfer_queue, cmd_pool, sampler,
                                   format);
    }
    if (!ok) {
        LUMEN_LOG_WARN("TextureRegistry: 上传失败: {}", full);
        return nullptr;
    }

    map_[key] = std::weak_ptr<lumen::render::Texture>(tex);
    return tex;
}

void TextureRegistry::clear() {
    std::lock_guard lock { mutex_ };
    map_.clear();
}

} // namespace lumen::asset
