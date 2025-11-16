#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <tabulate/table.hpp>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    void initWindow() {
        // 初始化 glfw
        glfwInit();

        // 设置不适用 opengl api
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // 先不管 resize
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() { createInstance(); }

    void createInstance() {
        // VkXxXxx 类型对应的 sType 都是 VK_STRUCTURE_TYPE_XX_XXX

        // 创建 app info
        VkApplicationInfo appInfo;
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        // 创建 VkInstanceCreateInfo
        VkInstanceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        // 查询 glfw 需要的 vulkan 拓展
        // 如果是 sdl 就查询 glfw 的
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions {};

        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        tabulate::Table glfwRequiredInstanceExtensionsTable;
        std::cout << "glfw 需要的扩展:\n";
        glfwRequiredInstanceExtensionsTable.add_row({ "Name" });
        for (int i {}; i < glfwExtensionCount; ++i) {
            glfwRequiredInstanceExtensionsTable.add_row({ glfwExtensions[i] });
        }
        std::cout << glfwRequiredInstanceExtensionsTable << std::endl;

        // 设置要使用的扩展
        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;

        // 设置校验层（先不开启）
        createInfo.enabledLayerCount = 0;
        // createInfo.enabledExtensionCount = ?

        // 查询支持的拓展（非必须）
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               extensions.data());

        tabulate::Table availableExtensionsTable;
        std::cout << "支持的拓展:\n";
        availableExtensionsTable.add_row({ "Name", "Verison" });
        for (const auto &extension : extensions) {
            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        std::cout << availableExtensionsTable << std::endl;

        // create 并 check 有没有创建成功
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error { "failed to create instance!" };
        }
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        // 销毁 VkInstnace
        vkDestroyInstance(instance, nullptr);

        // 销毁 glfwWindow
        glfwDestroyWindow(window);
        // 停止 glfw
        glfwTerminate();
    }

private:
    GLFWwindow *window;

    VkInstance instance;
};

int main() {

    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
