/*
1. 所有的查询基本上都在物理设备上
2. 应该注意的是，在实际应用中，你不应该为每个单独的缓冲区实际调用 vkAllocateMemory 。
   同时进行的内存分配数量受 maxMemoryAllocationCount 物理设备限制，即使是在高端硬件如 NVIDIA GTX 1080 上，这个限制也可能低至 4096 。
   同时为大量对象分配内存的正确方法是，通过我们在许多函数中看到的使用 offset 参数，将单个内存分配分割到多个不同的对象中。(合并)
3. 描述符的使用包括三个部分：
    在创建管线时指定描述符集布局
    从描述符池中分配描述符集
    在渲染期间绑定描述符集
4. 将纹理添加到我们的应用程序将涉及以下步骤：
    创建一个基于设备内存的图像对象
    用图像文件中的像素填充它
    创建一个图像采样器
    向纹理中添加一个组合图像采样器描述符以采样颜色
5. 然而，在处理图像时，我们还需要注意一些额外的事项。
   图像可以有不同的布局，这些布局会影响像素在内存中的组织方式。
   由于图形硬件的工作方式，仅仅按行存储像素可能无法获得最佳性能，例如。
   在进行任何图像操作时，你必须确保它们具有该操作的最佳布局。实际上，我们在指定渲染通道时已经见过一些这样的布局：
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: 适合演示
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: 作为从片元着色器写入颜色的最佳附件
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: 作为传输操作的最佳源，例如 vkCmdCopyImageToBuffer
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: 作为传输操作的最佳目标，例如 vkCmdCopyBufferToImage
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: 用于从着色器采样最佳
6. 最常见的一种转换图像布局的方式是管线屏障。
   管线屏障主要用于同步资源访问，比如确保图像在读取之前已被写入，但它们也可以用来转换布局。
   在本章中我们将看到如何使用管线屏障实现这一目的。
   当使用 VK_SHARING_MODE_EXCLUSIVE 时，屏障还可以用来转移队列家族所有权。
*/

#include <fstream>
#include <set>
#include <stdexcept>

// OpenGL depth [-1, 1]
// Vulkan depth [0, 1]
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>

#include <tabulate/table.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/ConsoleSink.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

constexpr const char *const ICON_PATH = "./assets/icons/哈士奇.png";

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const std::string MODEL_PATH = "assets/meshes/viking_room/viking_room.obj";
const std::string TEXTURE_PATH = "assets/meshes/viking_room/viking_room.png";

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
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

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

    static std::array<VkVertexInputAttributeDescription, 3>
    getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3>
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
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }

    bool operator==(const Vertex &other) const {
        return pos == other.pos && color == other.color &&
               texCoord == other.texCoord;
    }
};

namespace std {
    template <>
    struct hash<Vertex> {
        size_t operator()(Vertex const &vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                     (hash<glm::vec3>()(vertex.color) << 1)) >>
                    1) ^
                   (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
} // namespace std

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

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

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void *> uniformBuffersMapped;

    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;

    uint32_t mipLevels;
    VkImage textureImage;
    VkImageView textureImageView;
    VkDeviceMemory textureImageMemory;
    VkSampler textureSampler;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;

    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;

private:
    void initWindow() {

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(
                std::string("Couldn't initialize SDL: {} ") + SDL_GetError());
        }

