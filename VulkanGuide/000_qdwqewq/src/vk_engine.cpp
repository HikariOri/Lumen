//> includes
#include "vk_engine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui_impl_sdl3.h>
#include <vk_initializers.h>
#include <vk_types.h>

#include <chrono>
#include <thread>

VulkanEngine *loadedEngine = nullptr;

VulkanEngine &VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::init() {
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    auto window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    // _window = SDL_CreateWindow("Vulkan Engine", SDL_WINDOWPOS_UNDEFINED,
    //                            SDL_WINDOWPOS_UNDEFINED, _windowExtent.width,
    //                            _windowExtent.height, window_flags);
    _window = SDL_CreateWindow("Vulkan Engine", _windowExtent.width,
                               _windowExtent.height, window_flags);

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (_isInitialized) {

        SDL_DestroyWindow(_window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw() {
    // nothing yet
}

void VulkanEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_EVENT_QUIT)
                bQuit = true;

            if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
                stop_rendering = true;
            }
            if (e.type == SDL_EVENT_WINDOW_RESTORED) {
                stop_rendering = false;
            }
        }

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}
