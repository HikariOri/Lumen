#pragma once

#include <flecs.h>

class Scene {
    Scene() { auto t = ecs.entity("bob"); }

private:
    flecs::world ecs;
};