        window = SDL_CreateWindow("Vulkan ", WIDTH, HEIGHT,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

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
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createColorResources();
        createDepthResources();
        createFramebuffers();
        createTextureImage();
        createTextureImageView();
        createTextureSampler();
        loadModel();
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
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] =
                createImageView(swapChainImages[i], swapChainImageFormat,
                                VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void createRenderPass() {
        // 颜色附件描述
        VkAttachmentDescription colorAttachment {};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = msaaSamples;
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
        // 你会注意到我们将 finalLayout 从 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 更改为 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 。
        // 这是因为多重采样图像不能直接呈现。我们需要先将它们解析为普通图像。
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment {};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = msaaSamples;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 因此我们需要为颜色添加一个新的附件，解析附件
        VkAttachmentDescription colorAttachmentResolve {};
        colorAttachmentResolve.format = swapChainImageFormat;
        colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.stencilStoreOp =
            VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        // 引用 attachments 数组中第 0 个 (colorAttachment)
        colorAttachmentRef.attachment = 0;
        // 子通道中使用 color 附件时的布局
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentResolveRef {};
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass {};
        // 这是一个图形子通道 (graphics)
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        // 子通道有 1 个颜色附件
        subpass.colorAttachmentCount = 1;
        // 指向 colorAttachmentRef
        subpass.pColorAttachments = &colorAttachmentRef;
        // 指向 depthAttachmentRef
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        subpass.pResolveAttachments = &colorAttachmentResolveRef;

        VkSubpassDependency dependency {};
        // 特殊值 VK_SUBPASS_EXTERNAL 指的是在渲染阶段之前或之后的隐式子渲染阶段，具体取决于它是否在 srcSubpass 或 dstSubpass 中指定。
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 3> attachments = {
            colorAttachment, depthAttachment, colorAttachmentResolve
        };
        VkRenderPassCreateInfo renderPassInfo {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount =
            static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
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

        VkPipelineMultisampleStateCreateInfo multisampling {};
        multisampling.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = msaaSamples;
        multisampling.minSampleShading = 1.0F;          // Optional
        multisampling.pSampleMask = nullptr;            // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE;      // Optional

        VkPipelineDepthStencilStateCreateInfo depthStencil {};
        depthStencil.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0F; // Optional
        depthStencil.maxDepthBounds = 1.0F; // Optional
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {}; // Optional
        depthStencil.back = {};  // Optional

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
        pipelineInfo.pDepthStencilState = &depthStencil;
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

        VkDescriptorSetLayoutBinding samplerLayoutBinding {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
            uboLayoutBinding, samplerLayoutBinding
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                        &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error {
                "failed to create descriptor set layout"
            };
        }
    }

    void createFramebuffers() {
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::array<VkImageView, 3> attachments = { colorImageView,
                                                       depthImageView,
                                                       swapChainImageViews[i] };

            VkFramebufferCreateInfo framebufferInfo {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount =
                static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                                    &swapChainFramebuffers[i]) != VK_SUCCESS) {
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

    bool hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void createDepthResources() {
        /*
        与纹理图像不同，我们不一定需要特定的格式，因为我们不会直接从程序中访问纹理元素。它只需要具有合理的精度，在现实应用中至少 24 位是常见的。有几个格式符合这一要求：
            VK_FORMAT_D32_SFLOAT: 32 位浮点数用于深度
            VK_FORMAT_D32_SFLOAT_S8_UINT: 32 位有符号浮点数用于深度和 8 位模板组件
            VK_FORMAT_D24_UNORM_S8_UINT: 24 位浮点数用于深度和 8 位模板组件
        */
        // 我们不需要显式地将图像布局转换为深度附件，因为我们会负责在渲染通道中处理这个问题。
        VkFormat depthFormat = findDepthFormat();

        createImage(swapChainExtent.width, swapChainExtent.height, 1,
                    msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage,
                    depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat,
                                         VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    }

    VkFormat findDepthFormat() {
        return findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
              VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                                 VkImageTiling tiling,
                                 VkFormatFeatureFlags features) {
        for (VkFormat format : candidates) {
            /*
        VkFormatProperties 结构体包含三个字段：
            linearTilingFeatures: 支持线性瓦片的使用场景
            optimalTilingFeatures: 支持最佳瓦片化用例
            bufferFeatures: 支持的缓冲区用例
        */
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR &&
                (props.linearTilingFeatures & features) == features) {
                return format;
            } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                       (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }

    void createColorResources() {
        VkFormat colorFormat = swapChainImageFormat;

        createImage(swapChainExtent.width, swapChainExtent.height, 1,
                    msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage,
                    colorImageMemory);
        // 由于 Vulkan 规范在每像素使用多个样本的图像情况下强制要求使用单一 mip 级别，因此我们只使用一个 mip 级别。此外，这个颜色缓冲区不需要 mipmaps，因为它不会用作纹理
        colorImageView = createImageView(colorImage, colorFormat,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }

    void createTextureImage() {
        int texWidth {}, texHeight {}, texChannels {};
        stbi_uc *pixels = stbi_load(TEXTURE_PATH.c_str(), &texWidth, &texHeight,
                                    &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        mipLevels = static_cast<uint32_t>(
                        std::floor(std::log2(std::max(texWidth, texHeight)))) +
                    1;

        if (!pixels) {
            throw std::runtime_error { "failed to load texture image!" };
        }

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingBufferMemory);

        void *data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        stbi_image_free(pixels);

        createImage(texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT,
                    VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage,
                    textureImageMemory);

        /*
       下一步是将 staging buffer 复制到纹理图像。这涉及两个步骤：
            将纹理图像转换到 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            执行缓冲区到图像的复制操作
        */
        transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);
        copyBufferToImage(stagingBuffer, textureImage,
                          static_cast<uint32_t>(texWidth),
                          static_cast<uint32_t>(texHeight));
        // transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
        //                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        //                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //                       mipLevels);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth,
                        texHeight, mipLevels);
    }

    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth,
                         int32_t texHeight, uint32_t mipLevels) {
        // 检查物理设备是否支持给定格式的线性 blitting（线性过滤）
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat,
                                            &formatProperties);

        // 如果不支持线性采样 (过滤)，则不能用 vkCmdBlitImage 来生成 mipmaps
        if (!(formatProperties.optimalTilingFeatures &
              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
            throw std::runtime_error(
                "texture image format does not support linear blitting!");
        }

        // 开始一个一次性 (single-time) 命令缓冲区，用于提交 blit 和 barrier 操作
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        // 创建一个 image memory barrier，用于 mipmap 级别布局转换
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        // 我们只处理颜色 (color) 方面
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        // 每次 barrier 只针对一个 mip 级别 (levelCount = 1)
        barrier.subresourceRange.levelCount = 1;

        // 初始化当前 mip 层的宽高为原始纹理大小
        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        // 从 i = 1 开始，因为 mip‐level 0 已经由 staging buffer 填充
        for (uint32_t i = 1; i < mipLevels; i++) {
            // 为上一个 mip 级别设置 barrier，转换布局：DST → SRC
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            // 上一个级别刚刚被写入 (TRANSFER_WRITE)
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            // 将作为 blit 源端 (TRANSFER_READ)
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            // 插入管线屏障 (pipeline barrier)，确保写入结束，可以读取
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, // 写入发生在 transfer 阶段
                VK_PIPELINE_STAGE_TRANSFER_BIT, // 读取也在 transfer 阶段
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            // 配置 blit 区域 (region)，从前一 mip 级别 (i-1) blit 到当前 (i)
            VkImageBlit blit {};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;

            blit.dstOffsets[0] = { 0, 0, 0 };
            // 下一 mip 尺寸是当前 / 2（但保证至少为 1）
            blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1,
                                   mipHeight > 1 ? mipHeight / 2 : 1, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            // 执行 blit，从 src mip 级别到 dst mip 级别，并使用线性过滤
            vkCmdBlitImage(commandBuffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                           VK_FILTER_LINEAR);

            // blit 之后，把前一个 mip 级别 (i-1) 转换到 shader 可读布局 (只读采样)
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,        // blit 完成后
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // shader 读取
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            // 缩小 mip 的宽和高，为下一循环做准备
            if (mipWidth > 1) {
                mipWidth /= 2;
            }
            if (mipHeight > 1) {
                mipHeight /= 2;
            }
        }

        // 最后一个 mip 级别 (mipLevels - 1) 还在 TRANSFER_DST，我们需要把它也转换为 shader 只读
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,        // 写入结束
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // 接下来 shader 读取
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 提交并结束一次性命令缓冲区
        endSingleTimeCommands(commandBuffer);
    }

    void createTextureImageView() {
        textureImageView =
            createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    }

    void createTextureSampler() {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        VkSamplerCreateInfo samplerInfo {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;

        /*
        地址模式可以通过 addressMode 字段按轴指定。可用值如下所示。其中大部分在上面的图像中进行了演示。请注意，轴被称为 U、V 和 W，而不是 X、Y 和 Z。这是纹理空间坐标的约定。
        VK_SAMPLER_ADDRESS_MODE_REPEAT: 当超出图像维度时重复纹理。
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: 类似于重复，但在超出维度时反转坐标以镜像图像。
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: 取超出图像维度时最接近该坐标的边缘颜色。
        VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE: 类似于边缘钳位，但使用与最近边缘相反的边缘。
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: 当采样超出图像尺寸时返回纯色。
        */
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        // 各向异性过滤，如果不是性能瓶颈，没有理由不开启
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        // borderColor 字段指定在使用边界 clamp 地址模式采样图像外部时返回哪种颜色。可以返回黑色、白色或透明，并且可以以浮点或整数格式返回。你不能指定任意颜色。
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        // unnormalizedCoordinates 字段指定你想要使用哪种坐标系来寻址图像中的纹理单元。
        // 如果这个字段是 VK_TRUE ，那么你可以直接使用 [0, texWidth) 和 [0, texHeight) 范围内的坐标。
        // 如果它是 VK_FALSE ，那么所有轴上都使用 [0, 1) 范围内的坐标来寻址纹理单元。
        // 实际应用几乎总是使用归一化坐标，因为这样可以使用不同分辨率的纹理并使用完全相同的坐标。
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        // 如果启用了比较函数，则首先会将纹理单元与一个值进行比较，该比较的结果将用于过滤操作。这主要用于阴影贴图的百分比更近过滤（PCSS）。
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        // mipmapping
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0F; // Optional
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.mipLodBias = 0.0F; //

        if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) !=
            VK_SUCCESS) {
            throw std::runtime_error { "failed to create texture sampler!" };
        }
    }

    VkImageView createImageView(VkImage image, VkFormat format,
                                VkImageAspectFlags aspectFlags,
                                uint32_t mipLevels) {
        VkImageViewCreateInfo viewInfo {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        // 指定将 image 视为 1D 纹理、2D 纹理、3D 纹理还是立方体贴图
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        // 指定格式
        viewInfo.format = format;

        // subresourceRange 字段描述了图像的用途已经访问该图像的哪一部分
        // 颜色附件
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create image view!");
        }

        return imageView;
    }

    VkCommandBuffer beginSingleTimeCommands() {
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

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;
        // 复制 buffer
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    void loadModel() {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn;
        std::string err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                              MODEL_PATH.c_str())) {
            throw std::runtime_error(err);
        }
        LOG_WARNING(logger, "load model: {}", warn);

        std::unordered_map<Vertex, uint32_t> uniqueVertices {};

        for (const auto &shape : shapes) {
            for (const auto &index : shape.mesh.indices) {
                Vertex vertex {};

                vertex.pos = { attrib.vertices[3 * index.vertex_index + 0],
                               attrib.vertices[3 * index.vertex_index + 1],
                               attrib.vertices[3 * index.vertex_index + 2] };

                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };

                vertex.color = { 1.0f, 1.0f, 1.0f };

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] =
                        static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
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
        // 描述符池大小（每种类型的描述符，总共要为多少个描述符预留空间）
        std::array<VkDescriptorPoolSize, 2> poolSizes;

        // 我们第一种描述符类型：Uniform Buffer（比如 UBO）
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        // descriptorCount 表示这个池里为这种类型准备多少个“描述符”。
        // 通常我们希望每帧（每个 in-flight）都有一个 Uniform Buffer 的描述符，所以设置为 MAX_FRAMES_IN_FLIGHT。
        poolSizes[0].descriptorCount =
            static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        // 第二种类型：Combined Image Sampler（结合了图像 + 采样器）
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // 同样为每帧一个这样的描述符
        poolSizes[1].descriptorCount =
            static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        // 准备创建描述符池的信息结构体
        VkDescriptorPoolCreateInfo poolInfo {};
        poolInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; // 结构体类型标识
        poolInfo.pNext = nullptr;                          // 没有扩展结构体

        // flags 可以指定一些行为，比如是否能单独释放描述符集 (descriptor set)。
        // 这里我们不设置 flags，表示默认行为（一般是全部重置或释放，而不是单个释放）。
        poolInfo.flags = 0;

        // poolSizeCount 是 poolSizes 数组里有多少种类型
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        // 指向刚才定义的 poolSizes 数组
        poolInfo.pPoolSizes = poolSizes.data();

        // maxSets 是这个描述符池里最多能分配多少个 descriptor set（描述符集）
        // 我们把它设为 MAX_FRAMES_IN_FLIGHT，表示我们打算为每一帧都分配一个 descriptor set
        // 注意：这个值不是 “每类型 descriptor 的数量”，而是 descriptor 集 (set) 的最大数量。 :contentReference[oaicite:0]{index=0}
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        // 创建描述符池
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr,
                                   &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error { "failed to create descriptor pool" };
        }
    }

