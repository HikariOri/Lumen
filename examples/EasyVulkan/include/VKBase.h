#pragma once

#include "EasyVKStart.h"

namespace vulkan {
    // 全局常量用constexpr修饰定义在类外：
    constexpr VkExtent2D defaultWindowSize = { 1280, 720 };

    class graphicsBase {
        static graphicsBase singleton;
        graphicsBase() = default;
        graphicsBase(graphicsBase &&) = default;
        ~graphicsBase() {}

    public:
        static graphicsBase &Base() { return singleton; }

    private:
        VkInstance instance;
        std::vector<const char *> instanceLayers;
        std::vector<const char *> instanceExtensions;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;

        VkPhysicalDevice physicalDevice;
        VkPhysicalDeviceProperties physicalDeviceProperties;
        VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
        std::vector<VkPhysicalDevice> availablePhysicalDevices;

        VkDevice device;
        // 有效的索引从 0
        // 开始，因此使用特殊值 VK_QUEUE_FAMILY_IGNORED（为
        // UINT32_MAX）为队列族索引的默认值
        uint32_t queueFamilyIndex_graphics = VK_QUEUE_FAMILY_IGNORED;
        uint32_t queueFamilyIndex_presentation = VK_QUEUE_FAMILY_IGNORED;
        uint32_t queueFamilyIndex_compute = VK_QUEUE_FAMILY_IGNORED;
        VkQueue queue_graphics;
        VkQueue queue_presentation;
        VkQueue queue_compute;

        std::vector<const char *> deviceExtensions;

        std::vector<VkSurfaceFormatKHR> availableSurfaceFormats;

        VkSwapchainKHR swapchain;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> swapchainImageViews;
        // 保存交换链的创建信息以便重建交换链
        VkSwapchainCreateInfoKHR swapchainCreateInfo = {};

        uint32_t apiVersion = VK_API_VERSION_1_4;

