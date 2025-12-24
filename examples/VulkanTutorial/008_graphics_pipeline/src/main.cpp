/*
1. 所有的查询基本上都在物理设备上
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <print>
#include <set>
#include <stdexcept>
#include <vector>

#include <tabulate/table.hpp>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// #define DEBUG_USER

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow *window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    // VkPhysicalDevice 会在 VkInstance 被销毁是隐式销毁，无需手动销毁
    // 事实上，你不可能使用代码销毁一张物理显卡
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    // 逻辑设备
    // 根据需要可能有多个
    VkDevice device;
    // 所有的队列（VkQueue 类型的实例）会随着逻辑设备的创建而自动创建
    // 也会随着逻辑设备的销毁而自动销毁
    // 我们要做的就是在创建逻辑设备之后使用 vkGetDeviceQueue 获取它
    VkQueue graphicsQueue;
    // 呈现队列
    VkQueue presentQueue;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    // 这些图像是由交换链创建，一旦交换链被销毁，它们将自动被清理，不需要手动清理
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;

    // 所有的 queue family 都使用 index 表示
    struct QueueFamilyIndices {
        // 支持图像的 queue family 的 index（支持绘制的 queue family）
        std::optional<uint32_t> graphicsFamily;
        // 支持呈现的 queue family
        std::optional<uint32_t> presentFamily;

        [[nodiscard]] bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }

        operator bool() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails {
        // 基本 surface 能力，例如交换链中图像的最大 / 最小数量、图像的最小 / 最大的宽度和高度
        VkSurfaceCapabilitiesKHR capabilities;
        // surface 的格式，例如像素格式、色彩空间
        std::vector<VkSurfaceFormatKHR> formats;
        // 可用的呈现模式
        std::vector<VkPresentModeKHR> presentModes;
    };

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

    void initVulkan() {
        createInstance();
        setupDebugMessenger(); // 这一步非必须
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createGraphicsPipeline();
    }

    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        // 为每个 image 创建一个对应的 image views
        for (size_t i = 0; i < swapChainImages.size(); ++i) {
            VkImageViewCreateInfo createInfo {};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapChainImages[i];

            // 指定将 image 视为 1D 纹理、2D 纹理、3D 纹理还是立方体贴图
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            // 指定格式
            createInfo.format = swapChainImageFormat;

            // components 字段可以交换颜色通道的顺序
            // 可以将 0 和 1 直接指定给某个通道
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            // subresourceRange 字段描述了图像的用途已经访问该图像的哪一部分
            // 颜色附件
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &createInfo, nullptr,
                                  &swapChainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error { "failed to create image views!" };
            }
        }
    }

    void createGraphicsPipeline() {}

    void createSurface() {
        // 创建 surface
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface)) {
            throw std::runtime_error { "failed to create window surface!" };
        }
    }

    void pickPhysicalDevice() {

        // 查询支持 Vulkan 的物理设备
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            throw std::runtime_error {
                "failed to find GPUs with Vulkan support;"
            };
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

#ifdef DEBUG_USER
        tabulate::Table physicalDevicesTable;
        std::println("\nGPUs:");
        physicalDevicesTable.add_row({ "Device Name", "Api Version",
                                       "Driver Version", "Vendor ID",
                                       "Device ID", "Device Type" });

        for (auto &device : devices) {

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            physicalDevicesTable.add_row({ props.deviceName,
                                           std::to_string(props.deviceType),
                                           std::to_string(props.apiVersion),
                                           std::to_string(props.driverVersion),
                                           std::to_string(props.vendorID),
                                           std::to_string(props.deviceID) });
        }
        std::println("{}", physicalDevicesTable.str());
#endif

        // check 所有的 Physical Device，有一个满足要求即可
        for (auto &device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error { "failed to find a suitable GPU!" };
        }
    }

    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            indices.graphicsFamily.value(), indices.presentFamily.value()
        };

        // 逻辑设备的创建指定 VkDeviceQueueCreateInfo 是必须的
        float queuePriority = 1.0F; // from 0 to 1
        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // 指定需要的物理设备的特性
        VkPhysicalDeviceFeatures deviceFeatures {};

        VkDeviceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        // 一次性指定图像队列和呈现队列
        createInfo.queueCreateInfoCount =
            static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        createInfo.pEnabledFeatures = &deviceFeatures;

        // 指定设备拓展
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // 注意，新的 Vulkan 已不再区分设备和示例的校验层
        // 这里为了兼容旧版本还是设置一下
        if constexpr (enableValidationLayers) {
            createInfo.enabledLayerCount =
                static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }

        // 获取图形队列
        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0,
                         &graphicsQueue);
        // 获取呈现队列
        vkGetDeviceQueue(device, indices.presentFamily.value(), 0,
                         &presentQueue);
    }

    void createSwapChain() {
        SwapChainSupportDetails swapChainSupport =
            querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat =
            chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode =
            chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        // 仅仅遵守这个最低要求意味着我们有时可能需要等待驱动程序完成内部操作，然后才能获取另一个用于渲染的图像，因此建议请求至少比最低数量多一个图像
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        // 当然也要保证不能让 swapchain 里的图片数量超过最大值
        // 0 是一个特殊值，表示没有最大数量限制
        if (swapChainSupport.capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;

        // 设置最少需要的 image 的数量
        createInfo.minImageCount = imageCount;
        // 设置图像格式
        createInfo.imageFormat = surfaceFormat.format;
        // 设置 color space
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        // 设置图像大小
        createInfo.imageExtent = extent;
        // imageArrayLayers 指定了每个图像包含的层数。这始终是 1，除非你正在开发立体 3D 应用程序。
        createInfo.imageArrayLayers = 1;
        // 因为颜色附件使用
        // TODO: 难道说 depth map 需要创建新的 swapchain?
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(),
                                          indices.presentFamily.value() };

        if (indices.graphicsFamily != indices.presentFamily) {
            //  图像可以在多个队列家族之间使用，而无需明确地转移所有权
            // 即自动处理图像在多个队列里的所有权转移
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            // 一张图像在任何时候只能属于一个队列家族，在使用它之前必须明确地转移所有权到另一个队列家族。这个选项提供最佳性能
            // 都是一个队列了，就没有转移了
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;     // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        // 如果支持，可以指定对交换链中的图像应用某种变换，例如 90 度顺时针旋转或水平翻转
        // 如果不希望有任何变换，只需指定当前变换即可
        createInfo.preTransform =
            swapChainSupport.capabilities.currentTransform;
        // compositeAlpha 字段指定是否应使用 alpha 通道与其他窗口系统中的其他窗口进行混。
        // 几乎总是希望简单地忽略 alpha 通道，因此设置为 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        // 是否裁剪，如果希望一直得到完整的图像应该设置为 VK_FALSE;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create swap chain!" };
        }

        // 获取 swapchain 中的 images
        // 注意，之前只在交换链中指定了最小的 image count
        // 实际创建的 image 数量可能大于那个数量，所以要查询数量
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapChain, &imageCount,
                                swapChainImages.data());

        // 将交换链图像的格式和大小储存起来
        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);

        bool extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        if (extensionsSupported) {
            SwapChainSupportDetails swapChainSupport =
                querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() &&
                                !swapChainSupport.presentModes.empty();
        }

        return indices.isComplete() && extensionsSupported && swapChainAdequate;
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR> &availableFormats) {
        // 每个 VkSurfaceFormatKHR 条目包含一个 format 和一个 colorSpace 成员
        // format 成员指定颜色通道和类型。例如，VK_FORMAT_B8G8R8A8_SRGB, 表示按顺序存储 B、G、R 和 A
        // 使用 8 位无符号整数，每个像素总共 32 位
        // colorSpace  成员使用 VK_COLOR_SPACE_SRGB_NONLINEAR_KHR

        // 查找满足条件的 VkSurfaceFormatKHR
        for (const auto &availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                availableFormat.colorSpace ==
                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        // 查找失败使用第一个
        // 当然也可以排序以选取最优的
        return availableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR> &availablePresentModes) {
        /*
显示模式可以说是交换链最重要的设置，因为它代表了向屏幕显示图像的实际条件。Vulkan 中有四种可能的模式可用：
    VK_PRESENT_MODE_IMMEDIATE_KHR: 
        （立即模式）你的应用程序提交的图像会立即传输到屏幕上，这可能导致撕裂。
    VK_PRESENT_MODE_FIFO_KHR:
        （FIFO）交换链是一个队列，显示器在刷新时从前端队列中获取图像，而程序在队列后端插入渲染的图像。
        如果队列已满，程序就必须等待。
        这最类似于现代游戏中常见的垂直同步。
        显示器刷新的时刻被称为"垂直空白"。
        此模式一定会被支持。
    VK_PRESENT_MODE_FIFO_RELAXED_KHR: 
        这种模式只有在应用程序延迟且在最后一个垂直空白时队列为空的情况下才与之前的不同。
        在这种情况下，图像在最终到达时立即传输，而不是等待下一个垂直空白。
        这可能导致可见的撕裂。
    VK_PRESENT_MODE_MAILBOX_KHR: 
        这是第二种模式的另一种变体。
        当队列满时，它不会阻塞应用程序，而是将已入队的图像简单地用较新的图像替换。
        这种模式可以在避免撕裂的同时尽可能快地渲染帧，从而比标准垂直同步产生更少的延迟问题。
        这通常被称为"三重缓冲"，尽管仅存在三个缓冲区并不一定意味着帧率被解锁。
            */

        // 一般使用 VK_PRESENT_MODE_MAILBOX_KHR就好
        // 对于资源有限的移动端可以考虑是使用 VK_PRESENT_MODE_FIFO_KHR

        for (const auto &availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
        if (capabilities.currentExtent.width !=
            std::numeric_limits<uint32_t>::max()) {
            // 如果窗口管理系统没有将宽度和高度设置为最大的话，说明 currentExtent 是可用的
            return capabilities.currentExtent;
        } else {
            int width {}, height {};
            // 考虑到高 DPI 显示器可能会导致分辨率大于屏幕坐标，使用最好使用 glfwGetFramebufferSize 查询窗口分辨率（像素）
            glfwGetFramebufferSize(window, &width, &height);

            VkExtent2D actualExtent {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height),
            };

            // 将 actualExtent 的宽高都限制一下
            actualExtent.width = std::clamp(actualExtent.width,
                                            capabilities.minImageExtent.width,
                                            capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(
                actualExtent.height, capabilities.minImageExtent.height,
                capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        // 查询所有受支持的设备拓展
        uint32_t extensionCount {};
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                             nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                             availableExtensions.data());

#ifdef DEBUG_USER
        tabulate::Table availableExtensionsTable;
        std::println("\n所支持的设备拓展:");
        availableExtensionsTable.add_row({ "Extension Name", "Spec Version" });

        for (auto &extension : availableExtensions) {

            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        std::println("{}", availableExtensionsTable.str());
#endif

        std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                                 deviceExtensions.end());

        // check 所有需要的拓展是否被支持
        // 事实上，呈现队列的可用性说明 swapchain 拓展一定是可用的
        for (const auto &extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        // 查找一个支持图形的 queue family
        QueueFamilyIndices indices;

        // 查询 pick 到的物理设备的支持的所有的 queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                                 queueFamilies.data());

#ifdef DEBUG_USER
        tabulate::Table queueFamiliesTable;
        std::println("\n当前设备支持的 queue family:");
        queueFamiliesTable.add_row({ "Queue Flags", "Queue Count",
                                     "Timestamp Valid Bits",
                                     "Min Image Transfer Granularity" });
        for (const auto &queueFamily : queueFamilies) {
            queueFamiliesTable.add_row(
                { std::to_string(queueFamily.queueFlags),
                  std::to_string(queueFamily.queueCount),
                  std::to_string(queueFamily.timestampValidBits),
                  std::format("{}, {}, {}",
                              queueFamily.minImageTransferGranularity.width,
                              queueFamily.minImageTransferGranularity.height,
                              queueFamily.minImageTransferGranularity.depth) });
        }
        std::println("{}", queueFamiliesTable.str());
#endif

        // 查找支持图形的 queuec family
        int i = 0;
        for (const auto &queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            // 获取支持呈现的 queue family
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface,
                                                 &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }

            i++;
        }

        return indices;
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void cleanup() {
        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        vkDestroyDevice(device, nullptr);

        if constexpr (enableValidationLayers) {
            // 销毁 VkDebugUtilsMessengerEXT
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }

        // 销毁 surface
        vkDestroySurfaceKHR(instance, surface, nullptr);
        // 销毁 VkInstnace
        vkDestroyInstance(instance, nullptr);

        // 销毁 glfwWindow
        glfwDestroyWindow(window);
        // 停止 glfw
        glfwTerminate();
    }

    void createInstance() {
        // 初始化 volk
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error { "failed to initialize volk!" };
        }

        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error(
                "validation layers requested, but not available!");
        }

        // VkXxXxx 类型对应的 sType 都是 VK_STRUCTURE_TYPE_XX_XXX

        // 创建 app info
        VkApplicationInfo appInfo {};
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

        // 获取要使用的扩展
        auto extensions = getRequiredExtensions();

#ifdef DEBUG_USER
        tabulate::Table requiredInstanceExtensionsTable;
        std::println("\n需要的扩展（glfw + 校验层）:");
        requiredInstanceExtensionsTable.add_row({ "Name" });
        for (const auto &extension : extensions) {
            requiredInstanceExtensionsTable.add_row({ extension });
        }
        std::println("{}", requiredInstanceExtensionsTable.str());
#endif

        // 设置要使用的扩展
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef DEBUG_USER
        // 查询支持的拓展（非必须）
        uint32_t availableExtensionsCount = 0;
        vkEnumerateInstanceExtensionProperties(
            nullptr, &availableExtensionsCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(
            availableExtensionsCount);
        vkEnumerateInstanceExtensionProperties(
            nullptr, &availableExtensionsCount, availableExtensions.data());

        tabulate::Table availableExtensionsTable;
        std::println("\n支持的拓展:");
        availableExtensionsTable.add_row({ "Name", "Verison" });
        for (const auto &extension : availableExtensions) {
            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        std::println("{}", availableExtensionsTable.str());
#endif

        // 创建并设置 VkDebugUtilsMessengerCreateInfoEXT，这样才能正确地报告错误
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {};
        if constexpr (enableValidationLayers) {
            createInfo.enabledLayerCount =
                static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext =
                (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
        } else {
            createInfo.enabledLayerCount = 0;

            createInfo.pNext = nullptr;
        }

        // create 并 check 有没有创建成功
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }

        // 加载实例级别的拓展
        volkLoadInstance(instance);
    }

    void populateDebugMessengerCreateInfo(
        VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
        createInfo = {};
        createInfo.sType =
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    void setupDebugMessenger() {
        if constexpr (!enableValidationLayers) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr,
                                           &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
    }

    std::vector<const char *> getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions {};
        // 查询 glfw 需要的 vulkan 拓展
        // 如果是 sdl 就查询 sdl 的
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char *> extensions(
            glfwExtensions, glfwExtensions + glfwExtensionCount);

        if constexpr (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    bool checkValidationLayerSupport() {
        // 查询支持的层
        uint32_t layerCount {};
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

#ifdef DEBUG_USER
        tabulate::Table availableLayersTable;
        std::println("\n支持的层:");
        availableLayersTable.add_row(
            { "Name", "Verison", "Description", "Implementation Version" });
        for (const auto &layer : availableLayers) {
            availableLayersTable.add_row(
                { layer.layerName, std::to_string(layer.specVersion),
                  layer.description,
                  std::to_string(layer.implementationVersion) });
        }
        std::println("{}", availableLayersTable.str());
#endif

        // check 是不是所有的 validationLayers 都在 availableLayers 中
        for (const char *layerName : validationLayers) {
            bool layerFound = false;

            for (const auto &layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        // 查询 surface 基本能力
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                                  &details.capabilities);

        // 查询 surface 的格式
        uint32_t formatCount {};
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                             nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                                 details.formats.data());
        }

        // 查询 surface 支持的呈现模式
        uint32_t presentModeCount {};
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                                  &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device, surface, &presentModeCount,
                details.presentModes.data());
        }

        return details;
    }

    /*
    messageSeverity: 指定消息的严重程度，它可以是以下标志之一
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: 调试信息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: 类似资源创建的信息性消息
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: 关于行为的信息，不一定是错误，但很可能是您的应用程序中的错误
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: 关于无效行为的信息，可能会导致崩溃
    messageType 参数可以有以下值：
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT：发生了与规范或性能无关的事件
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT：发生了违反规范或可能表示错误的情况
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT: 潜在的 Vulkan 非最优使用
    pCallbackData 参数指的是一个包含消息本身的详细信息的 VkDebugUtilsMessengerCallbackDataEXT 结构体，其中最重要的成员是：
        pMessage: 作为空终止字符串的调试消息
        pObjects: 与消息相关的 Vulkan 对象句柄数组
        objectCount: 数组中的对象数量
    pUserData 参数包含在回调设置期间指定的指针，允许你将其自己的数据传递给它。
    */
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                  void *pUserData) {
        std::println(stderr, "validation layer: {}", pCallbackData->pMessage);

        return VK_FALSE;
    }
};

int main() {

    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception &e) {
        std::println(stderr, "{}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
