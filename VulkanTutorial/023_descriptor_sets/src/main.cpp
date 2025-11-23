/*
1. 所有的查询基本上都在物理设备上
2. 应该注意的是，在实际应用中，你不应该为每个单独的缓冲区实际调用 vkAllocateMemory 。
   同时进行的内存分配数量受 maxMemoryAllocationCount 物理设备限制，即使是在高端硬件如 NVIDIA GTX 1080 上，这个限制也可能低至 4096 。
   同时为大量对象分配内存的正确方法是，通过我们在许多函数中看到的使用 offset 参数，将单个内存分配分割到多个不同的对象中。(合并)
3. 描述符的使用包括三个部分：
   在创建管线时指定描述符集布局
   从描述符池中分配描述符集
   在渲染期间绑定描述符集
*/

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <tabulate/table.hpp>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

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

// 所有的 queue family 都使用 index 表示
struct QueueFamilyIndices {
    // 支持图像的 queue family 的 index（支持绘制的 queue family）
    // 注意：图形队列和计算队列都可以作为传输队列使用
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

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription {};

        /*
        我们所有的顶点数据都打包在一个数组中，因此我们只需要一个绑定。
        binding 指定绑定在绑定数组中的索引。
        stride 指定从一个条目到下一个条目的字节数， 
        inputRate 参数可以具有以下值之一：
            VK_VERTEX_INPUT_RATE_VERTEX：在每个顶点之后移动到下一个数据条目
            VK_VERTEX_INPUT_RATE_INSTANCE：每个实例后移动到下一个数据条目
        */
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2>
    getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2>
            attributeDescriptions {};

        /*
        binding 参数告诉 Vulkan 每个顶点的数据来自哪个绑定。 
        location 参数引用顶点着色器中输入的 location 指令。
        顶点着色器中位置为 0 输入是位置信息，它包含两个 32 位浮点分量。
        format 参数描述属性的数据类型。需要注意的是，格式的指定使用了与颜色格式相同的枚举。以下着色器类型和格式通常一起使用：
        float: VK_FORMAT_R32_SFLOAT
        vec2: VK_FORMAT_R32G32_SFLOAT
        vec3: VK_FORMAT_R32G32B32_SFLOAT
        vec4: VK_FORMAT_R32G32B32A32_SFLOAT
        */
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

const std::vector<Vertex> vertices = {
    { .pos = { -0.5F, -0.5F }, .color = { 1.0F, 0.0F, 0.0F } },
    { .pos = { 0.5F, -0.5F }, .color = { 0.0F, 1.0F, 0.0F } },
    { .pos = { 0.5F, 0.5F }, .color = { 0.0F, 0.0F, 1.0F } },
    { .pos { -0.5F, 0.5F }, .color = { 1.0F, 1.0F, 1.0F } }
};

const std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };

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

    VkRenderPass renderPass;

    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    std::vector<VkFramebuffer> swapChainFramebuffers;
    // 命令池管理用于存储缓冲区的内存，命令缓冲区从中分配。
    VkCommandPool commandPool;
    // commandBuffer 会在其所在的 commandPool 销毁时自动释放
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    /*
    尽管许多驱动程序和平台在窗口调整大小后会自动触发 VK_ERROR_OUT_OF_DATE_KHR ，
    但这并不是保证的。这就是为什么我们将添加一些额外代码来显式处理调整大小。
    添加一个新的成员变量，用于标记是否发生了调整大小
    */
    bool framebufferResized = false;

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void *> uniformBuffersMapped;

    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;

