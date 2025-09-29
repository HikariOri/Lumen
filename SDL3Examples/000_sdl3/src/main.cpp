#include <iostream>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"

#include "ImGuizmo.h"

#include "imgui-node-editor/imgui_node_editor.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <SDL3_image/SDL_image.h>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

int main() {
    // 1) SDL init + window（确保使用 SDL_WINDOW_VULKAN 创建窗口）
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return -1;
    }
    SDL_Window *window =
        SDL_CreateWindow("SDL3 Vulkan RAII", 800, 600, SDL_WINDOW_VULKAN);

    // 加载 bmp 作为图标
    SDL_Surface *icon = IMG_Load("./assets/icons/哈士奇.png");
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        // SDL_DestroySurface(icon);
    }

    // 2) 获取 SDL 要求的 Instance 扩展名
    Uint32 extCount = 0;
    const char *const *sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    std::vector<const char *> extensions;
    extensions.reserve(extCount + 1);
    for (uint32_t i = 0; i < extCount; ++i)
        extensions.push_back(sdlExts[i]);
    // 可选：添加调试扩展（VK_EXT_debug_utils）和 validation layer 等

    // 3) Vulkan RAII context + instance（vulkan_raii.hpp）
    vk::ApplicationInfo appInfo { "MyApp", VK_MAKE_VERSION(1, 0, 0), "NoEngine",
                                  VK_MAKE_VERSION(1, 0, 0),
                                  VK_API_VERSION_1_3 };

    vk::InstanceCreateInfo instanceCI { {},
                                        &appInfo,
                                        0,
                                        nullptr,
                                        static_cast<uint32_t>(
                                            extensions.size()),
                                        extensions.data() };

    // RAII: Context, then Instance
    vk::raii::Context context;                           // 初始化 dispatcher
    vk::raii::Instance instance { context, instanceCI }; // 自动在析构时销毁

    // 4) 使用 SDL 创建 VkSurfaceKHR，然后用 RAII 包装（你也可以直接用
    // VkSurfaceKHR）
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(*instance),
                                  nullptr, &rawSurface)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError()
                  << "\n";
        return -1;
    }
    // 把 rawSurface 放进 RAII wrapper（实例 owned by instance）
    vk::raii::SurfaceKHR surface { instance, rawSurface };

    // 5) 之后按 Vulkan 流程：选物理设备 -> 创建 Logical device & queues ->
    // swapchain ...
    //    （用 vk::raii::PhysicalDevices / Device / SwapchainKHR 等）

    // 事件循环（简略）
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                running = false;
        }
        // 渲染帧...
    }

    // RAII 会在析构时顺序清理 instance、surface、device 等
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
