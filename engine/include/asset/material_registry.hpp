/**
 * @file asset/material_registry.hpp
 * @brief `MaterialLoadDesc` 指纹 → 共享 `PbrMaterialInstance`（内部走 `TextureRegistry`）
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "asset/pbr_material_instance.hpp"

namespace lumen::render {
class CommandPool;
class Context;
struct MaterialLoadDesc;
} // namespace lumen::render

namespace lumen::asset {

class TextureRegistry;

class MaterialRegistry {
public:
    MaterialRegistry() = default;
    MaterialRegistry(const MaterialRegistry &) = delete;
    MaterialRegistry &operator=(const MaterialRegistry &) = delete;

    [[nodiscard]] std::shared_ptr<PbrMaterialInstance> get_or_create(
        TextureRegistry &textures, lumen::render::Context &ctx,
        VkQueue transfer_queue, lumen::render::CommandPool &cmd_pool,
        const lumen::render::MaterialLoadDesc &desc);

    void clear();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::weak_ptr<PbrMaterialInstance>> map_;
};

} // namespace lumen::asset
