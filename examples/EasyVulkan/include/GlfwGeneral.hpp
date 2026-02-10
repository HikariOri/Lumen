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
    if (!glfwInit()) {
        std::println("[ InitializeWindow ] ERROR\nFailed to initialize GLFW!");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, isResizable);

    uint32_t extensionCount {};
    const char **extensionNames;
    extensionNames = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensionNames) {
        std::println(
            "[ InitializeWindow ]\nVulkan is not available on this machine!");
        glfwTerminate();
        return false;
    }

    for (size_t i = 0; i < extensionCount; i++) {
        vulkan::graphicsBase::Base().AddInstanceExtension(extensionNames[i]);
    }
    vulkan::graphicsBase::Base().AddDeviceExtension(
        VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    pMonitor = glfwGetPrimaryMonitor();

    const GLFWvidmode *pMode = glfwGetVideoMode(pMonitor);
    pWindow = fullScreen ? glfwCreateWindow(pMode->width, pMode->height,
                                            windowTitle, pMonitor, nullptr)
                         : glfwCreateWindow(size.width, size.height,
                                            windowTitle, nullptr, nullptr);

    if (!pWindow) {
        std::println("[ InitializeWindow ]\nFailed to create a glfw window!");
        glfwTerminate();
        return false;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (VkResult result =
            glfwCreateWindowSurface(vulkan::graphicsBase::Base().Instance(),
                                    pWindow, nullptr, &surface)) {
        std::println("[ InitializeWindow ] ERROR\nFailed to create a window "
                     "surface!\nError code: {}",
                     string_VkResult(result));
        glfwTerminate();
        return false;
    }
    vulkan::graphicsBase::Base().Surface(surface);

    return true;
}

/**
 * @brief 终止窗口时清理 GLFW
 *
 */
void TerminateWindow() { glfwTerminate(); }

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
