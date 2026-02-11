#pragma once

#include "EasyVKStart.h"
#include "result_t.h"

#define DestroyHandleBy(Func)                                                  \
    if (handle) {                                                              \
        Func(vulkan::graphicsBase::Base().Device(), handle, nullptr);          \
        handle = VK_NULL_HANDLE;                                               \
    }

#define MoveHandle                                                             \
    handle = other.handle;                                                     \
    other.handle = VK_NULL_HANDLE;

#define DefineMoveAssignmentOperator(type)                                     \
    type &operator=(type &&other) {                                            \
        this->~type();                                                         \
        MoveHandle;                                                            \
        return *this;                                                          \
    }

#define DefineHandleTypeOperator                                               \
    operator decltype(handle)() const { return handle; }

#define DefineAddressFunction                                                  \
    const decltype(handle) *Address() const { return &handle; }

#define ExecuteOnce(...)                                                       \
    {                                                                          \
        static bool executed = false;                                          \
        if (executed)                                                          \
            return __VA_ARGS__;                                                \
        executed = true;                                                       \
    }

inline auto &outStream = std::cout;

namespace vulkan {
    // 全局常量用constexpr修饰定义在类外：
    constexpr VkExtent2D defaultWindowSize = { 1280, 720 };

    class graphicsBase {
        static graphicsBase singleton;
        graphicsBase() = default;
        graphicsBase(graphicsBase &&) = default;

        ~graphicsBase() {
            if (!instance) {
                return;
            }

            if (device) {
                WaitIdle();
                if (swapchain) {
                    ExecuteCallbacks(callbacks_destroySwapchain);
                    for (auto &i : swapchainImageViews) {
                        if (i) {
                            vkDestroyImageView(device, i, nullptr);
                        }
                    }
                    vkDestroySwapchainKHR(device, swapchain, nullptr);
                }
                ExecuteCallbacks(callbacks_destroyDevice);
                vkDestroyDevice(device, nullptr);
            }

            if (surface) {
                vkDestroySurfaceKHR(instance, surface, nullptr);
            }

            if (debugMessenger) {
                PFN_vkDestroyDebugUtilsMessengerEXT
                    vkDestroyDebugUtilsMessenger =
                        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                            vkGetInstanceProcAddr(
                                instance, "vkDestroyDebugUtilsMessengerEXT"));
                if (vkDestroyDebugUtilsMessenger) {
                    vkDestroyDebugUtilsMessenger(instance, debugMessenger,
                                                 nullptr);
                }
            }

            vkDestroyInstance(instance, nullptr);
        }

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
        VkSwapchainCreateInfoKHR swapchainCreateInfo {};

        uint32_t apiVersion = VK_API_VERSION_1_4;

        // 当前取得的交换链图像索引
        uint32_t currentImageIndex {};

        std::vector<void (*)()> callbacks_createSwapchain;
        std::vector<void (*)()> callbacks_destroySwapchain;

        std::vector<void (*)()> callbacks_createDevice;
        std::vector<void (*)()> callbacks_destroyDevice;

