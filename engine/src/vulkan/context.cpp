/**
 * @file context.cpp
 * @brief `vulkan::Context`：Vulkan 1.3 实例 / 设备 / 交换链 / VMA。
 */

#include "vulkan/context.hpp"

#include "core/log/logger.hpp"

namespace vulkan {

namespace {

constexpr std::uint32_t kApiVersion = VK_API_VERSION_1_4;

[[nodiscard]] bool format_has_depth_stencil_attachment(
    VkPhysicalDevice physicalDevice, VkFormat candidateFormat) {
    VkFormatProperties formatProps {};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, candidateFormat,
                                        &formatProps);
    return (formatProps.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

} // namespace

std::expected<std::unique_ptr<Context>, std::string> Context::create(
    const std::string_view appName, const std::uint32_t appVersion,
    const std::vector<const char *> &instanceExtensions,
    const std::uint32_t windowWidth, const std::uint32_t windowHeight,
    const std::function<VkSurfaceKHR(VkInstance)> &surfaceFromInstance,
    const bool enableValidation) {
    std::unique_ptr<Context> context { new Context() };
    if (auto initResult =
            context->init(appName, appVersion, surfaceFromInstance,
                          instanceExtensions, windowWidth, windowHeight,
                          enableValidation);
        !initResult.has_value()) {
        return std::unexpected(initResult.error());
    }
    return context;
}

Context::~Context() {
    cleanup_swapchain();

    if (vmaAllocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(vmaAllocator_);
        vmaAllocator_ = VK_NULL_HANDLE;
    }

    if (vkDevice_ != VK_NULL_HANDLE) {
        vkDestroyDevice(vkDevice_, nullptr);
        vkDevice_ = VK_NULL_HANDLE;
    }

    if (vkSurface_ != VK_NULL_HANDLE && vkInstance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vkInstance_, vkSurface_, nullptr);
        vkSurface_ = VK_NULL_HANDLE;
    }

