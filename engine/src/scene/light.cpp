/**
 * @file light.cpp
 */

#include "scene/light.hpp"

#include "scene/components.hpp"
#include "scene/transform.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <entt/entt.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

namespace lumen::scene {
namespace {

[[nodiscard]] glm::vec3 world_translation(const glm::mat4 &world) {
    return glm::vec3(world[3]);
}

[[nodiscard]] glm::vec3 normalize_safe(glm::vec3 v, glm::vec3 fallback) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : fallback;
}

/// 与旧版统一光源默认一致：局部空间「表面 → 光源」
constexpr glm::vec3 kDefaultSurfToLightLocal { 0.0f, 0.5f, -1.0f };

/// 聚光锥轴（光发射方向）：局部 -Z
constexpr glm::vec3 kDefaultSpotEmitLocal { 0.0f, 0.0f, -1.0f };

} // namespace

void pack_lights_for_ubo(const entt::registry &registry, GPULight *out_lights,
                         std::uint32_t &out_count) {
    for (std::size_t i = 0; i < kMaxLightsUbo; ++i) {
        out_lights[i] = GPULight {};
    }

    struct Item {
        std::uint32_t sort_key {};
        GPULight gpu {};
    };
    std::vector<Item> items;
    items.reserve(32);

    for (const entt::entity e : registry.view<DirectionalLightComponent>()) {
        const auto &dl = registry.get<DirectionalLightComponent>(e);
        const glm::mat4 world = world_matrix(registry, e);
        const glm::mat3 linear = glm::mat3(world);
        glm::vec3 surf_to_light = linear * kDefaultSurfToLightLocal;
        surf_to_light =
            normalize_safe(surf_to_light, glm::vec3(0.0f, 1.0f, 0.0f));

        GPULight g {};
        g.position = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        g.direction = glm::vec4(surf_to_light, 0.0f);
        g.color = glm::vec4(dl.radiance, dl.intensity);
        g.params = glm::vec4(0.0f);
        items.push_back(Item {
            .sort_key = static_cast<std::uint32_t>(entt::to_integral(e)),
            .gpu = g });
    }

    for (const entt::entity e : registry.view<PointLightComponent>()) {
        const auto &pl = registry.get<PointLightComponent>(e);
        const glm::mat4 world = world_matrix(registry, e);
        const float range = std::max({ pl.radius, pl.minRadius, 1e-4F });

        GPULight g {};
        g.position = glm::vec4(world_translation(world), 1.0f);
        g.direction = glm::vec4(0.0f);
        g.color = glm::vec4(pl.radiance, pl.intensity);
        g.params = glm::vec4(range, 0.0f, 0.0f, 0.0f);
        items.push_back(Item {
            .sort_key = static_cast<std::uint32_t>(entt::to_integral(e)),
            .gpu = g });
    }

    for (const entt::entity e : registry.view<SpotLightComponent>()) {
        const auto &sl = registry.get<SpotLightComponent>(e);
        const glm::mat4 world = world_matrix(registry, e);
        const glm::mat3 linear = glm::mat3(world);
        glm::vec3 emit_axis = linear * kDefaultSpotEmitLocal;
        emit_axis = normalize_safe(emit_axis, glm::vec3(0.0f, 0.0f, -1.0f));

        const float outer_half = glm::radians(sl.angle * 0.5f);
        const float inner_half =
            outer_half *
            (1.0f / (1.0f + 0.15f * std::max(sl.angleAttenuation, 0.01f)));
        const float outer_clamped = std::min(outer_half, 1.553343f);
        const float inner_clamped = std::min(inner_half, outer_clamped);

        GPULight g {};
        g.position = glm::vec4(world_translation(world), 2.0f);
        g.direction = glm::vec4(emit_axis, 0.0f);
        g.color = glm::vec4(sl.radiance, sl.intensity);
        g.params = glm::vec4(std::max(sl.range, 1e-4f), std::cos(outer_clamped),
                             std::cos(inner_clamped), 0.0f);
        items.push_back(Item {
            .sort_key = static_cast<std::uint32_t>(entt::to_integral(e)),
            .gpu = g });
    }

    std::ranges::sort(items, [](const Item &a, const Item &b) {
        return a.sort_key < b.sort_key;
    });

    out_count = 0;
    for (const Item &it : items) {
        if (out_count >= kMaxLightsUbo) {
            break;
        }
        out_lights[out_count] = it.gpu;
        ++out_count;
    }
}

} // namespace lumen::scene
