#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <vector>

// #define VULKAN_HPP_NO_EXCEPTIONS
// #define VULKAN_HPP_NO_CONSTRUCTORS
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector validationLayers = { "VK_LAYER_KHRONOS_validation" };

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

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

    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
    }

    void createSurface() {
        VkSurfaceKHR _surface;
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface)) {
            throw std::runtime_error { "failed to create window surface!" };
        }
        surface = vk::raii::SurfaceKHR(instance, _surface);
    };

    void createLogicalDevice() {
        // find the index of the first queue family that supports graphics
        // 找到第一个支持图形的 queue
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
            physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports
        // graphics
        // 获取第一个支持图像的 queueFamilyProperties 的 index
        auto graphicsQueueFamilyProperty =
            std::ranges::find_if(queueFamilyProperties, [](auto const &qfp) {
                return (qfp.queueFlags & vk::QueueFlagBits::eGraphics) !=
                       static_cast<vk::QueueFlags>(0);
            });

        // 计算索引
        auto graphicsIndex = static_cast<uint32_t>(std::distance(
            queueFamilyProperties.begin(), graphicsQueueFamilyProperty));

        // determine a queueFamilyIndex that supports present
        // first check if the graphicsIndex is good enough
        // 挑一个支持 present 的 queueFamilyIndex，先判断 graphicsIndex 支不支持
        auto presentIndex =
            physicalDevice.getSurfaceSupportKHR(graphicsIndex, *surface)
                ? graphicsIndex
                : static_cast<uint32_t>(queueFamilyProperties.size());
        // 如果 graphicsIndex 不支持
        if (presentIndex == queueFamilyProperties.size()) {
            // the graphicsIndex doesn't support present -> look for another
            // family index that supports both graphics and present
            // 找一个同时支持图像和 prsent 的队列蔟
            for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
                if ((queueFamilyProperties[i].queueFlags &
                     vk::QueueFlagBits::eGraphics) &&
                    physicalDevice.getSurfaceSupportKHR(
                        static_cast<uint32_t>(i), *surface)) {
                    graphicsIndex = static_cast<uint32_t>(i);
                    presentIndex = graphicsIndex;
                    break;
                }
            }
            if (presentIndex == queueFamilyProperties.size()) {
                // there's nothing like a single family index that supports both
                // graphics and present -> look for another family index that
                // supports present
                // 没找到（同时支持图像和 perset 的）
                /// 找一个只支持 preset 的
                for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
                    if (physicalDevice.getSurfaceSupportKHR(
                            static_cast<uint32_t>(i), *surface)) {
                        presentIndex = static_cast<uint32_t>(i);
                        break;
                    }
                }
            }
        }
        // 如果没有找到支持图形或者 preset 的队列族，直接 throw
        if ((graphicsIndex == queueFamilyProperties.size()) ||
            (presentIndex == queueFamilyProperties.size())) {
            throw std::runtime_error { "Could not find a queue for graphics or "
                                       "present -> terminating" };
        }

        // query for Vulkan 1.3 features
        // 查询 Vulkan 1.3 的特性
        auto features = physicalDevice.getFeatures2();
        vk::PhysicalDeviceVulkan13Features vulkan13Features;
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
            extendedDynamicStateFeatures;
        vulkan13Features.setDynamicRendering(vk::True);
        extendedDynamicStateFeatures.setExtendedDynamicState(vk::True);
        vulkan13Features.setPNext(&extendedDynamicStateFeatures);
        features.setPNext(&vulkan13Features);
        // create a Device
        // 创建逻辑设备
        float queuePriority = 0.0F;
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo;
        deviceQueueCreateInfo.setQueueFamilyIndex(graphicsIndex)
            .setQueueCount(1)
            .setPQueuePriorities(&queuePriority);
        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.setPNext(&features)
            .setQueueCreateInfoCount(1)
            .setPQueueCreateInfos(&deviceQueueCreateInfo)
            .setEnabledExtensionCount(requiredDeviceExtension.size())
            .setPpEnabledExtensionNames(requiredDeviceExtension.data());

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
        presentQueue = vk::raii::Queue(device, presentIndex, 0);
    }

    void pickPhysicalDevice() {
        std::vector<vk::raii::PhysicalDevice> devices =
            instance.enumeratePhysicalDevices();
        const auto devIter = std::ranges::find_if(devices, [&](auto const
                                                                   &device) {
            // Check if the device supports the Vulkan 1.3 API version
            bool supportsVulkan1_3 =
                device.getProperties().apiVersion >= VK_API_VERSION_1_3;

            // Check if any of the queue families support graphics operations
            auto queueFamilies = device.getQueueFamilyProperties();
            bool supportsGraphics =
                std::ranges::any_of(queueFamilies, [](auto const &qfp) {
                    return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
                });

            // Check if all required device extensions are available
            auto availableDeviceExtensions =
                device.enumerateDeviceExtensionProperties();
            bool supportsAllRequiredExtensions = std::ranges::all_of(
                requiredDeviceExtension,
                [&availableDeviceExtensions](
                    auto const &requiredDeviceExtension) {
                    return std::ranges::any_of(
                        availableDeviceExtensions,
                        [requiredDeviceExtension](
                            auto const &availableDeviceExtension) {
                            return strcmp(
                                       availableDeviceExtension.extensionName,
                                       requiredDeviceExtension) == 0;
                        });
                });

            auto features = device.template getFeatures2<
                vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
            bool supportsRequiredFeatures =
                features.template get<vk::PhysicalDeviceVulkan13Features>()
                    .dynamicRendering &&
                features
                    .template get<
                        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
                    .extendedDynamicState;

            return supportsVulkan1_3 && supportsGraphics &&
                   supportsAllRequiredExtensions && supportsRequiredFeatures;
        });
        if (devIter != devices.end()) {
            physicalDevice = *devIter;
        } else {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void setupDebugMessenger() {
        if constexpr (!enableValidationLayers) {
            return;
        }

        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

        vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT;
        debugUtilsMessengerCreateInfoEXT.setMessageSeverity(severityFlags)
            .setMessageType(messageTypeFlags)
            .setPfnUserCallback(&debugCallback);

        debugMessenger = instance.createDebugUtilsMessengerEXT(
            debugUtilsMessengerCreateInfoEXT);
    };

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

        // Get the required layers
        std::vector<char const *> requiredLayers;
        if constexpr (enableValidationLayers) {
            requiredLayers.assign(validationLayers.begin(),
                                  validationLayers.end());
        }

        // Check if the required layers are supported by the Vulkan
        // implementation.
        auto layerProperties = context.enumerateInstanceLayerProperties();
        if (std::ranges::any_of(
                requiredLayers, [&layerProperties](auto const &requiredLayer) {
                    return std::ranges::none_of(
                        layerProperties,
                        [requiredLayer](auto const &layerProperty) {
                            return strcmp(layerProperty.layerName,
                                          requiredLayer) == 0;
                        });
                })) {
            throw std::runtime_error {
                "One or more required layers are not supported!"
            };
        }

        // Get the required extensions.
        auto requiredExtensions = getRequiredExtensions();

        // Check if the required extensions are supported by the Vulkan
        // implementation.
        auto extensionProperties =
            context.enumerateInstanceExtensionProperties();
        for (auto const &requiredExtension : requiredExtensions) {
            if (std::ranges::none_of(
                    extensionProperties,
                    [requiredExtension](auto const &extensionProperty) {
                        return strcmp(extensionProperty.extensionName,
                                      requiredExtension) == 0;
                    })) {
                throw std::runtime_error("Required extension not supported: " +
                                         std::string(requiredExtension));
            }
        }

        vk::InstanceCreateInfo createInfo;
        createInfo.setPApplicationInfo(&appInfo)
            .setEnabledLayerCount(static_cast<uint32_t>(requiredLayers.size()))
            .setPpEnabledLayerNames(requiredLayers.data())
            .setEnabledExtensionCount(
                static_cast<uint32_t>(requiredExtensions.size()))
            .setPpEnabledExtensionNames(requiredExtensions.data());
        instance = vk::raii::Instance(context, createInfo);
    }

    std::vector<const char *> getRequiredExtensions() {
        uint32_t glfwExtensionCount {};
        auto glfwExtensions =
            glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector extensions(glfwExtensions,
                               glfwExtensions + glfwExtensionCount);
        if constexpr (enableValidationLayers) {
            extensions.push_back(vk::EXTDebugUtilsExtensionName);
        }

        return extensions;
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
        std::cerr << "validation layer: type " << to_string(type)
                  << " msg: " << pCallbackData->pMessage << '\n';

        return vk::False;
    }

    GLFWwindow *window {};

    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;

    vk::raii::Queue graphicsQueue = nullptr;
    vk::raii::Queue presentQueue = nullptr;

    vk::raii::SurfaceKHR surface = nullptr;

    std::vector<const char *> requiredDeviceExtension = {
        vk::KHRSwapchainExtensionName, vk::KHRSpirv14ExtensionName,
        vk::KHRSynchronization2ExtensionName,
        vk::KHRCreateRenderpass2ExtensionName
    };
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
