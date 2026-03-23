/**
 * @file pbr_placeholder_textures.hpp
 * @brief PBR 缺贴图时的 1×1 占位纹理（反照率 sRGB、法线、MR、AO、自发光）
 */

#pragma once

#include <vulkan/vulkan.h>

#include "render/resource/texture.hpp"

namespace lumen {
namespace render {

class CommandPool;
class Context;

/**
 * @brief 五张占位 2D 纹理；create 失败时 is_complete() 为 false
 */
class PbrPlaceholderTextures {
public:
    bool create(const Context &ctx, VkQueue transfer_queue,
                CommandPool &cmd_pool);

    [[nodiscard]] bool is_complete() const {
        return albedo_.is_valid() && normal_.is_valid() &&
               metallic_roughness_.is_valid() && ao_.is_valid() &&
               emissive_.is_valid();
    }

    [[nodiscard]] const Texture &albedo() const { return albedo_; }
    [[nodiscard]] const Texture &normal() const { return normal_; }
    [[nodiscard]] const Texture &metallic_roughness() const {
        return metallic_roughness_;
    }
    [[nodiscard]] const Texture &ao() const { return ao_; }
    [[nodiscard]] const Texture &emissive() const { return emissive_; }

private:
    Texture albedo_;
    Texture normal_;
    Texture metallic_roughness_;
    Texture ao_;
    Texture emissive_;
};

} // namespace render
} // namespace lumen