    void createDescriptorSets() {
        // 创建一个存 descriptor set layout 的 vector，每个 in-flight 帧都对应一个 layout
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                   descriptorSetLayout);

        // 分配描述符集所需的信息
        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; // 结构类型
        allocInfo.pNext = nullptr;                          // 无扩展
        allocInfo.descriptorPool = descriptorPool; // 从哪个 descriptor 池分配
        allocInfo.descriptorSetCount =
            static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT); // 一共分配多少个 set
        allocInfo.pSetLayouts = layouts.data(); // 每个 set 对应的 layout

        // 调整 descriptorSets 容量，以存放为每帧分配出的 descriptor 集
        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

        // 分配 descriptor sets
        if (vkAllocateDescriptorSets(device, &allocInfo,
                                     descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets!");
        }

        // 对每一个 descriptor set 进行填充 (即写入 buffer 和 image 描述符)
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            // 描述 uniform buffer 的信息
            VkDescriptorBufferInfo bufferInfo {};
            bufferInfo.buffer =
                uniformBuffers[i]; // 所属帧对应的 uniform buffer
            bufferInfo.offset = 0; // 从 buffer 开始位置
            bufferInfo.range =
                sizeof(UniformBufferObject); // buffer 的大小（UBO 结构体大小）

            // 描述图像采样器 (texture) 的信息
            VkDescriptorImageInfo imageInfo {};
            imageInfo.imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // 图像 layout
            imageInfo.imageView = textureImageView; // 纹理对应的 image view
            imageInfo.sampler = textureSampler;     // 使用的 sampler

            // 要写入 descriptor set 的结构体数组（两种类型：buffer + image sampler）
            std::array<VkWriteDescriptorSet, 2> descriptorWrites {};

            // 第一个写操作 —— uniform buffer
            descriptorWrites[0].sType =
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; // 结构类型
            descriptorWrites[0].pNext = nullptr;        // 无扩展
            descriptorWrites[0].dstSet =
                descriptorSets[i]; // 写入哪个 descriptor set
            descriptorWrites[0].dstBinding =
                0; // 在 layout 里 binding = 0 的 UBO 位置
            descriptorWrites[0].dstArrayElement =
                0; // 第一个数组元素 (如果是数组 binding)
            descriptorWrites[0].descriptorType =
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         // 类型是 UBO
            descriptorWrites[0].descriptorCount = 1;       // 写 1 个 descriptor
            descriptorWrites[0].pBufferInfo = &bufferInfo; // 指向 bufferInfo

            // 第二个写操作 —— combined image sampler (纹理 + sampler)
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].pNext = nullptr;
            descriptorWrites[1].dstSet =
                descriptorSets[i]; // 写入同一个 descriptor set
            descriptorWrites[1].dstBinding =
                1; // 在 layout 里 binding = 1 的 sampler 位置
            descriptorWrites[1].dstArrayElement = 0; // 第一个元素
            descriptorWrites[1].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // 类型是图像 + 采样器
            descriptorWrites[1].descriptorCount = 1;       // 写 1 个
            descriptorWrites[1].pImageInfo = &imageInfo;   // 指向 imageInfo

            // 实际调用 Vulkan API，将上述描述符写入到 descriptor set
            vkUpdateDescriptorSets(
                device,
                static_cast<uint32_t>(descriptorWrites.size()), // 写操作数量
                descriptorWrites.data(),                        // 写操作数组
                0,      // copy 操作数量（我们这里不做 copy）
                nullptr // copy 操作数组
            );
        }
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

    void createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                     VkSampleCountFlagBits numSamples, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkImage &image,
                     VkDeviceMemory &imageMemory) {
        VkImageCreateInfo imageInfo {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        /*
            tiling 字段可以是两种值之一：
                VK_IMAGE_TILING_LINEAR: 纹素按行主序排列，就像我们的 pixels 数组
                VK_IMAGE_TILING_OPTIMAL: 纹素按实现定义的顺序排列，以实现最佳访问
            */
        imageInfo.tiling = tiling;
        /*
            图像的 initialLayout 只有两种可能的值：
                VK_IMAGE_LAYOUT_UNDEFINED : 对 GPU 不可用，且第一个转换将丢弃纹理元素。
                VK_IMAGE_LAYOUT_PREINITIALIZED : 对 GPU 不可用，但第一个转换将保留纹理元素。
            */
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        /*
            samples 标志与多重采样相关。这仅适用于将图像用作附件的情况，因此请坚持使用单采样。
            有一些可选的标志与稀疏图像相关。稀疏图像是指只有特定区域实际由内存支持的图像。
            例如，如果你使用 3D 纹理来表示体素地形，那么你可以使用这个功能来避免为存储大量的"空气"值分配内存。
            */
        imageInfo.samples = numSamples;
        imageInfo.flags = 0; // Optional

        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image!");
        }

        // 为图像分配内存的方式与为缓冲区分配内存完全相同。
        // 使用 vkGetImageMemoryRequirements 代替 vkGetBufferMemoryRequirements，使用 vkBindImageMemory 代替 vkBindBufferMemory。
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex =
            findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory!");
        }

        vkBindImageMemory(device, image, imageMemory, 0);
    }

    /// 处理布局转换
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t mipLevels) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        // 执行布局转换最常见的方法之一是使用图像内存屏障。
        // 像这样的管线屏障通常用于同步对资源的访问，例如确保向缓冲区的写入完成后再从中读取，但它也可以用于转换图像布局以及在 VK_SHARING_MODE_EXCLUSIVE 使用时转移队列家族所有权。
        // 有一个等效的缓冲区内存屏障可以用来为缓冲区执行此操作。
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // 前两个字段指定布局转换。如果你不在乎图像的现有内容，可以使用 VK_IMAGE_LAYOUT_UNDEFINED 作为 oldLayout
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        // 如果你使用屏障来转移队列家族所有权，那么这两个字段应该是队列家族的索引。如果你不想这样做（不是默认值！），必须将它们设置为 VK_QUEUE_FAMILY_IGNORED 。
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            // 需要注意的是 VK_PIPELINE_STAGE_TRANSFER_BIT 不是图形和计算管线中的真实阶段。它更像是一个伪阶段，传输发生在这里。
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        // 命令缓冲区后的第一个参数指定屏障之前应发生的操作所在的管线阶段。第二个参数指定操作将在哪个管线阶段等待屏障。
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                           uint32_t height) {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        // bufferOffset 指定像素值在缓冲区中的字节偏移量。
        // bufferRowLength 和 bufferImageHeight 字段指定像素在内存中的排列方式。
        // 例如，你可以在图像的行之间有一些填充字节。为两者指定 0 表示像素像本例中这样紧密排列。
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        // imageSubresource、imageOffset 和 imageExtent 字段指示我们将像素复制到图像的哪一部分。
        region.imageOffset = { .x = 0, .y = 0, .z = 0 };
        region.imageExtent = { .width = width, .height = height, .depth = 1 };

        vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        endSingleTimeCommands(commandBuffer);
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
        std::array<VkClearValue, 2> clearValues {};
        clearValues[0].color = { { 0.0F, 0.0F, 0.0F, 1.0F } };
        clearValues[1].depthStencil = { 1.0F, 0 };

        renderPassInfo.clearValueCount =
            static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

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
                             VK_INDEX_TYPE_UINT32);

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
        for (const auto &device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                msaaSamples = getMaxUsableSampleCount();
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
        // 各向异性过滤是一个可选的设备功能
        deviceFeatures.samplerAnisotropy = VK_TRUE;

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
        vkDestroyImageView(device, depthImageView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthImageMemory, nullptr);

        vkDestroyImageView(device, colorImageView, nullptr);
        vkDestroyImage(device, colorImage, nullptr);
        vkFreeMemory(device, colorImageMemory, nullptr);

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
        if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
            throw std::runtime_error(
                std::string("SDL_GetWindowSizeInPixels failed: ") +
                SDL_GetError());
        }
        while (width == 0 || height == 0) {
            if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
                throw std::runtime_error(
                    std::string("SDL_GetWindowSizeInPixels failed: ") +
                    SDL_GetError());
            }
            SDL_Event event;
            SDL_WaitEvent(&event);
        }

        vkDeviceWaitIdle(device);

        cleanupSwapChain();

        createSwapChain();
        createImageViews();
        createColorResources();
        createDepthResources();
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

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        return indices.isComplete() && extensionsSupported &&
               swapChainAdequate && supportedFeatures.samplerAnisotropy;
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
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_EVENT_QUIT: running = false; break;

                case SDL_EVENT_WINDOW_RESIZED:
                    // 这里处理窗口被调整大小
                    framebufferResized = true;
                    // 如果你需要新宽高：
                    {
                        int newWidth = event.window.data1;
                        int newHeight = event.window.data2;
                        LOG_DEBUG(logger, "Window resized: {} x {}", newWidth,
                                  newHeight);
                    }
                    break;

                // 如果你关心 DPI / frame buffer 尺寸改变 (尤其在高 DPI 屏幕上)：
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    // event.window.data1 / data2 是像素尺寸
                    framebufferResized = true;
                    {
                        int newPixelW = event.window.data1;
                        int newPixelH = event.window.data2;
                        LOG_DEBUG(logger, "Pixel size changed: {} x {}",
                                  newPixelW, newPixelH);
                    }
                    break;

                default: break;
                }
            }
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

        vkDestroySampler(device, textureSampler, nullptr);
        vkDestroyImageView(device, textureImageView, nullptr);

        vkDestroyImage(device, textureImage, nullptr);
        vkFreeMemory(device, textureImageMemory, nullptr);

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

    VkSampleCountFlagBits getMaxUsableSampleCount() {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice,
                                      &physicalDeviceProperties);

        VkSampleCountFlags counts =
            physicalDeviceProperties.limits.framebufferColorSampleCounts &
            physicalDeviceProperties.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_64_BIT) {
            return VK_SAMPLE_COUNT_64_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_32_BIT) {
            return VK_SAMPLE_COUNT_32_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_16_BIT) {
            return VK_SAMPLE_COUNT_16_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_8_BIT) {
            return VK_SAMPLE_COUNT_8_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_4_BIT) {
            return VK_SAMPLE_COUNT_4_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_2_BIT) {
            return VK_SAMPLE_COUNT_2_BIT;
        }

        return VK_SAMPLE_COUNT_1_BIT;
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
