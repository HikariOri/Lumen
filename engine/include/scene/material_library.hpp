/**
 * @file material_library.hpp
 * @brief PBR 材质资产表：JSON 加载/保存与按 id 查询（`render-engine-features.md` 7a）
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "scene/components.hpp"

namespace lumen {
namespace scene {

/**
 * @brief 命名材质资产库；实例可通过 `source_asset_id` 引用后再做运行时覆盖
 */
class MaterialAssetLibrary {
public:
    [[nodiscard]] bool load_from_json_file(std::string_view path);
    [[nodiscard]] bool save_to_json_file(std::string_view path) const;

    /// 若存在则返回资产的一份拷贝，供 `MaterialInstance::resolved` 初始化
    [[nodiscard]] std::optional<MaterialComponent>
    try_get(std::string_view id) const;

    void clear() { assets_.clear(); }
    [[nodiscard]] std::size_t size() const { return assets_.size(); }

private:
    std::unordered_map<std::string, MaterialComponent> assets_;
};

} // namespace scene
} // namespace lumen
