/**
 * @file scene.hpp
 * @brief EnTT 场景封装：创建/销毁实体、父子关系（无环）、`IDComponent`（`core::ID`）
 */

#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "scene/components.hpp"

namespace lumen {
namespace scene {

class Scene {
public:
    [[nodiscard]] ::entt::registry &registry() { return reg_; }
    [[nodiscard]] const ::entt::registry &registry() const { return reg_; }

    /**
     * @brief 创建实体并附加 IDComponent、Transform、TagComponent（默认单位矩阵）
     */
    [[nodiscard]] ::entt::entity
    create_entity(std::string_view name = "Entity");

    /**
     * @brief 设置父节点；parent 为 null 则解除父子关系
     * @return 若会形成环或实体无效则 false
     */
    bool set_parent(::entt::entity child, ::entt::entity parent);

    /// 销毁实体；子节点会变为根（移除 `RelationshipComponent`）
    void destroy_entity(::entt::entity entity);

    /// 子节点列表（线性扫描，适合编辑器规模）
    [[nodiscard]] std::vector<::entt::entity>
    children_of(::entt::entity parent) const;

private:
    ::entt::registry reg_;
};

} // namespace scene
} // namespace lumen