        // 以下函数用于创建debug messenger
        VkResult CreateDebugMessenger() {
            static PFN_vkDebugUtilsMessengerCallbackEXT
                DebugUtilsMessengerCallback =
                    [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                       VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                       const VkDebugUtilsMessengerCallbackDataEXT
                           *pCallbackData,
                       void *pUserData) -> VkBool32 {
                std::println("{}\n", pCallbackData->pMessage);
                return VK_FALSE;
            };

            VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo;
            debugUtilsMessengerCreateInfo.sType =
                VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugUtilsMessengerCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugUtilsMessengerCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugUtilsMessengerCreateInfo.pfnUserCallback =
                DebugUtilsMessengerCallback;

            auto vkCreateDebugUtilsMessenger =
                reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance,
                                          "vkCreateDebugUtilsMessengerEXT"));
            if (vkCreateDebugUtilsMessenger) {
                VkResult result = vkCreateDebugUtilsMessenger(
                    instance, &debugUtilsMessengerCreateInfo, nullptr,
                    &debugMessenger);
                if (result)
                    std::println("[ graphicsBase ] ERROR\nFailed to create a "
                                 "debug messenger!\nError code: {}",
                                 string_VkResult(result));
                return result;
            }
            std::println("[ graphicsBase ] ERROR\nFailed to get the function "
                         "pointer of vkCreateDebugUtilsMessengerEXT!");
            return VK_RESULT_MAX_ENUM;
        }

        static void AddLayerOrExtension(std::vector<const char *> &container,
                                        const char *name) {
            for (auto &i : container) {
                if (!strcmp(name, i)) {
                    return;
                }
            }
            container.push_back(name);
        }

        // 该函数被　DeterminePhysicalDevice(...)
        // 调用，用于检查物理设备是否满足所需的队列族类型，
        // 并将对应的队列族索引返回到　queueFamilyIndices，执行成功时直接将索引写入相应成员变量
        VkResult GetQueueFamilyIndices(VkPhysicalDevice physicalDevice,
                                       bool enableGraphicsQueue,
                                       bool enableComputeQueue,
                                       uint32_t (&queueFamilyIndices)[3]) {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &queueFamilyCount, nullptr);
            if (!queueFamilyCount) {
                return VK_RESULT_MAX_ENUM;
            }

            std::vector<VkQueueFamilyProperties> queueFamilyPropertieses(
                queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &queueFamilyCount,
                queueFamilyPropertieses.data());

            auto &[ig, ip, ic] = queueFamilyIndices;
            ig = ip = ic = VK_QUEUE_FAMILY_IGNORED;

            for (uint32_t i = 0; i < queueFamilyCount; i++) {
                // 这三个 VkBool32
                // 变量指示是否可获取（指应该被获取且能获取）相应队列族索引

                // 只在
                // enableGraphicsQueue 为 true 时获取支持图形操作的队列族的索引
                VkBool32 supportGraphics =
                    enableGraphicsQueue &&
                    (queueFamilyPropertieses[i].queueFlags &
                     VK_QUEUE_GRAPHICS_BIT);
                VkBool32 supportPresentation = false;
                // 只在 enableComputeQueue 为 true
                // 时获取支持计算的队列族的索引
                VkBool32 supportCompute =
                    enableComputeQueue &&
                    (queueFamilyPropertieses[i].queueFlags &
                     VK_QUEUE_COMPUTE_BIT);

                // 只在创建了 window surface 时获取支持呈现的队列族的索引
                if (surface) {
                    if (VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
                            physicalDevice, i, surface, &supportPresentation)) {
                        std::println("[ graphicsBase ] ERROR\nFailed to "
                                     "determine if the queue family supports "
                                     "presentation!\nError code: {}",
                                     string_VkResult(result));
                        return result;
                    }
                }
                // 若某队列族同时支持图形操作和计算
                if (supportGraphics && supportCompute) {
                    // 若需要呈现，最好是三个队列族索引全部相同
                    if (supportPresentation) {
                        ig = ip = ic = i;
                        break;
                    }
                    // 除非 ig 和 ic 都已取得且相同，否则将它们的值覆写为
                    // i，以确保两个队列族索引相同
                    if (ig != ic || ig == VK_QUEUE_FAMILY_IGNORED) {
                        ig = ic = i;
                    }
                    // 如果不需要呈现，那么已经可以 break 了
                    if (!surface) {
                        break;
                    }
                }
                // 若任何一个队列族索引可以被取得但尚未被取得，将其值覆写为 i
                if (supportGraphics && ig == VK_QUEUE_FAMILY_IGNORED) {
                    ig = i;
                }
                if (supportPresentation && ip == VK_QUEUE_FAMILY_IGNORED) {
                    ip = i;
                }
                if (supportCompute && ic == VK_QUEUE_FAMILY_IGNORED) {
                    ic = i;
                }
            }

            if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
                ip == VK_QUEUE_FAMILY_IGNORED && surface ||
                ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue) {
                return VK_RESULT_MAX_ENUM;
            }

            queueFamilyIndex_graphics = ig;
            queueFamilyIndex_presentation = ip;
            queueFamilyIndex_compute = ic;

            return VK_SUCCESS;
        }

        // 该函数被 CreateSwapchain(...) 和 RecreateSwapchain() 调用
        VkResult CreateSwapchain_Internal() {}

    public:
        // Getter
        VkInstance Instance() const { return instance; }
        const std::vector<const char *> &InstanceLayers() const {
            return instanceLayers;
        }

        VkSurfaceKHR Surface() const { return surface; }

        const std::vector<const char *> &InstanceExtensions() const {
            return instanceExtensions;
        }

        VkPhysicalDevice PhysicalDevice() const { return physicalDevice; }

        const VkPhysicalDeviceProperties &PhysicalDeviceProperties() const {
            return physicalDeviceProperties;
        }

        const VkPhysicalDeviceMemoryProperties &
        PhysicalDeviceMemoryProperties() const {
            return physicalDeviceMemoryProperties;
        }

        VkPhysicalDevice AvailablePhysicalDevice(uint32_t index) const {
            return availablePhysicalDevices[index];
        }

        VkDevice Device() const { return device; }

        uint32_t QueueFamilyIndex_Graphics() const {
            return queueFamilyIndex_graphics;
        }

        uint32_t QueueFamilyIndex_Presentation() const {
            return queueFamilyIndex_presentation;
        }

        uint32_t QueueFamilyIndex_Compute() const {
            return queueFamilyIndex_compute;
        }

        VkQueue Queue_Graphics() const { return queue_graphics; }

        VkQueue Queue_Presentation() const { return queue_presentation; }

        VkQueue Queue_Compute() const { return queue_compute; }

        const std::vector<const char *> &DeviceExtensions() const {
            return deviceExtensions;
        }

        const VkFormat &AvailableSurfaceFormat(uint32_t index) const {
            return availableSurfaceFormats[index].format;
        }

        const VkColorSpaceKHR &
        AvailableSurfaceColorSpace(uint32_t index) const {
            return availableSurfaceFormats[index].colorSpace;
        }

        uint32_t AvailableSurfaceFormatCount() const {
            return static_cast<uint32_t>(availableSurfaceFormats.size());
        }

        VkSwapchainKHR Swapchain() const {}

        VkImage SwapchainImage(uint32_t index) const {
            return swapchainImages[index];
        }

        VkImageView SwapchainImageView(uint32_t index) const {
            return swapchainImageViews[index];
        }

        uint32_t SwapchainImageCount() const {
            return static_cast<uint32_t>(swapchainImages.size());
        }

        const VkSwapchainCreateInfoKHR &SwapchainCreateInfo() const {
            return swapchainCreateInfo;
        }

        uint32_t ApiVersion() const { return apiVersion; }

        VkResult UseLatestApiVersion() {
            if (vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                      "vkEnumerateInstanceVersion")) {
                return vkEnumerateInstanceVersion(&apiVersion);
            }

            return VK_SUCCESS;
        }

        VkResult GetSurfaceFormats() { /*待Ch1-4填充*/ }

        VkResult
        SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat) { /*待Ch1-4填充*/ }
        // 该函数用于创建交换链
        VkResult CreateSwapchain(bool limitFrameRate = true,
                                 VkSwapchainCreateFlagsKHR flags = 0) {
            VkSurfaceCapabilitiesKHR surfaceCapabilities {};
            if (VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice, surface, &surfaceCapabilities)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get physical "
                             "device surface capabilities!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            swapchainCreateInfo.minImageCount =
                surfaceCapabilities.minImageCount +
                (surfaceCapabilities.maxImageCount >
                 surfaceCapabilities.minImageCount);

            swapchainCreateInfo.imageExtent =
                surfaceCapabilities.currentExtent.width == -1
                    ? VkExtent2D { std::clamp(
                                       defaultWindowSize.width,
                                       surfaceCapabilities.minImageExtent.width,
                                       surfaceCapabilities.maxImageExtent
                                           .width),
                                   std::clamp(defaultWindowSize.height,
                                              surfaceCapabilities.minImageExtent
                                                  .height,
                                              surfaceCapabilities.maxImageExtent
                                                  .height) }
                    : surfaceCapabilities.currentExtent;

            swapchainCreateInfo.imageArrayLayers = 1;
            swapchainCreateInfo.preTransform =
                surfaceCapabilities.currentTransform;

            if (surfaceCapabilities.supportedCompositeAlpha &
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
                swapchainCreateInfo.compositeAlpha =
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
            } else {
                for (size_t i = 0; i < 4; i++) {
                    if (surfaceCapabilities.supportedCompositeAlpha & 1 << i) {
                        swapchainCreateInfo.compositeAlpha =
                            VkCompositeAlphaFlagBitsKHR(
                                surfaceCapabilities.supportedCompositeAlpha &
                                1 << i);
                        break;
                    }
                }
            }
        }

        // 该函数用于重建交换链
        VkResult RecreateSwapchain() { /*待Ch1-4填充*/ }

        // 以下函数用于创建 Vulkan 实例前
        void AddInstanceLayer(const char *layerName) {
            AddLayerOrExtension(instanceLayers, layerName);
        }

        void AddInstanceExtension(const char *extensionName) {
            AddLayerOrExtension(instanceExtensions, extensionName);
        }

        // 该函数用于创建 Vulkan 实例
        VkResult CreateInstance(VkInstanceCreateFlags flags = 0) {
#ifndef NDEBUG
            AddInstanceLayer("VK_LAYER_KHRONOS_validation");
            AddInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
            VkApplicationInfo applicationInfo {};
            applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            applicationInfo.apiVersion = apiVersion;

            VkInstanceCreateInfo instanceCreateInfo {};
            instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instanceCreateInfo.flags = flags;
            instanceCreateInfo.pApplicationInfo = &applicationInfo;
            instanceCreateInfo.enabledLayerCount =
                uint32_t(instanceLayers.size());
            instanceCreateInfo.ppEnabledLayerNames = instanceLayers.data();
            instanceCreateInfo.enabledExtensionCount =
                uint32_t(instanceExtensions.size());
            instanceCreateInfo.ppEnabledExtensionNames =
                instanceExtensions.data();

            if (VkResult result =
                    vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
                result != VK_SUCCESS) {
                std::println("[ graphicsBase ] ERROR\nFailed to create a "
                             "vulkan instance!\nError code: {}",
                             static_cast<uint32_t>(result));
                return result;
            }
            // 成功创建Vulkan实例后，输出Vulkan版本
            std::println("Vulkan API Version: {}.{}.{}",
                         VK_API_VERSION_MAJOR(apiVersion),
                         VK_API_VERSION_MINOR(apiVersion),
                         VK_API_VERSION_PATCH(apiVersion));
#ifndef NDEBUG
            // 创建完Vulkan实例后紧接着创建debug messenger
            CreateDebugMessenger();
#endif
            return VK_SUCCESS;
        }

        // 以下函数用于创建 Vulkan 实例失败后
        /**
         * @brief
         * 将传入的 span 中不可用的层和扩展设置为 nullptr, 然后与原本保存的
         * instanceLayers 比对，即可确定哪些层不可用
         *
         * @param layersToCheck
         * @return VkResult 获取可用层或扩展列表时是否发生错误
         */
        VkResult CheckInstanceLayers(std::span<const char *> layersToCheck) {
            uint32_t layerCount {};
            std::vector<VkLayerProperties> availableLayers;
            if (VkResult result =
                    vkEnumerateInstanceLayerProperties(&layerCount, nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of instance layers!");
                return result;
            }

            if (layerCount) {
                availableLayers.resize(layerCount);
                if (VkResult result = vkEnumerateInstanceLayerProperties(
                        &layerCount, availableLayers.data())) {
                    std::println("[ graphicsBase ] ERROR\nFailed to enumerate "
                                 "instance layer properties!\nError code: {}",
                                 string_VkResult(result));
                    return result;
                }

                for (auto &i : layersToCheck) {
                    bool found = false;
                    for (auto &j : availableLayers)
                        if (!strcmp(i, j.layerName)) {
                            found = true;
                            break;
                        }
                    if (!found) {
                        i = nullptr;
                    }
                }
            } else {
                for (auto &i : layersToCheck) {
                    i = nullptr;
                }
            }

            // 一切顺利则返回VK_SUCCESS
            return VK_SUCCESS;
        }

        void InstanceLayers(const std::vector<const char *> &layerNames) {
            instanceLayers = layerNames;
        }

        /**
         * @brief 将传入的 span 中不可用的层和扩展设置为 nullptr,
         * 然后与原本保存的 instanceExtensions
         * 比对，即可确定哪些扩展不可用
         *
         * @param extensionsToCheck
         * @param layerName
         * @return VkResult 获取可用层或扩展列表时是否发生错误
         */
        VkResult
        CheckInstanceExtensions(std::span<const char *> extensionsToCheck,
                                const char *layerName = nullptr) const {
            uint32_t extensionCount {};
            std::vector<VkExtensionProperties> availableExtensions;

            if (VkResult result = vkEnumerateInstanceExtensionProperties(
                    layerName, &extensionCount, nullptr)) {
                layerName
                    ? std::println(
                          "[ graphicsBase ] ERROR\nFailed to get the count of "
                          "instance extensions!\nLayer name: {}",
                          layerName)
                    : std::println("[ graphicsBase ] ERROR\nFailed to get the "
                                   "count of instance extensions!");
                return result;
            }

            if (extensionCount) {
                availableExtensions.resize(extensionCount);
                if (VkResult result = vkEnumerateInstanceExtensionProperties(
                        layerName, &extensionCount,
                        availableExtensions.data())) {
                    std::println(
                        "[ graphicsBase ] ERROR\nFailed to enumerate instance "
                        "extension properties!\nError code: {}",
                        string_VkResult(result));
                    return result;
                }
                for (auto &i : extensionsToCheck) {
                    bool found = false;
                    for (auto &j : availableExtensions)
                        if (!strcmp(i, j.extensionName)) {
                            found = true;
                            break;
                        }
                    if (!found) {
                        i = nullptr;
                    }
                }
            } else {
                for (auto &i : extensionsToCheck) {
                    i = nullptr;
                }
            }

            return VK_SUCCESS;
        }

        void
        InstanceExtensions(const std::vector<const char *> &extensionNames) {
            instanceExtensions = extensionNames;
        }

        // 该函数用于选择物理设备前
        void Surface(VkSurfaceKHR surface) {
            if (!this->surface) {
                this->surface = surface;
            }
        }

        // 该函数用于创建逻辑设备前
        void AddDeviceExtension(const char *extensionName) {
            AddLayerOrExtension(deviceExtensions, extensionName);
        }

        // 该函数用于获取物理设备
        VkResult GetPhysicalDevices() {
            uint32_t deviceCount {};

            if (VkResult result = vkEnumeratePhysicalDevices(
                    instance, &deviceCount, nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of physical devices!\nError code: {}\n",
                             string_VkResult(result));
                return result;
            }

            if (!deviceCount) {
                std::println("[ graphicsBase ] ERROR\nFailed to find any "
                             "physical device supports vulkan!\n");
                abort();
            }

            availablePhysicalDevices.resize(deviceCount);
            VkResult result = vkEnumeratePhysicalDevices(
                instance, &deviceCount, availablePhysicalDevices.data());
            if (result) {
                std::println("[ graphicsBase ] ERROR\nFailed to enumerate "
                             "physical devices!\nError code: {}\n",
                             string_VkResult(result));
            }

            return result;
        }

        // 该函数用于指定物理设备并调用 GetQueueFamilyIndices(...) 获取队列索引
        VkResult DeterminePhysicalDevice(uint32_t deviceIndex = 0,
                                         bool enableGraphicsQueue = true,
                                         bool enableComputeQueue = true) {
            // 定义一个特殊值用于标记一个队列族索引已被找过但未找到
            static constexpr uint32_t notFound = INT32_MAX;
            // 定义队列族索引组合的结构体
            struct queueFamilyIndexCombination {
                uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
                uint32_t presentation = VK_QUEUE_FAMILY_IGNORED;
                uint32_t compute = VK_QUEUE_FAMILY_IGNORED;
            };
            // queueFamilyIndexCombinations
            // 用于为各个物理设备保存一份队列族索引组合
            static std::vector<queueFamilyIndexCombination>
                queueFamilyIndexCombinations(availablePhysicalDevices.size());
            auto &[ig, ip, ic] = queueFamilyIndexCombinations[deviceIndex];

            // 如果有任何队列族索引已被找过但未找到，返回 VK_RESULT_MAX_ENUM
            if (ig == notFound && enableGraphicsQueue ||
                ip == notFound && surface ||
                ic == notFound && enableComputeQueue) {
                return VK_RESULT_MAX_ENUM;
            }

            // 如果有任何队列族索引应被获取但还未被找过
            if (ig == VK_QUEUE_FAMILY_IGNORED && enableGraphicsQueue ||
                ip == VK_QUEUE_FAMILY_IGNORED && surface ||
                ic == VK_QUEUE_FAMILY_IGNORED && enableComputeQueue) {
                uint32_t indices[3];
                VkResult result = GetQueueFamilyIndices(
                    availablePhysicalDevices[deviceIndex], enableGraphicsQueue,
                    enableComputeQueue, indices);
                // 若 GetQueueFamilyIndices(...) 返回 VK_SUCCESS 或
                // VK_RESULT_MAX_ENUM（vkGetPhysicalDeviceSurfaceSupportKHR(...)
                // 执行成功但没找齐所需队列族），
                // 说明对所需队列族索引已有结论，保存结果到
                // queueFamilyIndexCombinations[deviceIndex] 中相应变量
                // 应被获取的索引若仍为
                // VK_QUEUE_FAMILY_IGNORED，说明未找到相应队列族，VK_QUEUE_FAMILY_IGNORED（~0u）与
                // INT32_MAX 做位与得到的数值等于 notFound
                if (result == VK_SUCCESS || result == VK_RESULT_MAX_ENUM) {
                    if (enableGraphicsQueue) {
                        ig = indices[0] & INT32_MAX;
                    }
                    if (surface) {
                        ip = indices[1] & INT32_MAX;
                    }
                    if (enableComputeQueue) {
                        ic = indices[2] & INT32_MAX;
                    }
                }
            } else {
                queueFamilyIndex_graphics =
                    enableGraphicsQueue ? ig : VK_QUEUE_FAMILY_IGNORED;
                queueFamilyIndex_presentation =
                    surface ? ip : VK_QUEUE_FAMILY_IGNORED;
                queueFamilyIndex_compute =
                    enableComputeQueue ? ic : VK_QUEUE_FAMILY_IGNORED;
            }
            physicalDevice = availablePhysicalDevices[deviceIndex];
            return VK_SUCCESS;
        }

        // 该函数用于创建逻辑设备，并获取队列
        VkResult CreateDevice(VkDeviceCreateFlags flags = 0) {
            float queuePriority = 1.F;
            VkDeviceQueueCreateInfo queueCreateInfos[3] {};
            queueCreateInfos[0].sType =
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[0].queueCount = 1;
            queueCreateInfos[0].pQueuePriorities = &queuePriority;

            queueCreateInfos[1].sType =
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[1].queueCount = 1;
            queueCreateInfos[1].pQueuePriorities = &queuePriority;

            queueCreateInfos[2].sType =
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[2].queueCount = 1;
            queueCreateInfos[2].pQueuePriorities = &queuePriority;

            uint32_t queueCreateInfoCount = 0;
            if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED) {
                queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex =
                    queueFamilyIndex_graphics;
            }
            if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED &&
                queueFamilyIndex_presentation != queueFamilyIndex_graphics) {
                queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex =
                    queueFamilyIndex_presentation;
            }
            if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED &&
                queueFamilyIndex_compute != queueFamilyIndex_graphics &&
                queueFamilyIndex_compute != queueFamilyIndex_presentation) {
                queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex =
                    queueFamilyIndex_compute;
            }

            VkPhysicalDeviceFeatures physicalDeviceFeatures;
            vkGetPhysicalDeviceFeatures(physicalDevice,
                                        &physicalDeviceFeatures);
            VkDeviceCreateInfo deviceCreateInfo {};
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.flags = flags;
            deviceCreateInfo.queueCreateInfoCount = queueCreateInfoCount;
            deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
            deviceCreateInfo.enabledExtensionCount =
                static_cast<uint32_t>(deviceExtensions.size());
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
            deviceCreateInfo.pEnabledFeatures = &physicalDeviceFeatures;

            if (VkResult result = vkCreateDevice(
                    physicalDevice, &deviceCreateInfo, nullptr, &device)) {
                std::println("[ graphicsBase ] ERROR\nFailed to create a "
                             "vulkan logical device!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED) {
                vkGetDeviceQueue(device, queueFamilyIndex_graphics, 0,
                                 &queue_graphics);
            }
            if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED) {
                vkGetDeviceQueue(device, queueFamilyIndex_presentation, 0,
                                 &queue_presentation);
            }
            if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED) {
                vkGetDeviceQueue(device, queueFamilyIndex_compute, 0,
                                 &queue_compute);
            }

            vkGetPhysicalDeviceProperties(physicalDevice,
                                          &physicalDeviceProperties);
            vkGetPhysicalDeviceMemoryProperties(
                physicalDevice, &physicalDeviceMemoryProperties);
            // 输出所用的物理设备名称
            std::println("Renderer: {}", physicalDeviceProperties.deviceName);
            /*待Ch1-4填充*/
            return VK_SUCCESS;
        }

        // 以下函数用于创建逻辑设备失败后
        VkResult
        CheckDeviceExtensions(std::span<const char *> extensionsToCheck,
                              const char *layerName = nullptr) const {}

        void DeviceExtensions(const std::vector<const char *> &extensionNames) {
            deviceExtensions = extensionNames;
        }
    };

    graphicsBase graphicsBase::singleton;
} // namespace vulkan
