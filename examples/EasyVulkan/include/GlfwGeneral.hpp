#pragma once

#include "VKBase.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

GLFWwindow *pWindow {};
GLFWmonitor *pMonitor {};
const char *windowTitle = "EasyVK";

/**
 * @brief 初始化窗口
 *
 * @param size 窗口大小
 * @param fullScreen 指定是否以全屏初始化窗口
 * @param isResizable 指定窗口是否可拉伸，游戏窗口通常是不可任意拉伸的
 * @param limitFrameRate 指定是否将帧数限制到不超过屏幕刷新率
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool InitializeWindow(VkExtent2D size, bool fullScreen = false,
                      bool isResizable = true, bool limitFrameRate = true) {
    // using命名空间
    using namespace vulkan;

    if (!glfwInit()) {
        std::println(
            "[ InitializeWindow ] ERROR\nFailed to initialize GLFW!\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, isResizable);

    pMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *pMode = glfwGetVideoMode(pMonitor);
    pWindow = fullScreen ? glfwCreateWindow(pMode->width, pMode->height,
                                            windowTitle, pMonitor, nullptr)
                         : glfwCreateWindow(size.width, size.height,
                                            windowTitle, nullptr, nullptr);
    if (!pWindow) {
        std::println("[ InitializeWindow ]\nFailed to create a glfw window!\n");
        glfwTerminate();
        return false;
    }

    uint32_t extensionCount = 0;
    const char **extensionNames;
    extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensionNames) {
        std::cout << std::format(
            "[ InitializeWindow ]\nVulkan is not available on this machine!\n");
        glfwTerminate();
        return false;
    }

    for (size_t i = 0; i < extensionCount; i++) {
        graphicsBase::Base().AddInstanceExtension(extensionNames[i]);
    }

    graphicsBase::Base().AddDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // 在创建 window surface 前创建 Vulkan 实例
    graphicsBase::Base().UseLatestApiVersion();
    if (graphicsBase::Base().CreateInstance()) {
        return false;
    }

    // 创建window surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (result_t result = glfwCreateWindowSurface(
            graphicsBase::Base().Instance(), pWindow, nullptr, &surface)) {
        std::println("[ InitializeWindow ] ERROR\nFailed to create a window "
                     "surface!\nError code: {}\n",
                     string_VkResult(result));
        glfwTerminate();
        return false;
    }
    graphicsBase::Base().Surface(surface);

    // 通过用 || 操作符短路执行来省去几行
    if ( // 获取物理设备，并使用列表中的第一个物理设备，这里不考虑以下任意函数失败后更换物理设备的情况
        graphicsBase::Base().GetPhysicalDevices() ||
        // 一个 true一个 false，暂时不需要计算用的队列
        graphicsBase::Base().DeterminePhysicalDevice(0, true, false) ||
        // 创建逻辑设备
        graphicsBase::Base().CreateDevice()) {
        return false;
    }

    if (graphicsBase::Base().CreateSwapchain(limitFrameRate)) {
        return false;
    }

    return true;
}

/**
 * @brief 终止窗口时清理 GLFW
 *
 */
void TerminateWindow() {
    vulkan::graphicsBase::Base().WaitIdle();
    glfwTerminate();
}

/**
 * @brief 在标题上显示帧率，每帧调用一次。约每一秒更新一次帧率
 *
 * @param updatePerSecond 是一秒更新一次，还是每帧都更新
 */
void TitleFps(bool updatePerSecond = false) {
    static double time0 = glfwGetTime();
    static double time1;
    static double dt;
    static int dframe = -1;
    static std::stringstream info;

    time1 = glfwGetTime();

    dframe++;

    if ((dt = time1 - time0) >= 1) {
        info.precision(1);
        info << windowTitle << "    " << std::fixed << dframe / dt << " FPS";
        glfwSetWindowTitle(pWindow, info.str().c_str());
        info.str(""); // 别忘了在设置完窗口标题后清空所用的stringstream
        time0 = time1;
        dframe = 0;
    }
}

void MakeWindowFullScreen() {
    const GLFWvidmode *pMode = glfwGetVideoMode(pMonitor);
    glfwSetWindowMonitor(pWindow, pMonitor, 0, 0, pMode->width, pMode->height,
                         pMode->refreshRate);
}

void MakeWindowWindowed(VkOffset2D position, VkExtent2D size) {
    const GLFWvidmode *pMode = glfwGetVideoMode(pMonitor);
    glfwSetWindowMonitor(pWindow, nullptr, position.x, position.y, size.width,
                         size.height, pMode->refreshRate);
}
