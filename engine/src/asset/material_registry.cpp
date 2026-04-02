/**
 * @file asset/material_registry.cpp
 */

#include "asset/material_registry.hpp"

#include "asset/material_fingerprint.hpp"
#include "asset/texture_registry.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/material/material.hpp"
#include "render/resource/texture.hpp"

namespace lumen::asset {
namespace {

[[nodiscard]] std::shared_ptr<lumen::render::Texture>
acquire_tex(lumen::asset::TextureRegistry &tex_reg, lumen::render::Context &ctx,
            VkQueue q, lumen::render::CommandPool &pool,
            const std::string &rel_path, VkFormat fmt) {
    if (rel_path.empty()) {
        return nullptr;
    }
    return tex_reg.get_or_create_file(ctx, q, pool, rel_path, fmt, {});
}

} // namespace

std::shared_ptr<PbrMaterialInstance> MaterialRegistry::get_or_create(
    TextureRegistry &tex_reg, lumen::render::Context &ctx,
    VkQueue transfer_queue, lumen::render::CommandPool &cmd_pool,
    const lumen::render::MaterialLoadDesc &desc) {
    const std::string key = material_load_desc_cache_key(desc);

    std::lock_guard lock { mutex_ };
    if (const auto it = map_.find(key); it != map_.end()) {
        if (auto sp = it->second.lock()) {
            return sp;
        }
        map_.erase(it);
    }

    auto inst = std::make_shared<PbrMaterialInstance>();
    lumen::render::Material &m = inst->material;
    m.baseColorFactor = desc.base_color_factor;
    m.metallicFactor = desc.metallic_factor;
    m.roughnessFactor = desc.roughness_factor;
    m.emissiveFactor = desc.emissive_factor;
    m.occlusionStrength = desc.ao_factor;
    m.doubleSided = desc.double_sided;
    m.alphaMode = desc.alpha_mode;

    auto push_tex = [&](std::shared_ptr<lumen::render::Texture> sp,
                        const lumen::render::Texture **slot) {
        if (!sp || !sp->is_valid()) {
            *slot = nullptr;
            return;
        }
        *slot = sp.get();
        inst->textureKeepalive.push_back(std::move(sp));
    };

    push_tex(acquire_tex(tex_reg, ctx, transfer_queue, cmd_pool, desc.albedo_path,
                         VK_FORMAT_R8G8B8A8_SRGB),
             &m.baseColorTex);
    push_tex(acquire_tex(tex_reg, ctx, transfer_queue, cmd_pool, desc.normal_path,
                         VK_FORMAT_R8G8B8A8_UNORM),
             &m.normalTex);
    push_tex(acquire_tex(tex_reg, ctx, transfer_queue, cmd_pool,
                         desc.metallic_roughness_path, VK_FORMAT_R8G8B8A8_UNORM),
             &m.metallicRoughnessTex);
    push_tex(acquire_tex(tex_reg, ctx, transfer_queue, cmd_pool, desc.ao_path,
                         VK_FORMAT_R8G8B8A8_UNORM),
             &m.occlusionTex);
    push_tex(acquire_tex(tex_reg, ctx, transfer_queue, cmd_pool, desc.emissive_path,
                         VK_FORMAT_R8G8B8A8_SRGB),
             &m.emissiveTex);

    map_[key] = std::weak_ptr<PbrMaterialInstance>(inst);
    return inst;
}

void MaterialRegistry::clear() {
    std::lock_guard lock { mutex_ };
    map_.clear();
}

} // namespace lumen::asset
