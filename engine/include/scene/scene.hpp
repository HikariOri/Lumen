/**
 * @file scene.hpp
 * @brief EnTT 场景封装：创建/销毁实体、父子关系（无环）、ObjectId 与 Pick 反查
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
     * @brief 创建实体并附加 ObjectId、Transform、Name（默认单位矩阵）
     */
    [[nodiscard]] ::entt::entity
    create_entity(std::string_view name = "Entity");

    /**
     * @brief 设置父节点；parent 为 null 则解除父子关系
     * @return 若会形成环或实体无效则 false
     */
    bool set_parent(::entt::entity child, ::entt::entity parent);

    /// 销毁实体；子节点会变为根（移除 Parent）；同步移除 ObjectId 反查表项
    void destroy_entity(::entt::entity entity);

    /// 子节点列表（线性扫描，适合编辑器规模）
    [[nodiscard]] std::vector<::entt::entity>
    children_of(::entt::entity parent) const;

    /// 首个带 DrawableTag 的实体，若无则 null
    [[nodiscard]] ::entt::entity primary_drawable() const;

    /**
     * @brief 读取 `ObjectId::id`，供渲染/Pick Pass 写入
     * GPU（无效实体或无组件返回 kInvalid）
     */
    [[nodiscard]] std::uint32_t object_id_for(::entt::entity entity) const;

    /**
     * @brief 将 Pick 缓冲读回的 ID 解析为实体（0 或未知 ID 返回 nullopt）
     */
    [[nodiscard]] std::optional<::entt::entity>
    entity_from_object_id(std::uint32_t object_id) const;

private:
    [[nodiscard]] std::uint32_t allocate_object_id_();

    ::entt::registry reg_;
    /// 下一个待分配的 ObjectId（0 保留，从 1 起）
    std::uint32_t next_object_id_ { 1 };
    /// ObjectId::id → entity（仅含当前仍存在的分配；与 destroy 同步擦除）
    std::unordered_map<std::uint32_t, ::entt::entity> object_id_to_entity_;
};

} // namespace scene
} // namespace lumen