        // 以下函数用于创建debug messenger
        result_t CreateDebugMessenger() {
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

            VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo {};
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
                result_t result = vkCreateDebugUtilsMessenger(
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
        result_t GetQueueFamilyIndices(VkPhysicalDevice physicalDevice,
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
                    if (result_t result = vkGetPhysicalDeviceSurfaceSupportKHR(
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
        result_t CreateSwapchain_Internal() {
            if (result_t result = vkCreateSwapchainKHR(
                    device, &swapchainCreateInfo, nullptr, &swapchain)) {
                std::println("[ graphicsBase ] ERROR\nFailed to create a "
                             "swapchain!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            // 获取交换连图像
            uint32_t swapchainImageCount {};
            if (result_t result = vkGetSwapchainImagesKHR(
                    device, swapchain, &swapchainImageCount, nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of swapchain images!\nError code: {}",
                             string_VkResult(result));
                return result;
            }
            swapchainImages.resize(swapchainImageCount);
            if (result_t result = vkGetSwapchainImagesKHR(
                    device, swapchain, &swapchainImageCount,
                    swapchainImages.data())) {
                std::println("[ graphicsBase ] ERROR\nFailed to get swapchain "
                             "images!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            // 创建 image view
            swapchainImageViews.resize(swapchainImageCount);
            VkImageViewCreateInfo imageViewCreateInfo {};
            imageViewCreateInfo.sType =
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = swapchainCreateInfo.imageFormat;
            // imageViewCreateInfo.components = {}; / /四个成员皆为
            // VK_COMPONENT_SWIZZLE_IDENTITY
            imageViewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,
                                                     0, 1, 0, 1 };

            for (size_t i {}; i < swapchainImageCount; i++) {
                imageViewCreateInfo.image = swapchainImages[i];
                if (result_t result =
                        vkCreateImageView(device, &imageViewCreateInfo, nullptr,
                                          &swapchainImageViews[i])) {
                    std::println("[ graphicsBase ] ERROR\nFailed to create a "
                                 "swapchain image view!\nError code: {}",
                                 string_VkResult(result));
                    return result;
                }
            }
            return VK_SUCCESS;
        }

        static void ExecuteCallbacks(std::vector<void (*)()> callbacks) {
            for (size_t size = callbacks.size(), i = 0; i < size; ++i) {
                callbacks[i]();
            }
        }

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

        result_t UseLatestApiVersion() {
            if (vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                      "vkEnumerateInstanceVersion")) {
                return vkEnumerateInstanceVersion(&apiVersion);
            }

            return VK_SUCCESS;
        }

        result_t GetSurfaceFormats() {
            uint32_t surfaceFormatCount {};

            if (result_t result = vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice, surface, &surfaceFormatCount, nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of surface formats!\nError code: {}\n",
                             string_VkResult(result));
                return result;
            }

            if (!surfaceFormatCount) {
                std::println("[ graphicsBase ] ERROR\nFailed to find any "
                             "supported surface format!\n");
                abort();
            }

            availableSurfaceFormats.resize(surfaceFormatCount);
            result_t result = vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice, surface, &surfaceFormatCount,
                availableSurfaceFormats.data());

            if (result) {
                std::println("[ graphicsBase ] ERROR\nFailed to get surface "
                             "formats!\nError code: {}\n",
                             string_VkResult(result));
            }

            return result;
        }

        uint32_t CurrentImageIndex() const { return currentImageIndex; }

        result_t SetSurfaceFormat(VkSurfaceFormatKHR surfaceFormat) {
            bool formatIsAvailable {};

            if (!surfaceFormat.format) {
                // 如果格式未指定，只匹配色彩空间，图像格式有啥就用啥
                for (auto &i : availableSurfaceFormats) {
                    if (i.colorSpace == surfaceFormat.colorSpace) {
                        swapchainCreateInfo.imageFormat = i.format;
                        swapchainCreateInfo.imageColorSpace = i.colorSpace;
                        formatIsAvailable = true;
                        break;
                    }
                }
            } else { // 否则匹配格式和色彩空间
                for (auto &i : availableSurfaceFormats)
                    if (i.format == surfaceFormat.format &&
                        i.colorSpace == surfaceFormat.colorSpace) {
                        swapchainCreateInfo.imageFormat = i.format;
                        swapchainCreateInfo.imageColorSpace = i.colorSpace;
                        formatIsAvailable = true;
                        break;
                    }
            }

            // 如果没有符合的格式，恰好有个语义相符的错误代码
            if (!formatIsAvailable) {
                return VK_ERROR_FORMAT_NOT_SUPPORTED;
            }

            // 如果交换链已存在，调用 RecreateSwapchain() 重建交换链
            if (swapchain) {
                return RecreateSwapchain();
            }

            return VK_SUCCESS;
        }

        // 该函数用于创建交换链
        result_t CreateSwapchain(bool limitFrameRate = true,
                                 VkSwapchainCreateFlagsKHR flags = 0) {
            VkSurfaceCapabilitiesKHR surfaceCapabilities {};
            if (result_t result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
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

            swapchainCreateInfo.imageUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (surfaceCapabilities.supportedUsageFlags &
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
                swapchainCreateInfo.imageUsage |=
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if (surfaceCapabilities.supportedUsageFlags &
                VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
                swapchainCreateInfo.imageUsage |=
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            } else {
                std::println(
                    "[ graphicsBase ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT "
                    "isn't supported!");
            }

            if (availableSurfaceFormats.empty()) {
                if (result_t result = GetSurfaceFormats()) {
                    return result;
                }
            }

            if (!swapchainCreateInfo.imageFormat) {
                if (SetSurfaceFormat({ VK_FORMAT_R8G8B8A8_UNORM,
                                       VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) &&
                    SetSurfaceFormat({ VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })) {
                    // 如果找不到上述图像格式和色彩空间的组合，那只能有什么用什么，采用
                    // availableSurfaceFormats 中的第一组
                    swapchainCreateInfo.imageFormat =
                        availableSurfaceFormats[0].format;
                    swapchainCreateInfo.imageColorSpace =
                        availableSurfaceFormats[0].colorSpace;
                    std::println("[ graphicsBase ] WARNING\nFailed to select a "
                                 "four-component UNORM surface format!");
                }
            }

            uint32_t surfacePresentModeCount {};
            if (result_t result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice, surface, &surfacePresentModeCount,
                    nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of surface present modes!\nError code: {}",
                             string_VkResult(result));
                return result;
            }
            if (!surfacePresentModeCount) {
                std::println("[ graphicsBase ] ERROR\nFailed to find any "
                             "surface present mode!");
                abort();
            }

            std::vector<VkPresentModeKHR> surfacePresentModes(
                surfacePresentModeCount);
            if (result_t result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice, surface, &surfacePresentModeCount,
                    surfacePresentModes.data())) {
                std::println("[ graphicsBase ] ERROR\nFailed to get surface "
                             "present modes!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            if (!limitFrameRate) {
                for (size_t i {}; i < surfacePresentModeCount; ++i) {
                    if (surfacePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                        swapchainCreateInfo.presentMode =
                            VK_PRESENT_MODE_MAILBOX_KHR;
                        break;
                    }
                }
            }

            swapchainCreateInfo.sType =
                VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapchainCreateInfo.flags = flags;
            swapchainCreateInfo.surface = surface;
            // 独占模式
            swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainCreateInfo.clipped = VK_TRUE;

            // 创建交换链
            if (result_t result = CreateSwapchain_Internal()) {
                return result;
            }

            // 执行回调函数
            ExecuteCallbacks(callbacks_createSwapchain);

            return VK_SUCCESS;
        }

        // 该函数用于重建交换链
        result_t RecreateSwapchain() {
            VkSurfaceCapabilitiesKHR surfaceCapabilities {};

            if (result_t result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice, surface, &surfaceCapabilities)) {
                std::println(
                    "[ graphicsBase ] ERROR\nFailed to get physical device "
                    "surface capabilities!\nError code: {}",
                    string_VkResult(result));
                return result;
            }

            if (surfaceCapabilities.currentExtent.width == 0 ||
                surfaceCapabilities.currentExtent.height == 0) {
                return VK_SUBOPTIMAL_KHR;
            }

            swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
            // 可以重用一些参数
            swapchainCreateInfo.oldSwapchain = swapchain;

            result_t result = vkQueueWaitIdle(queue_graphics);
            // 仅在等待图形队列成功，且图形与呈现所用队列不同时等待呈现队列
            if (!result && queue_graphics != queue_presentation) {
                result = vkQueueWaitIdle(queue_presentation);
            }
            if (result) {
                std::println("[ graphicsBase ] ERROR\nFailed to wait for the "
                             "queue to be idle!\nError code: {}",
                             string_VkResult(result));
                return result;
            }

            for (auto &i : swapchainImageViews) {
                if (i) {
                    vkDestroyImageView(device, i, nullptr);
                }
            }

            swapchainImageViews.clear();

            // 创建新交换链及与之相关的对象
            if (result = CreateSwapchain_Internal()) {
                return result;
            }

            // 执行回调函数
            ExecuteCallbacks(callbacks_createSwapchain);

            return VK_SUCCESS;
        }

        // 以下函数用于创建 Vulkan 实例前
        void AddInstanceLayer(const char *layerName) {
            AddLayerOrExtension(instanceLayers, layerName);
        }

        void AddInstanceExtension(const char *extensionName) {
            AddLayerOrExtension(instanceExtensions, extensionName);
        }

        void AddCallback_CreateSwapchain(void (*function)()) {
            callbacks_createSwapchain.push_back(function);
        }

        void AddCallback_DestorySwapchain(void (*function)()) {
            callbacks_destroySwapchain.push_back(function);
        }

        void AddCallback_CreateDevice(void (*function)()) {
            callbacks_createDevice.push_back(function);
        }
        void AddCallback_DestroyDevice(void (*function)()) {
            callbacks_destroyDevice.push_back(function);
        }

        // 该函数用于创建 Vulkan 实例
        result_t CreateInstance(VkInstanceCreateFlags flags = 0) {
            if constexpr (ENABLE_DEBUG_MESSENGER) {
                AddInstanceLayer("VK_LAYER_KHRONOS_validation");
                AddInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }

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

            if (result_t result =
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
            if constexpr (ENABLE_DEBUG_MESSENGER) {
                // 创建完Vulkan实例后紧接着创建debug
                // messenger
                CreateDebugMessenger();
            }

            return VK_SUCCESS;
        }

        // 以下函数用于创建 Vulkan 实例失败后
        /**
         * @brief
         * 将传入的 span 中不可用的层和扩展设置为 nullptr, 然后与原本保存的
         * instanceLayers 比对，即可确定哪些层不可用
         *
         * @param layersToCheck
         * @return result_t 获取可用层或扩展列表时是否发生错误
         */
        result_t CheckInstanceLayers(std::span<const char *> layersToCheck) {
            uint32_t layerCount {};
            std::vector<VkLayerProperties> availableLayers;
            if (result_t result =
                    vkEnumerateInstanceLayerProperties(&layerCount, nullptr)) {
                std::println("[ graphicsBase ] ERROR\nFailed to get the count "
                             "of instance layers!");
                return result;
            }

            if (layerCount) {
                availableLayers.resize(layerCount);
                if (result_t result = vkEnumerateInstanceLayerProperties(
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
         * @return result_t 获取可用层或扩展列表时是否发生错误
         */
        result_t
        CheckInstanceExtensions(std::span<const char *> extensionsToCheck,
                                const char *layerName = nullptr) const {
            uint32_t extensionCount {};
            std::vector<VkExtensionProperties> availableExtensions;

            if (result_t result = vkEnumerateInstanceExtensionProperties(
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
                if (result_t result = vkEnumerateInstanceExtensionProperties(
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
        result_t GetPhysicalDevices() {
            uint32_t deviceCount {};

            if (result_t result = vkEnumeratePhysicalDevices(
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
            result_t result = vkEnumeratePhysicalDevices(
                instance, &deviceCount, availablePhysicalDevices.data());
            if (result) {
                std::println("[ graphicsBase ] ERROR\nFailed to enumerate "
                             "physical devices!\nError code: {}\n",
                             string_VkResult(result));
            }

            return result;
        }

        // 该函数用于指定物理设备并调用 GetQueueFamilyIndices(...) 获取队列索引
        result_t DeterminePhysicalDevice(uint32_t deviceIndex = 0,
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
                result_t result = GetQueueFamilyIndices(
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
        result_t CreateDevice(VkDeviceCreateFlags flags = 0) {
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

            if (result_t result = vkCreateDevice(
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

            ExecuteCallbacks(callbacks_createDevice);

            return VK_SUCCESS;
        }

        // 以下函数用于创建逻辑设备失败后
        result_t
        CheckDeviceExtensions(std::span<const char *> extensionsToCheck,
                              const char *layerName = nullptr) const {}

        void DeviceExtensions(const std::vector<const char *> &extensionNames) {
            deviceExtensions = extensionNames;
        }

        result_t WaitIdle() const {
            result_t result = vkDeviceWaitIdle(device);
            if (result) {
                std::println("[ graphicsBase ] ERROR\nFailed to wait for the "
                             "device to be idle!\nError code: {}",
                             string_VkResult(result));
            }
            return result;
        }

        result_t RecreateDevice(VkDeviceCreateFlags flags = 0) {
            if (device) {
                if (result_t result = WaitIdle();
                    result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
                    if (swapchain) {
                        ExecuteCallbacks(callbacks_destroySwapchain);
                        for (auto &i : swapchainImageViews) {
                            if (i) {
                                vkDestroyImageView(device, i, nullptr);
                            }
                        }
                        swapchainImageViews.clear();
                        vkDestroySwapchainKHR(device, swapchain, nullptr);
                        swapchain = VK_NULL_HANDLE;
                        swapchainCreateInfo = {};
                    }
                }
                ExecuteCallbacks(callbacks_destroyDevice);
                vkDestroyDevice(device, nullptr);
                device = VK_NULL_HANDLE;
            }
            return CreateDevice(flags);
        }

        void Terminate() {
            this->~graphicsBase();
            instance = VK_NULL_HANDLE;
            physicalDevice = VK_NULL_HANDLE;
            device = VK_NULL_HANDLE;
            surface = VK_NULL_HANDLE;
            swapchain = VK_NULL_HANDLE;
            swapchainCreateInfo = {};
            debugMessenger = VK_NULL_HANDLE;
        }

        // 该函数用于获取交换链图像索引到
        // currentImageIndex，以及在需要重建交换链时调用
        // RecreateSwapchain()、重建交换链后销毁旧交换链
        result_t SwapImage(VkSemaphore semaphore_imageIsAvailable) {
            // 销毁旧交换链（若存在）
            if (swapchainCreateInfo.oldSwapchain &&
                swapchainCreateInfo.oldSwapchain != swapchain) {
                vkDestroySwapchainKHR(device, swapchainCreateInfo.oldSwapchain,
                                      nullptr);
                swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
            }
            // 获取交换链图像索引
            while (VkResult result = vkAcquireNextImageKHR(
                       device, swapchain, UINT64_MAX,
                       semaphore_imageIsAvailable, VK_NULL_HANDLE,
                       &currentImageIndex)) {
                switch (result) {
                case VK_SUBOPTIMAL_KHR:
                case VK_ERROR_OUT_OF_DATE_KHR:
                    if (VkResult result = RecreateSwapchain()) {
                        return result;
                    }
                    break; // 注意重建交换链后仍需要获取图像索引，通过 break
                           // 递归，再次执行 while 的条件判定语句
                default:
                    std::println("[ graphicsBase ] ERROR\nFailed to acquire "
                                 "the next image!\nError code: {}",
                                 string_VkResult(result));
                    return result;
                }
            }
            return VK_SUCCESS;
        }
    };

    class fence {
        VkFence handle = VK_NULL_HANDLE;

    public:
        // fence() = default;
        fence(VkFenceCreateInfo &createInfo) { Create(createInfo); }

        // 默认构造器创建未置位的栅栏
        fence(VkFenceCreateFlags flags = 0) { Create(flags); }

        fence(fence &&other) noexcept { MoveHandle; }

        ~fence() { DestroyHandleBy(vkDestroyFence); }

        // Getter
        DefineHandleTypeOperator;

        DefineAddressFunction;

        // Const Function
        result_t Wait() const {
            VkResult result = vkWaitForFences(graphicsBase::Base().Device(), 1,
                                              &handle, false, UINT64_MAX);
            if (result) {
                std::println("[ fence ] ERROR\nFailed to wait for the "
                             "fence!\nError code: {}",
                             string_VkResult(result));
            }

            return result;
        }

        result_t Reset() const {
            VkResult result =
                vkResetFences(graphicsBase::Base().Device(), 1, &handle);
            if (result) {
                std::println("[ fence ] ERROR\nFailed to reset the "
                             "fence!\nError code: {}",
                             string_VkResult(result));
            }

            return result;
        }

        // 因为“等待后立刻重置”的情形经常出现，定义此函数
        result_t WaitAndReset() const {
            VkResult result = Wait();
            result || (result = Reset());
            return result;
        }

        result_t Status() const {
            VkResult result =
                vkGetFenceStatus(graphicsBase::Base().Device(), handle);
            if (result <
                0) // vkGetFenceStatus(...)成功时有两种结果，所以不能仅仅判断result是否非0
                std::println("[ fence ] ERROR\nFailed to get the status of the "
                             "fence!\nError code: {}",
                             string_VkResult(result));
            return result;
        }

        // Non-const Function
        result_t Create(VkFenceCreateInfo &createInfo) {
            createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkResult result = vkCreateFence(graphicsBase::Base().Device(),
                                            &createInfo, nullptr, &handle);
            if (result) {
                std::println("[ fence ] ERROR\nFailed to create a "
                             "fence!\nError code: {}",
                             string_VkResult(result));
            }

            return result;
        }

        result_t Create(VkFenceCreateFlags flags = 0) {
            VkFenceCreateInfo createInfo {};
            createInfo.flags = flags;
            return Create(createInfo);
        }
    };

    class semaphore {
        VkSemaphore handle = VK_NULL_HANDLE;

    public:
        // semaphore() = default;
        semaphore(VkSemaphoreCreateInfo &createInfo) { Create(createInfo); }

        // 默认构造器创建未置位的信号量
        semaphore(/*VkSemaphoreCreateFlags flags*/) { Create(); }

        semaphore(semaphore &&other) noexcept { MoveHandle; }

        ~semaphore() { DestroyHandleBy(vkDestroySemaphore); }

        // Getter
        DefineHandleTypeOperator;

        DefineAddressFunction;

        // Non-const Function
        result_t Create(VkSemaphoreCreateInfo &createInfo) {
            createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkResult result = vkCreateSemaphore(graphicsBase::Base().Device(),
                                                &createInfo, nullptr, &handle);
            if (result) {
                std::println("[ semaphore ] ERROR\nFailed to create a "
                             "semaphore!\nError code: {}\n",
                             string_VkResult(result));
            }

            return result;
        }

        result_t Create(/*VkSemaphoreCreateFlags flags*/) {
            VkSemaphoreCreateInfo createInfo = {};
            return Create(createInfo);
        }
    };

    graphicsBase graphicsBase::singleton;
} // namespace vulkan
