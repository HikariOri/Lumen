/*
1. 所有的查询基本上都在物理设备上
*/

#include <cstdlib>
#include <fstream>
#include <set>
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

const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// #define DEBUG_USER

quill::Logger *logger = nullptr;

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

    VkPipelineLayout pipelineLayout;

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

    void createGraphicsPipeline() {
        auto vertShaderCode = readFile("./shaders/glsl/vert.spv");
        auto fragShaderCode = readFile("./shaders/glsl/frag.spv");

        // VkShaderModule 只是原始 code 的包装
        // 在创建管线之后就不需要它们了
        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        // 创建 vertex shader 阶段
        VkPipelineShaderStageCreateInfo vertShaderStageInfo {};
        vertShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        // 着色器程序入口函数
        vertShaderStageInfo.pName = "main";

        // 创建 fragment shader 阶段
        VkPipelineShaderStageCreateInfo fragShaderStageInfo {};
        fragShaderStageInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        // 着色器阶段
        VkPipelineShaderStageCreateInfo shaderStages[] = {
            vertShaderStageInfo, fragShaderStageInfo
        };

        /*
    VkPipelineVertexInputStateCreateInfo 结构描述了将要传递给顶点着色器的顶点数据的格式。它大致以两种方式来描述：
        Bindings: 数据之间的间隔以及数据是按顶点还是按实例
        Attribute descriptions: 传递给顶点着色器的属性类型，从哪个绑定加载它们以及它们在哪个偏移量
        */
        // 顶点输入阶段
        // 暂时没有顶点输入
        VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
        vertexInputInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        // pVertexBindingDescriptions 和 pVertexAttributeDescriptions 成员各自指向一个结构体数组，该数组描述了上述用于加载顶点数据的详细信息。
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

        /*
        VkPipelineInputAssemblyStateCreateInfo 结构体描述了两件事：1. 将从顶点绘制何种几何图形（拓扑） 2. 是否启用原始图形重启动。
        前者在 topology 成员中指定，可以具有如下的值：
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST: 
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST: 每 2 个顶点取一条线，不重复使用
            VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: 每条线的末尾顶点被用作下一条线的起始顶点
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: 每 3 个顶点取一个三角形，不重复使用
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: 每个三角形的第二和第三个顶点被用作下一个三角形的第一个和第二个顶点
        通常情况下，顶点会按顺序从顶点缓冲区中通过索引加载，但使用元素缓冲区时，你可以自行指定要使用的索引。
        这允许你执行优化操作，如重复使用顶点。
        如果你将 primitiveRestartEnable 成员设置为 VK_TRUE，
        那么可以通过使用特殊的 0xFFFF 或 0xFFFFFFFF 索引来拆分 _STRIP 顶点拓扑模式中的线和三角形。
        */
        VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // viewport 阶段（使用动态阶段）
        VkPipelineViewportStateCreateInfo viewportState {};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // 光栅化阶段
        VkPipelineRasterizationStateCreateInfo rasterizer {};
        rasterizer.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        // 如果 depthClampEnable 设置为 VK_TRUE ，那么超出近裁剪面和远裁剪面的片段会被裁剪到这些平面上，而不是被丢弃。
        // 在制作阴影贴图可能需要这样
        rasterizer.depthClampEnable = VK_FALSE;
        // 如果 rasterizerDiscardEnable 设置为 VK_TRUE ，那么几何体将不会通过光栅化阶段。这基本上禁用了任何输出到帧缓冲区
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        /*
        polygonMode 确定如何为几何体生成片段。以下模式可用：
            VK_POLYGON_MODE_FILL : 用片段填充多边形区域
            VK_POLYGON_MODE_LINE : 将多边形边绘制为线
            VK_POLYGON_MODE_POINT : 将多边形顶点绘制为点
        */
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        // 光栅化器可以通过添加一个常量值或根据片段的斜率对其进行偏置来改变深度值，这有时用于阴影映射
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0F; // Optional
        rasterizer.depthBiasClamp = 0.0F;          // Optional
        rasterizer.depthBiasSlopeFactor = 0.0F;    // Optional

        // MSAA，先不管
        VkPipelineMultisampleStateCreateInfo multisampling {};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0F;          // Optional
        multisampling.pSampleMask = nullptr;            // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE;      // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment {};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor =
            VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ZERO;                            // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ZERO;                            // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending {};
        colorBlending.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0F; // Optional
        colorBlending.blendConstants[1] = 0.0F; // Optional
        colorBlending.blendConstants[2] = 0.0F; // Optional
        colorBlending.blendConstants[3] = 0.0F; // Optional

        // 可以指定为静态的
        // VkViewport viewport {};
        // viewport.x = 0.0F;
        // viewport.y = 0.0F;
        // viewport.width = static_cast<float>(swapChainExtent.width);
        // viewport.height = static_cast<float>(swapChainExtent.height);
        // viewport.minDepth = 0.0F;
        // viewport.maxDepth = 1.0F;

        // VkRect2D scissor {};
        // scissor.offset = { 0, 0 };
        // scissor.extent = swapChainExtent;

        // 不使用动态状态，视口和裁剪矩形需要在管线中使用 VkPipelineViewportStateCreateInfo 结构体进行设置。
        // 这使得该管线的视口和裁剪矩形不可变。对这些值所需的任何更改都需要创建一个具有新值的新的管线。
        // VkPipelineViewportStateCreateInfo viewportState {};
        // viewportState.sType =
        //     VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        // viewportState.viewportCount = 1;
        // viewportState.pViewports = &viewport;
        // viewportState.scissorCount = 1;
        // viewportState.pScissors = &scissor;

        // 配置管线动态状态
        // 虽然管线状态大部分需要被烘焙，但也有少部分可以在绘制时更改
        // 例如视口的大小、线宽和混合常量
        // 要想使用这些，必须创建一个 VkPipelineDynamicStateCreateInfo 并填充他
        std::vector<VkDynamicState> dynamicStates { VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR };
        // 动态阶段
        VkPipelineDynamicStateCreateInfo dynamicState {};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =
            static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
        pipelineLayoutInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0;            // Optional
        pipelineLayoutInfo.pSetLayouts = nullptr;         // Optional
        pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                   &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    // 着色器代码必须通过 VkShaderModule 包装才能传递给管线
    VkShaderModule createShaderModule(const std::vector<char> &code) {
        VkShaderModuleCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create shader module!" };
        }

        return shaderModule;
    }

    void createSurface() {
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            throw std::runtime_error(
                std::string("failed to create window surface: ") +
                SDL_GetError());
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
        LOG_DEBUG(logger, "GPUs:");
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
        LOG_DEBUG(logger, "{}", physicalDevicesTable.str());
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
            int width = 0, height = 0;
            // 用 SDL3 获取窗口的真实像素宽高（drawable / frame buffer 大小）
            if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
                throw std::runtime_error(
                    std::string("SDL_GetWindowSizeInPixels failed: ") +
                    SDL_GetError());
            }

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
        LOG_DEBUG(logger, "所支持的设备拓展:");
        availableExtensionsTable.add_row({ "Extension Name", "Spec Version" });

        for (auto &extension : availableExtensions) {

            availableExtensionsTable.add_row(
                { extension.extensionName,
                  std::to_string(extension.specVersion) });
        }
        LOG_DEBUG(logger, "{}", availableExtensionsTable.str());
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
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

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

    static std::vector<char> readFile(const std::string &filename) {
        std::ifstream file { filename, std::ios::ate | std::ios::binary };

        if (!file.is_open()) {
            throw std::runtime_error { "failed to open file!" };
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);

        file.close();

        return buffer;
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
