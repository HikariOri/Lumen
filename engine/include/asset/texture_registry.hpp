/**
 * @file asset/texture_registry.hpp
 * @brief 路径 + 格式 + `SamplerConfig` 指纹 → 共享 `Texture`（Hazel 式集中缓存）
 *
 * @note `Material` 内贴图槽为裸指针时，约定其来自本注册表返回的 `shared_ptr::get()`（或
 * 场景资产内为同一实例延长生命期的 `shared_ptr`），在对应资产 / 缓存项卸载前指针稳定。
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "render/vulkan.hpp"

#include "asset/sampler_fingerprint.hpp"
#include "render/resource/sampler.hpp"

namespace lumen::render {
class CommandPool; // `render/command_buffer.hpp`
class Context;
class Texture;
} // namespace lumen::render

namespace lumen::asset {

/**
 * @brief 规范化资源相对路径作缓存键片段（统一分隔符，去掉前导 `./`）
 */
[[nodiscard]] std::string normalize_texture_rel_path_key(std::string_view rel_path);

/**
 * @brief 合成纹理缓存键：`rel_key#formatU32#sampler_fp`
 */
[[nodiscard]] std::string make_texture_cache_key(std::string_view rel_path_key,
                                                 VkFormat format,
                                                 std::uint64_t sampler_fp);

class TextureRegistry {
public:
    TextureRegistry() = default;
    TextureRegistry(const TextureRegistry &) = delete;
    TextureRegistry &operator=(const TextureRegistry &) = delete;

    /**
     * @brief 按资源相对路径加载或命中缓存；磁盘路径由 `get_resource_path(rel_path)` 解析
     * @return 失败返回空 `shared_ptr`
     */
    /// 亦即计划中的 `get_or_create`：磁盘文件 → 缓存命中或上传
    [[nodiscard]] std::shared_ptr<lumen::render::Texture>
    get_or_create_file(lumen::render::Context &ctx, VkQueue transfer_queue,
                       lumen::render::CommandPool &cmd_pool,
                       std::string_view resource_rel_path, VkFormat format,
                       const lumen::render::SamplerConfig &sampler = {});

    void clear();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<lumen::render::Texture>> map_;
};

} // namespace lumen::asset
