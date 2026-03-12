#pragma once

#include <flecs.h>

namespace lumen {
    class Scene {
        Scene() { auto t = ecs.entity("bob"); }

    private:
        flecs::world ecs;
    };

} // namespace lumen
