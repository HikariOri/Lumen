/**
 * @file directional_light.cpp
 */

#include "scene/directional_light.hpp"

#include "scene/components.hpp"
#include "scene/transform.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <entt/entt.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>

namespace lumen::scene {

void pack_directional_lights_for_ubo(const ::entt::registry &registry,
                                     glm::vec4 (&out_lights)[kMaxDirectionalLightsUbo]) {
    for (std::size_t i = 0; i < kMaxDirectionalLightsUbo; ++i) {
        out_lights[i] = glm::vec4(0.0f);
    }

    struct Item {
        std::uint32_t sort_key;
        ::entt::entity entity;
    };
    std::vector<Item> items;
    items.reserve(8);

    for (const ::entt::entity e : registry.view<DirectionalLightComponent>()) {
        std::uint32_t sort_key = std::numeric_limits<std::uint32_t>::max();
        if (const auto *oid = registry.try_get<ObjectId>(e)) {
            sort_key = oid->id;
        }
        items.push_back(Item { sort_key, e });
    }

    std::sort(items.begin(), items.end(),
              [](const Item &a, const Item &b) { return a.sort_key < b.sort_key; });

    std::size_t n = 0;
    for (const Item &it : items) {
        if (n >= kMaxDirectionalLightsUbo) {
            break;
        }
        const auto &light = registry.get<DirectionalLightComponent>(it.entity);
        const glm::mat4 world = world_matrix(registry, it.entity);
        const glm::mat3 linear = glm::mat3(world);
        const glm::vec3 world_dir = linear * light.direction;
        out_lights[n] = glm::vec4(world_dir, light.intensity);
        ++n;
    }
}

} // namespace lumen::scene
