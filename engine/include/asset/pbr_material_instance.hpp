/**
 * @file asset/pbr_material_instance.hpp
 * @brief PBR 材质 CPU 描述 + 贴图 `shared_ptr` 延长寿命（供 `MaterialRegistry`）
 */

#pragma once

#include <memory>
#include <vector>

#include "render/material/material.hpp"
#include "render/resource/texture.hpp"

namespace lumen::asset {

/**
 * @brief `Material` 裸指针指向 `textureKeepalive` 内对象；实例由注册表去重缓存
 */
struct PbrMaterialInstance {
    lumen::render::Material material {};
    std::vector<std::shared_ptr<lumen::render::Texture>> textureKeepalive {};
};

} // namespace lumen::asset
