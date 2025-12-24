#include <cstdlib>
#include <stdexcept>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>

#include <tabulate/table.hpp>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

constexpr const char *const ICON_PATH = "./assets/icons/哈士奇.png";

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// #define DEBUG_USER

quill::Logger *logger = nullptr;

class HelloTriangleApplication {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    SDL_Window *window;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    // VkPhysicalDevice 会在 VkInstance 被销毁是隐式销毁，无需手动销毁
    // 事实上，你不可能使用代码销毁一张物理显卡
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    // 逻辑设备
    // 根据需要可能有多个
    VkDevice device;
    // 队列会随着逻辑设备的创建而自动创建
    // 也会随着逻辑设备的销毁而自动销毁
    // 我们要做的就是在创建逻辑设备之后使用 vkGetDeviceQueue 获取它
    VkQueue graphicsQueue;

    // 所有的 queue family 都使用 index 表示
    struct QueueFamilyIndices {
        std::optional<uint32_t>
            graphicsFamily; // 支持图像的 queue family 的 index

        [[nodiscard]] bool isComplete() const {
            return graphicsFamily.has_value();
        }

        operator bool() const { return graphicsFamily.has_value(); }
    };

private:
    void initWindow() {

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(
                std::string("Couldn't initialize SDL: {} ") + SDL_GetError());
        }

        window = SDL_CreateWindow("Vulkan ", WIDTH, HEIGHT,
                                  SDL_WINDOW_VULKAN /*|SDL_WINDOW_RESIZABLE*/);

        SDL_Surface *icon = IMG_Load(ICON_PATH);
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        } else {
            LOG_WARNING(logger, "failed to load icon: {}", ICON_PATH);
        }

        if (!window) {
            throw std::runtime_error(std::string("Couldn't create window: ") +
                                     SDL_GetError());
        }
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger(); // 这一步非必须
        pickPhysicalDevice();
        createLogicalDevice();
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

        // for ( auto &device : devices) {
        //     std::cout << device << '\n';
        // }

        // check 所有的 Physical Device，有一个满足要求即可
        for (auto &device : devices) {
            if (isDevicesSuitable(device)) {
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

        // 逻辑设备的创建指定 VkDeviceQueueCreateInfo 是必须的
        VkDeviceQueueCreateInfo queueCreateInfo {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex =
            indices.graphicsFamily.value(); // 只考虑图形队列
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0F;                        // from 0 to 1
        queueCreateInfo.pQueuePriorities = &queuePriority; // 指定优先级是必须的

        // 指定需要的物理设备的特性
        VkPhysicalDeviceFeatures deviceFeatures {};

        VkDeviceCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        // 创建逻辑设备
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;

        createInfo.pEnabledFeatures = &deviceFeatures;

        // 注意，新的 Vulkan 已不再区分设备和示例的校验层
        // 这里为了兼容旧版本还是设置一下
        createInfo.enabledExtensionCount = 0;
        if constexpr (enableValidationLayers) {
            createInfo.enabledLayerCount =
                static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create logical device!" };
        }

        vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0,
                         &graphicsQueue);
    }

    bool isDevicesSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);

        return indices.isComplete();
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
        LOG_DEBUG(logger, "当前设备支持的 queue family:");
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
        LOG_DEBUG(logger, "{}", queueFamiliesTable.str());
#endif

        // 查找支持图形的 queuec family
        int i = 0;
        for (const auto &queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }

            i++;
        }

        return indices;
    }

    void mainLoop() {
        bool running { true };
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
            }
        }
    }

    void cleanup() {
        vkDestroyDevice(device, nullptr);

        if constexpr (enableValidationLayers) {
            // 销毁 VkDebugUtilsMessengerEXT
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }

        // 销毁 VkInstnace
        vkDestroyInstance(instance, nullptr);

        SDL_DestroyWindow(window);
        SDL_Quit();
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
        LOG_DEBUG(logger, "需要的扩展（sdl + 校验层）:");
        requiredInstanceExtensionsTable.add_row({ "Name" });
        for (const auto &extension : extensions) {
            requiredInstanceExtensionsTable.add_row({ extension });
        }
        LOG_DEBUG(logger, "{}", requiredInstanceExtensionsTable.str());
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
        LOG_DEBUG(logger, "支持的拓展:");
        availableExtensionsTable.add_row({ "Name", "Verison" });
        for (const auto &extension : availableExtensions) {
            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        LOG_DEBUG(logger, "{}", availableExtensionsTable.str());
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
        uint32_t sdlExtensionCount = 0;
        const char *const *sdlExtensions =
            SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
        if (!sdlExtensions) {
            throw std::runtime_error(
                "SDL_Vulkan_GetInstanceExtensions failed: " +
                std::string(SDL_GetError()));
        }

#ifdef DEBUG_USER
        tabulate::Table sdlRequiredInstanceExtensionsTable;
        LOG_DEBUG(logger, "sdl 需要的扩展:");
        sdlRequiredInstanceExtensionsTable.add_row({ "Name" });
        for (int i {}; i < sdlExtensionCount; ++i) {
            sdlRequiredInstanceExtensionsTable.add_row({ sdlExtensions[i] });
        }
        LOG_DEBUG(logger, "{}", sdlRequiredInstanceExtensionsTable.str());
#endif

        std::vector<const char *> extensions(sdlExtensions,
                                             sdlExtensions + sdlExtensionCount);

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
        LOG_DEBUG(logger, "支持的层:");
        availableLayersTable.add_row(
            { "Name", "Verison", "Description", "Implementation Version" });
        for (const auto &layer : availableLayers) {
            availableLayersTable.add_row(
                { layer.layerName, std::to_string(layer.specVersion),
                  layer.description,
                  std::to_string(layer.implementationVersion) });
        }
        LOG_DEBUG(logger, "{}", availableLayersTable.str());
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
    pUserData 参数包含在回调设置期间指定的指针，允许您将其自己的数据传递给它。
    */
    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                  void *pUserData) {
        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
            LOG_DEBUG(logger, "validation layer: {}", pCallbackData->pMessage);
        } else if (messageSeverity &
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            LOG_INFO(logger, "validation layer: {}", pCallbackData->pMessage);
        } else if (messageSeverity &
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            LOG_WARNING(logger, "validation layer: {}",
                        pCallbackData->pMessage);
        } else if (messageSeverity &
                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            LOG_ERROR(logger, "validation layer: {}", pCallbackData->pMessage);
        }

        return VK_FALSE;
    }
};

void initLogger() {
    quill::BackendOptions backend_options;
    // 这样就禁用了默认的 “只允许 ASCII” 校验
    backend_options.check_printable_char = {};
    quill::Backend::start(backend_options);

    auto console_sink =
        quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");

    quill::PatternFormatterOptions fmt_options;
    fmt_options.format_pattern =
        "%(time) [%(thread_id)] %(short_source_location:<20) %(log_level) "
        "%(message)";
    fmt_options.timestamp_pattern = "%Y-%m-%d %H:%M:%S.%Qms";
    fmt_options.timestamp_timezone = quill::Timezone::LocalTime;
    fmt_options.add_metadata_to_multi_line_logs = true;

    logger = quill::Frontend::create_or_get_logger("vulkan", console_sink,
                                                   fmt_options);

    logger->set_log_level(quill::LogLevel::TraceL3);
}

int main() {
    HelloTriangleApplication app;

    initLogger();

    try {
        app.run();
    } catch (const std::exception &e) {
        LOG_ERROR(logger, "{}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