private:
    void initWindow() {
        // 初始化 glfw
        glfwInit();

        // 设置不适用 opengl api
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        // 先不管 resize
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    static void framebufferResizeCallback(GLFWwindow *window, int width,
                                          int height) {
        auto app = reinterpret_cast<HelloTriangleApplication *>(
            glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void initVulkan() {
        createInstance();
        setupDebugMessenger(); // 这一步非必须
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
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

    void createRenderPass() {
        // 颜色附件描述
        VkAttachmentDescription colorAttachment {};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        /*
        loadOp 和 storeOp 决定在渲染之前和渲染之后如何处理附件中的数据。
        对于 loadOp 我们有以下选择：
            VK_ATTACHMENT_LOAD_OP_LOAD: 保留附件的现有内容
            VK_ATTACHMENT_LOAD_OP_CLEAR: 在开始时将值清空为常量
            VK_ATTACHMENT_LOAD_OP_DONT_CARE: 现有内容是未定义的；我们不在乎它们
        对于 storeOp 只有两个可能性：
            VK_ATTACHMENT_STORE_OP_STORE: 渲染内容将被存储在内存中，稍后可以读取
            VK_ATTACHMENT_STORE_OP_DONT_CARE: 渲染操作后，帧缓冲区的内容将未定义
        */
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        /*
        一些最常见的布局包括：
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL：用作颜色附件的图像
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR：要在交换链中展示的图像
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL：用作内存复制操作目标的图像
         */
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        // 引用 attachments 数组中第 0 个 (colorAttachment)
        colorAttachmentRef.attachment = 0;
        // 子通道中使用 color 附件时的布局
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass {};
        // 这是一个图形子通道 (graphics)
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // 子通道有 1 个颜色附件
        subpass.colorAttachmentCount = 1;
        // 指向 colorAttachmentRef
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency {};
        // 特殊值 VK_SUBPASS_EXTERNAL 指的是在渲染阶段之前或之后的隐式子渲染阶段，具体取决于它是否在 srcSubpass 或 dstSubpass 中指定。
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        // 索引 0 指的是我们的子渲染阶段，它是第一个且唯一的。
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create render pass!" };
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
        VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
        vertexInputInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        // pVertexBindingDescriptions 和 pVertexAttributeDescriptions 成员各自指向一个结构体数组，该数组描述了上述用于加载顶点数据的详细信息。
        // 这里只是告诉程序如何解析顶点数据，但实际的数据还未传递到 GPU
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.pVertexAttributeDescriptions =
            attributeDescriptions.data();

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
        rasterizer.lineWidth = 1.0F;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
        // 绑定标识符集
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;    // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                   &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;

        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr; // Optional
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;

        pipelineInfo.layout = pipelineLayout;

        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1;              // Optional

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                      nullptr,
                                      &graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error { "failed to create graphics pipeline!" };
        }

        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboLayoutBinding {};
        // location = 0
        uboLayoutBinding.binding = 0;
        // UBO
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // 有几个
        uboLayoutBinding.descriptorCount = 1;
        // 在哪个阶段中应用
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

        VkDescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                        &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error {
                "failed to create descriptor set layout"
            };
        }
    }

    void createFramebuffers() {
        // 为 swapChainFramebuffers 分配与 swapChainImageViews 同样数量的元素
        // 每一个 swapchain image 都对应一个 framebuffer
        swapChainFramebuffers.resize(swapChainImageViews.size());

        // 遍历所有 swap chain 的 image view，为每一个创建一个 framebuffer
        for (size_t i = 0; i < swapChainImageViews.size(); ++i) {
            // 定义 framebuffer 的 attachments，这里只有一个 attachment，就是当前的 swapChainImageView
            VkImageView attachments[] = { swapChainImageViews[i] };

            // 准备 VkFramebufferCreateInfo 结构，描述 framebuffer 的参数
            VkFramebufferCreateInfo framebufferInfo {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            // renderPass 是之前创建好的 render pass，framebuffer 必须和这个 render pass 兼容
            framebufferInfo.renderPass = renderPass;
            // 附件数量，这里是 1（只有 color attachment）
            framebufferInfo.attachmentCount = 1;
            // 附件数组指针
            framebufferInfo.pAttachments = attachments;
            // framebuffer 的宽度、高度，设置为 swap chain 的 extent（分辨率）
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            // 图像层数 (layers)，这里是单层 (1)，因为 swapchain image 通常是单层 2D 图像
            framebufferInfo.layers = 1;

            // 调用 Vulkan API 创建 framebuffer
            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                    &swapChainFramebuffers[i]) != VK_SUCCESS) {
                // 如果创建失败，就抛出异常
                throw std::runtime_error { "failed to create framebuffer!" };
            }
        }
    }

    void createCommandPool() {
        QueueFamilyIndices queueFamilyIndices =
            findQueueFamilies(physicalDevice);

        VkCommandPoolCreateInfo poolInfo {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        /*
        命令池有两种可能的标志：
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT：提示命令缓冲区会频繁地用新命令重新记录（可能会改变内存分配行为），用作传输时使用。
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许单独重新记录命令缓冲区；如果没有此标志，则所有命令缓冲区必须一起重置。
        */
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create command pool!" };
        }
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        /*
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT：缓冲区可以用作内存传输操作中的源。
        VK_BUFFER_USAGE_TRANSFER_DST_BIT：缓冲区可以用作内存传输操作中的目标。
        */
        /*
        主机不可见的内存不可映射
        */

        // 暂存缓冲区，仅主机可见
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingBufferMemory);

        // 内存映射
        /*
        现在您可以直接将顶点数据 memcpy 映射到内存中，并使用 vkUnmapMemory 再次取消映射。
        不幸的是，驱动程序可能不会立即将数据复制到缓冲区内存中，例如由于缓存的原因。还可能存在写入缓冲区的内容在映射内存中尚未可见的情况。处理这个问题有两种方法：
            使用主机一致的内存堆，用 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 标记
            在写入映射内存后调用 vkFlushMappedMemoryRanges ，在从映射内存读取前调用 vkInvalidateMappedMemoryRanges
        */
        void *data {};
        // 将数据写入到仅主机可见的暂存缓冲区内存
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer,
                     vertexBufferMemory);

        copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }

    void createIndexBuffer() {
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingBufferMemory);

        void *data;
        vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(device, stagingBufferMemory);

        createBuffer(bufferSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer,
                     indexBufferMemory);

        copyBuffer(stagingBuffer, indexBuffer, bufferSize);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
    }

    void createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         uniformBuffers[i], uniformBuffersMemory[i]);

            vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0,
                        &uniformBuffersMapped[i]);
        }
    }

    void createDescriptorPool() {
        // 定义一个描述符池大小（每种类型的描述符数量）
        VkDescriptorPoolSize poolSize {};
        // 这里我们只申请 Uniform Buffer 类型的描述符
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // descriptorCount 表示这个池里为这种类型（UBO）准备多少个描述符
        // 这里设为 MAX_FRAMES_IN_FLIGHT，通常是每帧一个 Uniform Buffer
        poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        // 准备 Descriptor Pool 创建信息结构体
        VkDescriptorPoolCreateInfo poolInfo {};
        poolInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; // 结构体类型
        poolInfo.poolSizeCount = 1; // 我们只传入一种类型的 VkDescriptorPoolSize
        poolInfo.pPoolSizes =
            &poolSize; // 指向 poolSize 数组（这里只是一个元素）

        // maxSets 是这个池里最多可以分配多少个 descriptor set
        // 设为 MAX_FRAMES_IN_FLIGHT，表明我们打算每帧都分配一个 descriptor set
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        // 创建 descriptor pool
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr,
                                   &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error { "failed to create descriptor pool" };
        }
    }

    void createDescriptorSets() {
        // 为每帧准备一个 descriptor set layout，所有帧都使用相同的布局
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                   descriptorSetLayout);
        // 设置分配描述符集的信息
        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool =
            descriptorPool; // 指定从哪个 descriptor 池里分配
        allocInfo.descriptorSetCount = static_cast<uint32_t>(
            MAX_FRAMES_IN_FLIGHT);              // 要分配多少个 descriptor set
        allocInfo.pSetLayouts = layouts.data(); // 每个 set 对应哪个 layout

        // 为 descriptorSets 数组分配空间
        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        // 从池里面分配 descriptor set
        if (vkAllocateDescriptorSets(device, &allocInfo,
                                     descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error { "failed to allocate descriptor sets" };
        }

        // 更新每一个 descriptor set，使其绑定对应帧的 uniform buffer
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            // 描述符所指向缓冲区的信息
            VkDescriptorBufferInfo bufferInfo {};
            bufferInfo.buffer =
                uniformBuffers[i]; // 第 i 帧对应的 uniform buffer
            bufferInfo.offset = 0; // 从 buffer 的哪个偏移开始
            bufferInfo.range =
                sizeof(UniformBufferObject); // buffer 的大小／范围

            // 写描述符集 (Descriptor Set) 的结构体
            VkWriteDescriptorSet descriptorWrite {};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet =
                descriptorSets[i]; // 要更新的 descriptor set
            descriptorWrite.dstBinding =
                0; // 对应 layout 中哪个 binding（假设 uniform buffer 在 binding 0）
            descriptorWrite.dstArrayElement =
                0; // 如果 binding 是数组，这里是数组起始索引。我们不是数组，用 0。

            descriptorWrite.descriptorType =
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // 描述符类型是统一缓冲区
            descriptorWrite.descriptorCount =
                1; // 更新多少个描述符 — 这里是一个

            descriptorWrite.pBufferInfo = &bufferInfo; // 传入 bufferInfo
            descriptorWrite.pImageInfo =
                nullptr; // 非采样/非图像绑定，所以为 null
            descriptorWrite.pTexelBufferView = nullptr; // 非 texel buffer view

            // 执行实际更新，将 buffer 信息写入 descriptor set
            vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
        }
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        // 对于这种短命的 commandBuffer 最好为他们单独分配一个 commandPool
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        // 只用一次的 command，并等待函数返回
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        // 复制 buffer
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer &buffer,
                      VkDeviceMemory &bufferMemory) {
        VkBufferCreateInfo bufferInfo {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        // 与交换链中的图像类似，缓冲区也可以由特定的队列族拥有，或者同时在多个队列族之间共享。
        // 缓冲区只会从图形队列中使用，因此我们可以坚持独占访问。
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create buffer!");
        }

        // 为缓冲区分配内存

        // 查询内存需求
        /*
        VkMemoryRequirements 结构体有三个字段：
            size：所需内存的大小（以字节为单位），可能与 bufferInfo.size 不同
            alignment：缓冲区在已分配内存区域中开始的字节偏移量，取决于 bufferInfo.usage 和 bufferInfo.flags 。
            memoryTypeBits：适用于缓冲区的内存类型的位域。
        */
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to allocate buffer memory!");
        }

        // 关联内存和缓冲区
        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    /// 显卡可以提供不同类型的内存供用户分配。
    /// 每种类型的内存允许的操作和性能特征各不相同。
    /// 我们需要结合缓冲区的要求和应用程序自身的需求，找到合适的内存类型。
    uint32_t findMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) {

        // 查询有关可用内存类型的信息
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) ==
                    properties) {
                return i;
            }
        }

        throw std::runtime_error { "failed to find suitable memory type!" };
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        /*
        level 参数指定分配的命令缓冲区是主命令缓冲区还是辅助命令缓冲区。
            VK_COMMAND_BUFFER_LEVEL_PRIMARY: 可以提交到队列中执行，但不能从其他命令缓冲区调用。
            VK_COMMAND_BUFFER_LEVEL_SECONDARY：不能直接提交，但可以从主命令缓冲区调用。
        */
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount =
            static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo,
                                     commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error { "failed to allocate command buffers!" };
        }
    }

    void createSyncObjects() {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        // 当前版本创建同步对象很简单
        VkSemaphoreCreateInfo semaphoreInfo {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // 设置为已发出信号的状态，这样防止保证第一帧还为渲染时无限期等待 fence
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                  &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                  &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, nullptr,
                              &inFlightFences[i]) != VK_SUCCESS) {

                throw std::runtime_error(
                    "failed to create synchronization objects for a frame!");
            }
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer,
                             uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        /*
        flags 参数指定我们如何使用命令缓冲区。以下是可用的值：
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: 命令缓冲区将在执行一次后立即重新记录。
            VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT: 这是一个辅助命令缓冲区，它将完全包含在单个渲染过程中。
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT：即使命令缓冲区已处于待执行状态，也可以重新提交该命令缓冲区。
        */
        beginInfo.flags = 0; // Optional
        // pInheritanceInfo 参数仅与辅助命令缓冲区相关。它指定要从调用的主命令缓冲区继承哪个状态。
        beginInfo.pInheritanceInfo = nullptr; // Optional

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to begin recording command buffer!");
        }

        VkRenderPassBeginInfo renderPassInfo {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = swapChainExtent;

        // The last two parameters define the clear values to use for VK_ATTACHMENT_LOAD_OP_CLEAR,
        // which we used as load operation for the color attachment.
        VkClearValue clearColor = { { { 0.0F, 0.0F, 0.0F, 1.0F } } };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        /*
        每个命令的第一个参数始终是用于记录命令的命令缓冲区。第二个参数指定我们刚刚提供的渲染通道的详细信息。最后一个参数控制渲染通道中绘图命令的提供方式。它可以取以下两个值之一：
            VK_SUBPASS_CONTENTS_INLINE：渲染通道命令将嵌入到主命令缓冲区本身中，不会执行任何辅助命令缓冲区。
            VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: 渲染通道命令将从辅助命令缓冲区执行。
        */

        // 接下来开始 draw

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphicsPipeline);

        VkBuffer vertexBuffers[] = { vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT16);

        // 我们在设置管线的时候将 viewport 阶段和 scissor 阶段设置为了动态的
        // 所以需要在发出绘制命令之前在命令缓冲区中设置它们
        VkViewport viewport {};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(swapChainExtent.width);
        viewport.height = static_cast<float>(swapChainExtent.height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { .x = 0, .y = 0 };
        scissor.extent = swapChainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // 与顶点和索引缓冲区不同，描述符集并非特定于图形管线。因此，如果我们想将描述符集绑定到图形或计算管线，需要指定。
        // 下一个参数是基于描述符的布局。
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);

        // draw call
        /*
        vkCmdDraw 函数本身可能有点平淡无奇，但它之所以如此简单，是因为我们预先指定了所有信息。除了命令缓冲区之外，它还有以下参数：
            vertexCount：即使我们没有顶点缓冲区，但从技术上讲，我们仍然有 3 个顶点要绘制。
            instanceCount：用于实例化渲染，如果不进行实例化渲染，则使用 1 。
            firstVertex：用作顶点缓冲区中的偏移量，定义 gl_VertexIndex 的最小值。
            firstInstance：用作实例化渲染的偏移量，定义 gl_InstanceIndex 的最小值。
        */
        // vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0,
        //           0);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()),
                         1, 0, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }
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
        LOG_DEBUG(logger, "{}", "GPUs:");
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

    void cleanupSwapChain() {
        for (auto framebuffer : swapChainFramebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device, imageView, nullptr);
        }

        vkDestroySwapchainKHR(device, swapChain, nullptr);
    }

    void recreateSwapChain() {
        // 最小化一直等待
        int width {}, height {};
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createFramebuffers();
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
        LOG_DEBUG(logger, "{}", "所支持的设备拓展:");
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
        LOG_DEBUG(logger, "{}", "当前设备支持的 queue family:");
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
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            drawFrame();
        }

        vkDeviceWaitIdle(device);
    }

    void drawFrame() {
        /*
    从总体上看，Vulkan 渲染帧包含一系列常见的步骤：
        等待上一帧结束
        从交换链中获取图像
        记录一个命令缓冲区，该缓冲区会将场景绘制到该图像上。
        提交已记录的命令缓冲区
        展示交换链图像
        */

        /*
        有一些事件需要我们明确排序，因为它们发生在 GPU 上，例如：
            从交换链中获取图像
            在已采集图像上执行绘制图形的命令
            将该图像呈现在屏幕上进行展示，然后将其返回到交换链。
        这些事件均由单个函数调用触发，但都是异步执行的。
        函数调用会在操作实际完成之前返回，且执行顺序也未定义。
        这很不利，因为每个操作都依赖于前一个操作的完成。因此，我们需要探索可以使用哪些基本操作来实现所需的执行顺序。
        */

        // Semaphores 可以让两个 GPU 操作等待
        // Fences 可以让 CPU 等待 GPU
        /*
        In summary, semaphores are used to specify the execution order of operations on the GPU while fences are used to keep the CPU and GPU in sync with each-other.
        */

        // 等待上一帧完成
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                        UINT64_MAX);

        // 从交换链中获取图像
        uint32_t imageIndex {};
        VkResult result =
            vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
                                  imageAvailableSemaphores[currentFrame],
                                  VK_NULL_HANDLE, &imageIndex);

        /*
        现在我们只需要确定何时需要重新创建交换链，并调用我们的新 recreateSwapChain 函数。
        幸运的是，Vulkan 通常会在呈现时告诉我们交换链不再适用。 
        vkAcquireNextImageKHR 和 vkQueuePresentKHR 函数可以返回以下特殊值来指示这一点。
            VK_ERROR_OUT_OF_DATE_KHR: 交换链与表面不再兼容，无法用于渲染。通常在窗口大小调整后发生。
            VK_SUBOPTIMAL_KHR: 交换链仍然可以成功地向表面呈现，但表面属性不再完全匹配。
        */
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error { "failed to acquire swap chain image!" };
        }

        updateUniformBuffer(currentFrame);

        // fence 必须手动 reset
        // 确实能 submit 才 reset
        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        // reset 一下 command 确保它可以被记录
        vkResetCommandBuffer(commandBuffers[currentFrame],
                             /*VkCommandBufferResetFlagBits*/ 0);
        // 记录命令
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        // 提交命令缓冲区
        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {
            imageAvailableSemaphores[currentFrame]
        };
        VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        // 这里会消费信号量，将信号量恢复到 unsignaled 状态
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

        VkSemaphore signalSemaphores[] = {
            renderFinishedSemaphores[currentFrame]
        };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        // 提交命令到队列
        // 此处会 reset imageAvailableSemaphore and signal renderFinishedSemaphore
        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                          inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        // present
        VkPresentInfoKHR presentInfo {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;

        presentInfo.pImageIndices = &imageIndex;

        presentInfo.pResults = nullptr; // Optional

        // 提交向交换链呈现镜像的请求
        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(
                         currentTime - startTime)
                         .count();

        UniformBufferObject ubo {};
        ubo.model = glm::rotate(glm::mat4(1.0F), time * glm::radians(90.0F),
                                glm::vec3(0.0F, 0.0F, 1.0F));
        ubo.view = glm::lookAt(glm::vec3(2.0F, 2.0F, 2.0F),
                               glm::vec3(0.0F, 0.0F, 0.0F),
                               glm::vec3(0.0F, 0.0F, 1.0F));
        ubo.proj = glm::perspective(
            glm::radians(45.0F),
            swapChainExtent.width / (float)swapChainExtent.height, 0.1F, 10.0F);
        // Vulkan 和 OpenGL DNC 的 y 轴相反
        ubo.proj[1][1] *= -1;

        memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
    }

    void cleanup() {
        cleanupSwapChain();

        vkDestroyPipeline(device, graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        }

        vkDestroyDescriptorPool(device, descriptorPool, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, indexBuffer, nullptr);
        vkFreeMemory(device, indexBufferMemory, nullptr);

        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexBufferMemory, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }

        vkDestroyCommandPool(device, commandPool, nullptr);

        vkDestroyDevice(device, nullptr);

        if (enableValidationLayers) {
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }

        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);

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
        LOG_DEBUG(logger, "{}", "需要的扩展（glfw + 校验层）:");
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
        LOG_DEBUG(logger, "{}", "支持的拓展:");
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
        LOG_DEBUG(logger, "{}", "支持的层:");
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

    initLogger();

    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception &e) {
        LOG_ERROR(logger, "{}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
