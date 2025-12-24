#include <cstdlib>
#include <stdexcept>

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

        // 设置要使用的扩展
        createInfo.enabledExtensionCount = sdlExtensionCount;
        createInfo.ppEnabledExtensionNames = sdlExtensions;

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

#ifdef DEBUG_USER
        tabulate::Table availableExtensionsTable;
        LOG_DEBUG(logger, "支持的拓展:");
        availableExtensionsTable.add_row({ "Name", "Verison" });
        for (const auto &extension : extensions) {
            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        LOG_DEBUG(logger, "{}", availableExtensionsTable.str());
#endif

        // create 并 check 有没有创建成功
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error { "failed to create instance!" };
        }
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
        // 销毁 VkInstnace
        vkDestroyInstance(instance, nullptr);

        SDL_DestroyWindow(window);
        SDL_Quit();
    }

private:
    SDL_Window *window;

    VkInstance instance;
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
