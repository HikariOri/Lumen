/**
 * @file main.cpp
 * @brief Sandbox：测试引擎功能
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "platform/window.hpp"
#include "render/context.hpp"
#include "render/swapchain.hpp"

int main() {
    lumen::core::LoggerConfig logConfig;
    logConfig.engine.level = spdlog::level::debug;
    logConfig.app.level = spdlog::level::info;
    if (!lumen::core::Logger::init(logConfig)) {
        return -1;
    }

    LUMEN_LOG_INFO("Sandbox 启动");
    LUMEN_APP_LOG_INFO("应用层日志测试");

    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "LearnVulkan Sandbox";
    winConfig.width = 800;
    winConfig.height = 600;

    if (!window.create(winConfig)) {
        LUMEN_LOG_ERROR("窗口创建失败");
        return -1;
    }
    LUMEN_LOG_INFO("窗口创建成功: {}x{}", window.width(), window.height());

    auto extensions = window.get_vulkan_instance_extensions();
    lumen::render::ContextConfig ctxConfig;
    ctxConfig.instanceExtensions.assign(extensions.begin(), extensions.end());

    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig)) {
        LUMEN_LOG_ERROR("Vulkan Instance 创建失败");
        return -1;
    }
    LUMEN_LOG_INFO("Vulkan Instance 创建成功");

    VkSurfaceKHR surface = window.create_vulkan_surface(ctx.instance());
    if (surface == VK_NULL_HANDLE) {
        LUMEN_LOG_ERROR("Vulkan Surface 创建失败");
        return -1;
    }
    LUMEN_LOG_INFO("Vulkan Surface 创建成功");

    if (!ctx.init_device(surface)) {
        LUMEN_LOG_ERROR("Vulkan Device 创建失败");
        return -1;
    }
    LUMEN_LOG_INFO("Vulkan Device 创建成功");

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);
    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface, static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        LUMEN_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }
    LUMEN_LOG_INFO("Swapchain 创建成功, {} 张图像", swapchain.image_count());

    LUMEN_APP_LOG_INFO("引擎初始化完成，进入主循环");

    lumen::platform::EventPump pump;
    lumen::platform::EventList events;
    lumen::platform::Input input;
    while (pump.poll(events, input)) {
        for (const auto& e : events) {
            if (std::holds_alternative<lumen::platform::EventWindowResize>(e)) {
                const auto& r = std::get<lumen::platform::EventWindowResize>(e);
                LUMEN_LOG_DEBUG("窗口大小: {}x{}", r.width, r.height);
            }
        }
        // TODO: 渲染
    }

    vkDestroySurfaceKHR(ctx.instance(), surface, nullptr);
    LUMEN_LOG_INFO("Sandbox 退出");
    lumen::core::Logger::shutdown();
    return 0;
}
