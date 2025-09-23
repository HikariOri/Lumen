#include <cstdlib>
#include <exception>
#include <iostream>
#include <ranges>
#include <vector>

// #define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_NO_CONSTRUCTORS
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

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
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    }

    void initVulkan() { createInstance(); }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        glfwDestroyWindow(window);

        glfwTerminate();
    }

    void createInstance() {
        vk::ApplicationInfo appInfo;
        appInfo.setPApplicationName("Hello Triangle")
            .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
            .setPEngineName("No Engine.")
            .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
            .setApiVersion(vk::ApiVersion14);

        // Get the required instance extensions from GLFW.
        uint32_t glfwExtensionCount {};
        auto *glfwExtensions =
            glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        // Check if the required GLFW extensions are supported by the Vulkan
        // implementation.
        auto extensionProperties =
            context.enumerateInstanceExtensionProperties();
        for (uint32_t i {}; i < glfwExtensionCount; ++i) {
            if (std::ranges::none_of(
                    extensionProperties, [glfwExtension = glfwExtensions[i]](
                                             auto const &extensionProperty) {
                        return strcmp(extensionProperty.extensionName,
                                      glfwExtension) == 0;
                    })) {
                throw std::runtime_error(
                    "Required GLFW extension not supported: " +
                    std::string(glfwExtensions[i]));
            }
        }

        vk::InstanceCreateInfo createInfo;
        createInfo.setPApplicationInfo(&appInfo)
            .setEnabledExtensionCount(glfwExtensionCount)
            .setPpEnabledExtensionNames(glfwExtensions);

        instance = vk::raii::Instance(context, createInfo);
    }

    GLFWwindow *window {};

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
};

int main() {

    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
