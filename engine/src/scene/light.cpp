/**
 * @file light.cpp
 */

#include "scene/light.hpp"

#include "scene/components.hpp"
#include "scene/transform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

} // namespace

void pack_lights_for_ubo(const ::entt::registry &registry, GPULight *out_lights,
                         std::uint32_t &out_count) {
    out_count = 0;
    for (std::size_t i = 0; i < kMaxLightsUbo; ++i) {
        out_lights[i] = GPULight {};
    }

    struct Item {
        std::uint32_t sort_key;
        ::entt::entity entity;
    };
    std::vector<Item> items;
    items.reserve(16);

    for (const ::entt::entity e : registry.view<LightComponent>()) {
        std::uint32_t sort_key = std::numeric_limits<std::uint32_t>::max();
        if (const auto *oid = registry.try_get<ObjectId>(e)) {
            sort_key = oid->id;
        }
        items.push_back(Item { sort_key, e });
    }

    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.sort_key < b.sort_key;
    });

    for (const Item &it : items) {
        if (out_count >= kMaxLightsUbo) {
            break;
        }
        const auto &light = registry.get<LightComponent>(it.entity);
        const glm::mat4 world = world_matrix(registry, it.entity);
        const glm::mat3 linear = glm::mat3(world);

        GPULight g {};
        g.color = glm::vec4(light.color, light.intensity);

        switch (light.type) {
        case LightType::Directional: {
            glm::vec3 wdir = linear * light.local_direction;
            const float len = glm::length(wdir);
            if (len > 1e-8f) {
                wdir /= len;
            } else {
                wdir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            g.position =
                glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(light.type));
            g.direction = glm::vec4(wdir, 0.0f);
            g.params = glm::vec4(0.0f);
            break;
        }
        case LightType::Point: {
            g.position = glm::vec4(world_translation(world),
                                   static_cast<float>(light.type));
            g.direction = glm::vec4(0.0f);
            g.params =
                glm::vec4(std::max(light.range, 1e-4f), 0.0f, 0.0f, 0.0f);
            break;
        }
        case LightType::Spot: {
            glm::vec3 waxis = linear * light.local_direction;
            const float ax_len = glm::length(waxis);
            if (ax_len > 1e-8f) {
                waxis /= ax_len;
            } else {
                waxis = glm::vec3(0.0f, 0.0f, -1.0f);
            }
            const float a0 = light.inner_radians;
            const float a1 = light.outer_radians;
            const float inner_a = std::min(a0, a1);
            const float outer_a = std::max(a0, a1);
            g.position = glm::vec4(world_translation(world),
                                   static_cast<float>(light.type));
            g.direction = glm::vec4(waxis, 0.0f);
            g.params = glm::vec4(std::max(light.range, 1e-4f),
                                 std::cos(outer_a), std::cos(inner_a), 0.0f);
            break;
        }
        }

        out_lights[out_count] = g;
        ++out_count;
    }
}

} // namespace lumen::scene