    if (debugMessenger_ != VK_NULL_HANDLE && vkInstance_ != VK_NULL_HANDLE) {
        const auto destroyDebugMessengerFn =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(vkInstance_,
                                      "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessengerFn != nullptr) {
            destroyDebugMessengerFn(vkInstance_, debugMessenger_, nullptr);
        }
        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (vkInstance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(vkInstance_, nullptr);
        vkInstance_ = VK_NULL_HANDLE;
    }
}

std::expected<void, std::string> Context::init(
    const std::string_view appName, const std::uint32_t appVersion,
    const std::function<VkSurfaceKHR(VkInstance)> &surfaceFromInstance,
    const std::vector<const char *> &instanceExtensions,
    const std::uint32_t windowWidth, const std::uint32_t windowHeight,
    const bool enableValidation) {
    enableValidation_ = enableValidation;

    if (auto instanceResult =
            create_instance(appName, appVersion, instanceExtensions);
        !instanceResult.has_value()) {
        return std::unexpected(instanceResult.error());
    }

    if (enableValidation_) {
        setup_debug_messenger();
    }

    vkSurface_ = surfaceFromInstance(vkInstance_);
    if (vkSurface_ == VK_NULL_HANDLE) {
        return std::unexpected(std::string(
            "Context: surfaceFromInstance returned VK_NULL_HANDLE"));
    }

    if (auto pickResult = pick_physical_device(); !pickResult.has_value()) {
        return pickResult;
    }
    if (auto deviceResult = create_device(); !deviceResult.has_value()) {
        return deviceResult;
    }
    if (auto vmaResult = create_vma_allocator(); !vmaResult.has_value()) {
        return vmaResult;
    }
    if (auto swapchainResult =
            create_swapchain(windowWidth, windowHeight);
        !swapchainResult.has_value()) {
        return swapchainResult;
    }
    return {};
}

std::expected<void, std::string>
Context::create_instance(const std::string_view appName,
                         const std::uint32_t appVersion,
                         const std::vector<const char *> &instanceExtensions) {
    const std::string applicationNameOwned(appName);
    VkApplicationInfo applicationInfo { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    applicationInfo.pApplicationName = applicationNameOwned.c_str();
    applicationInfo.applicationVersion = appVersion;
    applicationInfo.pEngineName = "Lumen";
    applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = kApiVersion;

    std::set<std::string, std::less<>> extensionNameSet;
    for (const char *const extName : instanceExtensions) {
        if (extName != nullptr) {
            extensionNameSet.insert(extName);
        }
    }
    if (enableValidation_) {
        extensionNameSet.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char *> enabledExtensionNames;
    enabledExtensionNames.reserve(extensionNameSet.size());
    for (const std::string &name : extensionNameSet) {
        enabledExtensionNames.push_back(name.c_str());
    }

    std::vector<const char *> enabledLayerNames;
    if (enableValidation_) {
        enabledLayerNames.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo createInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount =
        static_cast<std::uint32_t>(enabledExtensionNames.size());
    createInfo.ppEnabledExtensionNames = enabledExtensionNames.data();
    createInfo.enabledLayerCount =
        static_cast<std::uint32_t>(enabledLayerNames.size());
    createInfo.ppEnabledLayerNames = enabledLayerNames.data();

    const VkResult createResult =
        vkCreateInstance(&createInfo, nullptr, &vkInstance_);
    if (createResult != VK_SUCCESS) {
        return std::unexpected(
            std::string("Context: vkCreateInstance failed ec=") +
            std::to_string(static_cast<int>(createResult)));
    }
    return {};
}

void Context::setup_debug_messenger() {
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT
    };
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &Context::debug_callback;
    messengerInfo.pUserData = this;

    const auto createDebugMessengerFn =
        reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(vkInstance_, "vkCreateDebugUtilsMessengerEXT"));
    if (createDebugMessengerFn == nullptr) {
        LUMEN_LOG_WARN("Context: vkCreateDebugUtilsMessengerEXT unavailable");
        return;
    }
    if (createDebugMessengerFn(vkInstance_, &messengerInfo, nullptr,
                               &debugMessenger_) != VK_SUCCESS) {
        LUMEN_LOG_WARN("Context: vkCreateDebugUtilsMessengerEXT failed");
        debugMessenger_ = VK_NULL_HANDLE;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL Context::debug_callback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    const VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void * /*userData*/) {
    if (callbackData == nullptr) {
        return VK_FALSE;
    }
    const char *const message =
        callbackData->pMessage != nullptr ? callbackData->pMessage : "";
    // 校验层回调内避免经 LUMEN_LOG_* 走编译期格式串（动态消息会触发 clang/fmt
    // 诊断）。
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::fprintf(stderr, "[Vulkan Validation][E] %s\n", message);
    } else if (messageSeverity >=
               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[Vulkan Validation][W] %s\n", message);
    } else {
        std::fprintf(stderr, "[Vulkan Validation][D] %s\n", message);
    }
    return VK_FALSE;
}

std::optional<std::uint32_t>
Context::find_graphics_queue_family(const VkPhysicalDevice physicalDevice) {
    std::uint32_t queueFamilyCount { 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilyProps.data());

    for (std::uint32_t familyIndex { 0 }; familyIndex < queueFamilyCount;
         ++familyIndex) {
        if ((queueFamilyProps[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) !=
            0U) {
            return familyIndex;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
Context::find_present_queue_family(const VkPhysicalDevice physicalDevice,
                                   const VkSurfaceKHR surface) {
    std::uint32_t queueFamilyCount { 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    for (std::uint32_t familyIndex { 0 }; familyIndex < queueFamilyCount;
         ++familyIndex) {
        VkBool32 presentSupported { VK_FALSE };
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex,
                                             surface, &presentSupported);
        if (presentSupported == VK_TRUE) {
            return familyIndex;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
Context::find_compute_queue_family(const VkPhysicalDevice physicalDevice,
                                   const std::uint32_t graphicsFamily) {
    std::uint32_t queueFamilyCount { 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilyProps.data());

    if (graphicsFamily < queueFamilyCount &&
        (queueFamilyProps[graphicsFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) !=
            0U) {
        return graphicsFamily;
    }
    for (std::uint32_t familyIndex { 0 }; familyIndex < queueFamilyCount;
         ++familyIndex) {
        if ((queueFamilyProps[familyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) !=
            0U) {
            return familyIndex;
        }
    }
    return std::nullopt;
}

VkFormat
Context::find_supported_depth_format(const VkPhysicalDevice physicalDevice,
                                     const std::vector<VkFormat> &candidates) {
    for (const VkFormat candidateFormat : candidates) {
        if (format_has_depth_stencil_attachment(physicalDevice,
                                                candidateFormat)) {
            return candidateFormat;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

std::expected<void, std::string> Context::pick_physical_device() {
    std::uint32_t deviceCount { 0 };
    vkEnumeratePhysicalDevices(vkInstance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        return std::unexpected(
            std::string("Context: no Vulkan physical devices"));
    }
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance_, &deviceCount,
                               physicalDevices.data());

    VkPhysicalDevice bestDevice { VK_NULL_HANDLE };
    std::optional<std::uint32_t> bestGraphicsFamily;
    std::optional<std::uint32_t> bestPresentFamily;
    std::optional<std::uint32_t> bestComputeFamily;
    int bestDeviceScore { -1 };

    for (VkPhysicalDevice candidateDevice : physicalDevices) {
        const auto graphicsFamily =
            find_graphics_queue_family(candidateDevice);
        const auto presentFamily =
            find_present_queue_family(candidateDevice, vkSurface_);
        if (!graphicsFamily.has_value() || !presentFamily.has_value()) {
            continue;
        }
        const auto computeFamily =
            find_compute_queue_family(candidateDevice, *graphicsFamily);
        if (!computeFamily.has_value()) {
            continue;
        }

        VkPhysicalDeviceProperties deviceProperties {};
        vkGetPhysicalDeviceProperties(candidateDevice, &deviceProperties);
        int deviceScore { 0 };
        if (deviceProperties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            deviceScore += 1000;
        } else if (deviceProperties.deviceType ==
                   VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            deviceScore += 100;
        }
        if (deviceScore > bestDeviceScore) {
            bestDeviceScore = deviceScore;
            bestDevice = candidateDevice;
            bestGraphicsFamily = graphicsFamily;
            bestPresentFamily = presentFamily;
            bestComputeFamily = computeFamily;
        }
    }

    if (bestDevice == VK_NULL_HANDLE || !bestGraphicsFamily.has_value() ||
        !bestPresentFamily.has_value() || !bestComputeFamily.has_value()) {
        return std::unexpected(
            std::string("Context: no suitable physical device (graphics + "
                        "present + compute)"));
    }

    physicalDevice_ = bestDevice;
    graphicsQueueFamily_ = *bestGraphicsFamily;
    presentQueueFamily_ = *bestPresentFamily;
    computeQueueFamily_ = *bestComputeFamily;

    static const std::vector<VkFormat> kDepthCandidates {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    depthFormat_ =
        find_supported_depth_format(physicalDevice_, kDepthCandidates);
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        LUMEN_LOG_WARN("Context: no supported depth attachment format");
    }

    return {};
}

std::expected<void, std::string> Context::create_device() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority { 1.0F };

    const std::vector<std::uint32_t> queueFamilyIndices {
        graphicsQueueFamily_,
        presentQueueFamily_,
        computeQueueFamily_,
    };
    std::set<std::uint32_t> uniqueFamilyIndices(queueFamilyIndices.begin(),
                                                queueFamilyIndices.end());

    for (const std::uint32_t familyIndex : uniqueFamilyIndices) {
        VkDeviceQueueCreateInfo queueCreateInfo {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        queueCreateInfo.queueFamilyIndex = familyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    const char *swapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    deviceCreateInfo.queueCreateInfoCount =
        static_cast<std::uint32_t>(queueCreateInfos.size());
    deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = &swapchainExtensionName;

    const VkResult createResult =
        vkCreateDevice(physicalDevice_, &deviceCreateInfo, nullptr, &vkDevice_);
    if (createResult != VK_SUCCESS) {
        return std::unexpected(
            std::string("Context: vkCreateDevice failed ec=") +
            std::to_string(static_cast<int>(createResult)));
    }

    vkGetDeviceQueue(vkDevice_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(vkDevice_, presentQueueFamily_, 0, &presentQueue_);
    vkGetDeviceQueue(vkDevice_, computeQueueFamily_, 0, &computeQueue_);
    return {};
}

std::expected<void, std::string> Context::create_vma_allocator() {
    VmaAllocatorCreateInfo allocatorInfo {};
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = vkDevice_;
    allocatorInfo.instance = vkInstance_;
    allocatorInfo.vulkanApiVersion = kApiVersion;

    const VkResult vmaResult =
        vmaCreateAllocator(&allocatorInfo, &vmaAllocator_);
    if (vmaResult != VK_SUCCESS) {
        return std::unexpected(
            std::string("Context: vmaCreateAllocator failed ec=") +
            std::to_string(static_cast<int>(vmaResult)));
    }
    return {};
}

std::expected<void, std::string>
Context::create_swapchain(const std::uint32_t width, const std::uint32_t height,
                          const VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR surfaceCaps {};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, vkSurface_,
                                                  &surfaceCaps) != VK_SUCCESS) {
        return std::unexpected(std::string(
            "Context: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed"));
    }

    std::uint32_t surfaceFormatCount { 0 };
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, vkSurface_,
                                         &surfaceFormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    if (surfaceFormatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice_, vkSurface_, &surfaceFormatCount,
            surfaceFormats.data());
    }

    std::uint32_t presentModeCount { 0 };
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, vkSurface_,
                                              &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice_, vkSurface_, &presentModeCount, presentModes.data());
    }

    VkSurfaceFormatKHR chosenSurfaceFormat { VK_FORMAT_B8G8R8A8_UNORM,
                                             VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    if (!surfaceFormats.empty()) {
        const auto preferredFormatIt = std::find_if(
            surfaceFormats.begin(), surfaceFormats.end(),
            [](const VkSurfaceFormatKHR &fmt) {
                return fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
                       fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            });
        if (preferredFormatIt != surfaceFormats.end()) {
            chosenSurfaceFormat = *preferredFormatIt;
        } else {
            chosenSurfaceFormat = surfaceFormats.front();
        }
    }

    VkPresentModeKHR chosenPresentMode { VK_PRESENT_MODE_FIFO_KHR };
    if (std::find(presentModes.begin(), presentModes.end(),
                  VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
        chosenPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    VkExtent2D swapchainExtent { width, height };
    if (surfaceCaps.currentExtent.width != UINT32_MAX) {
        swapchainExtent = surfaceCaps.currentExtent;
    } else {
        swapchainExtent.width =
            std::clamp(swapchainExtent.width, surfaceCaps.minImageExtent.width,
                       surfaceCaps.maxImageExtent.width);
        swapchainExtent.height =
            std::clamp(swapchainExtent.height, surfaceCaps.minImageExtent.height,
                       surfaceCaps.maxImageExtent.height);
    }

    std::uint32_t minImageCount { surfaceCaps.minImageCount + 1 };
    if (surfaceCaps.maxImageCount > 0 &&
        minImageCount > surfaceCaps.maxImageCount) {
        minImageCount = surfaceCaps.maxImageCount;
    }

    swapchainFormat_ = chosenSurfaceFormat.format;
    swapchainWidth_ = swapchainExtent.width;
    swapchainHeight_ = swapchainExtent.height;

    VkSwapchainCreateInfoKHR swapchainCreateInfo {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    swapchainCreateInfo.surface = vkSurface_;
    swapchainCreateInfo.minImageCount = minImageCount;
    swapchainCreateInfo.imageFormat = chosenSurfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = chosenSurfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = swapchainExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.preTransform = surfaceCaps.currentTransform;
    if ((surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) !=
        0U) {
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if ((surfaceCaps.supportedCompositeAlpha &
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0U) {
        swapchainCreateInfo.compositeAlpha =
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if ((surfaceCaps.supportedCompositeAlpha &
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0U) {
        swapchainCreateInfo.compositeAlpha =
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    } else {
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    swapchainCreateInfo.presentMode = chosenPresentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = oldSwapchain;

    if (graphicsQueueFamily_ != presentQueueFamily_) {
        const std::vector<std::uint32_t> concurrentQueueFamilies {
            graphicsQueueFamily_,
            presentQueueFamily_,
        };
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainCreateInfo.queueFamilyIndexCount =
            static_cast<std::uint32_t>(concurrentQueueFamilies.size());
        swapchainCreateInfo.pQueueFamilyIndices =
            concurrentQueueFamilies.data();
    } else {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    const VkResult swapchainCreateResult = vkCreateSwapchainKHR(
        vkDevice_, &swapchainCreateInfo, nullptr, &vkSwapchain_);
    if (swapchainCreateResult != VK_SUCCESS) {
        return std::unexpected(
            std::string("Context: vkCreateSwapchainKHR failed ec=") +
            std::to_string(static_cast<int>(swapchainCreateResult)));
    }

    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vkDevice_, oldSwapchain, nullptr);
    }

    std::uint32_t swapImageCount { 0 };
    vkGetSwapchainImagesKHR(vkDevice_, vkSwapchain_, &swapImageCount, nullptr);
    swapchainImages_.resize(swapImageCount);
    vkGetSwapchainImagesKHR(vkDevice_, vkSwapchain_, &swapImageCount,
                            swapchainImages_.data());

    swapchainImageViews_.clear();
    swapchainImageViews_.reserve(swapchainImages_.size());
    for (VkImage swapImage : swapchainImages_) {
        VkImageViewCreateInfo imageViewCreateInfo {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        imageViewCreateInfo.image = swapImage;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = swapchainFormat_;
        imageViewCreateInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        VkImageView imageView { VK_NULL_HANDLE };
        if (vkCreateImageView(vkDevice_, &imageViewCreateInfo, nullptr,
                              &imageView) != VK_SUCCESS) {
            cleanup_swapchain();
            return std::unexpected(
                std::string("Context: vkCreateImageView failed"));
        }
        swapchainImageViews_.push_back(imageView);
    }

    return {};
}

void Context::cleanup_swapchain() noexcept {
    for (VkImageView imageView : swapchainImageViews_) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice_, imageView, nullptr);
        }
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();

    if (vkSwapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vkDevice_, vkSwapchain_, nullptr);
        vkSwapchain_ = VK_NULL_HANDLE;
    }
}

std::expected<void, std::string>
Context::recreate_swapchain(const std::uint32_t width,
                            const std::uint32_t height) {
    if (vkDevice_ == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("Context::recreate_swapchain: no device"));
    }
    vkDeviceWaitIdle(vkDevice_);

    const VkSwapchainKHR previousSwapchain = vkSwapchain_;
    for (VkImageView imageView : swapchainImageViews_) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice_, imageView, nullptr);
        }
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();

    return create_swapchain(width, height, previousSwapchain);
}

} // namespace vulkan
