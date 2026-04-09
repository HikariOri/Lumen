/**
 * @file main.cpp
 * @brief Sandbox：窗口 + `EventPump` / `EventDispatcher` 事件处理示例。
 */

#include "core/log/logger.hpp"
#include "platform/event.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "vulkan/context.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <print>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

[[nodiscard]] std::vector<std::byte> load_spirv(const std::string &filename) {
    std::ifstream file { filename, std::ios::ate | std::ios::binary };

    if (!file) {
        LUMEN_APP_LOG_ERROR("Failed to open file: {}", filename);
        return {};
    }

    const auto end = file.tellg();
    if (end <= 0) {
        LUMEN_APP_LOG_ERROR("SPIR-V 文件为空或无效: {}", filename);
        return {};
    }
    const auto size = static_cast<std::size_t>(end);
    if (size % sizeof(std::uint32_t) != 0U) {
        LUMEN_APP_LOG_ERROR("SPIR-V 文件大小须为 4 的倍数: {}", filename);
        return {};
    }

    file.seekg(0);
    std::vector<std::byte> out(size);
    file.read(reinterpret_cast<char *>(out.data()), // NOLINT
              static_cast<std::streamsize>(size));
    if (!file) {
        LUMEN_APP_LOG_ERROR("SPIR-V 读取失败: {}", filename);
        return {};
    }
    return out;
}

int main() {
    // 初始化日志
    if (!core::log::Logger::init()) {
        std::println(stderr, "日志初始化失败");
        return 1;
    }

    // 初始化窗口
    lumen::platform::WindowConfig windowConfig {};
    windowConfig.fullscreen = false;
    windowConfig.width = 1280;
    windowConfig.height = 720;
    windowConfig.title = "Sandbox";
    windowConfig.icon_path = "./assets/textures/ikun2026_happy_new_year.jpg";

    auto window = *lumen::platform::Window::create(windowConfig);

    bool running { true };

    // 初始化事件调度
    lumen::platform::EventPump pump;
    pump.set_on_application_event([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);

        d.dispatch<lumen::platform::EventQuit>(
            [&](lumen::platform::EventQuit &) {
                LUMEN_APP_LOG_INFO("收到退出请求");
            });

        d.dispatch<lumen::platform::EventWindowResize>(
            [&](lumen::platform::EventWindowResize &r) {
                LUMEN_APP_LOG_INFO("窗口尺寸变化: {} x {}", r.width, r.height);
            });

        d.dispatch<lumen::platform::EventKeyDown>(
            [&](lumen::platform::EventKeyDown &k) {
                LUMEN_APP_LOG_INFO("键盘按下: {}",
                                   lumen::platform::key_name(k.key));
                if (k.key == lumen::platform::Key::Escape) {
                    running = false;
                    return true;
                }
                return false;
            });

        d.dispatch<lumen::platform::EventMouseMove>(
            [&](lumen::platform::EventMouseMove &m) {
                LUMEN_APP_LOG_INFO("鼠标移动: {} x {}", m.x, m.y);
                LUMEN_APP_LOG_INFO("鼠标移动: {} x {}", m.deltaX, m.deltaY);
                return false;
            });
    });

    auto context = *vulkan::Context::create(
        "Sandbox", VK_MAKE_API_VERSION(0, 1, 0, 0),
        window.get_vulkan_instance_extensions(),
        static_cast<std::uint32_t>(window.width()),
        static_cast<std::uint32_t>(window.height()),
        [&](VkInstance instance) {
            return window.create_vulkan_surface(instance);
        },
        true);

    auto device = context->device();

    VkShaderModule vertexShader {};
    VkShaderModule fragmentShader {};
    {
        VkShaderModuleCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };

        auto vertexSpirv = load_spirv("./shaders/sandbox.vert.spv");
        auto fragmentSpirv = load_spirv("./shaders/sandbox.frag.spv");

        createInfo.codeSize = vertexSpirv.size();
        createInfo.pCode = (const std::uint32_t *)vertexSpirv.data();
        if (vkCreateShaderModule(context->device(), &createInfo, nullptr,
                                 &vertexShader)) {
            LUMEN_APP_LOG_ERROR("Failed to create vertex shader module");
        }

        createInfo.codeSize = fragmentSpirv.size();
        createInfo.pCode = (const std::uint32_t *)fragmentSpirv.data();
        if (vkCreateShaderModule(context->device(), &createInfo, nullptr,
                                 &fragmentShader)) {
            LUMEN_APP_LOG_ERROR("Failed to create fragment shader module");
        }
    }

    // 创建 Renader Pass
    VkRenderPass renderPass {};

    {
        VkAttachmentDescription colorAttachment {};
        colorAttachment.format = context->swapchain_format();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        // colorAttachment.stencilLoadOp;
        // colorAttachment.stencilStoreOp;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout =
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 用于呈现，在交换链中

        VkAttachmentDescription depthAttachment {};
        depthAttachment.format = context->depth_format();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        // depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentReference {};
        colorAttachmentReference.attachment = 0;
        colorAttachmentReference.layout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef {};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentReference;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkAttachmentDescription, 2> attachments { colorAttachment,
                                                             depthAttachment };

        VkRenderPassCreateInfo renderPassCreateInfo {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        };

        renderPassCreateInfo.attachmentCount = attachments.size();
        renderPassCreateInfo.pAttachments = attachments.data();
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;
        //   renderPassCreateInfo.dependencyCount;
        //   renderPassCreateInfo.pDependencies;

        if (vkCreateRenderPass(context->device(), &renderPassCreateInfo,
                               nullptr, &renderPass)) {
            LUMEN_APP_LOG_ERROR("Failed to create render pass");
        }
    }

    // 深度附件
    VkImage depthImage {};
    VkImageView depthImageView {};
    VmaAllocation depthAllocation {};
    {

        const VkFormat depthFormat = context->depth_format();

        VkImageCreateInfo imageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        };

        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = depthFormat;
        imageCreateInfo.extent = { .width = window.width(),
                                   .height = window.height(),
                                   .depth = 1 };
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // imageCreateInfo.queueFamilyIndexCount = 0;
        // imageCreateInfo.pQueueFamilyIndices = nullptr;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocationCreateInfo {};
        //  depth 必须 GPU_ONLY
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        // allocationCreateInfo.requiredFlags =
        // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        // allocationCreateInfo.preferredFlags =
        // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        // allocationCreateInfo.memoryTypeBits =
        // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        auto allocator = context->allocator();

        if (vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo,
                           &depthImage, &depthAllocation,
                           nullptr) != VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create depth image");
        }

        VkImageViewCreateInfo imageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };

        imageViewCreateInfo.image = depthImage;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = depthFormat;
        // imageViewCreateInfo.components;
        imageViewCreateInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(context->device(), &imageViewCreateInfo, nullptr,
                              &depthImageView)) {
            LUMEN_APP_LOG_ERROR("Failed to create depth image view");
        }
    }

    // VkFramebuffer framebuffer {};
    std::vector<VkFramebuffer> framebuffers(
        context->swapchain_image_views().size());
    {
        auto swapchainImageViews = context->swapchain_image_views();
        auto swapchainWidth = window.width();
        auto swapchainHeight = window.height();

        for (size_t i = 0; i < swapchainImageViews.size(); i++) {
            // attachment index 和 renderpass 里的顺序一致！
            std::array<VkImageView, 2> attachments {
                context->swapchain_image_views()[i],
                depthImageView,
            };

            VkFramebufferCreateInfo framebufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
            };

            framebufferCreateInfo.renderPass = renderPass;
            framebufferCreateInfo.attachmentCount = attachments.size();
            framebufferCreateInfo.pAttachments = attachments.data();
            framebufferCreateInfo.width = swapchainWidth;
            framebufferCreateInfo.height = swapchainHeight;
            framebufferCreateInfo.layers = 1;

            if (vkCreateFramebuffer(context->device(), &framebufferCreateInfo,
                                    nullptr, &framebuffers[i])) {
                LUMEN_APP_LOG_ERROR("Failed to create framebuffer");
            }
        }
    }

    // 创建图形管线
    VkPipeline graphicsPipeline {};
    VkPipelineLayout pipelineLayout {};
    {

        // 1. shader stages

        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages {};

        {
            shaderStages[0].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStages[0].module = vertexShader;
            shaderStages[0].pName = "main";

            shaderStages[1].sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStages[1].module = fragmentShader;
            shaderStages[1].pName = "main";
        }

        // 2 vertex Input
        VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        {
            vertexInputStateCreateInfo.vertexBindingDescriptionCount = 0;
            vertexInputStateCreateInfo.pVertexBindingDescriptions = nullptr;
            vertexInputStateCreateInfo.vertexAttributeDescriptionCount = 0;
            vertexInputStateCreateInfo.pVertexAttributeDescriptions = nullptr;
        }

        // 3. input assembly

        VkPipelineInputAssemblyStateCreateInfo inputAssembly {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };

        {
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = false;
        }

        // 4. viewport & scissor

        VkPipelineViewportStateCreateInfo viewportStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };

        VkViewport pipelineViewport {};
        VkRect2D pipelineScissor {};
        {
            pipelineViewport.x = 0.0F;
            pipelineViewport.y = 0.0F;
            pipelineViewport.width = static_cast<float>(window.width());
            pipelineViewport.height = static_cast<float>(window.height());
            pipelineViewport.minDepth = 0.0F;
            pipelineViewport.maxDepth = 1.0F;

            pipelineScissor.offset = { .x = 0, .y = 0 };
            pipelineScissor.extent = {
                .width = static_cast<std::uint32_t>(window.width()),
                .height = static_cast<std::uint32_t>(window.height())
            };

            viewportStateCreateInfo.viewportCount = 1;
            viewportStateCreateInfo.pViewports = &pipelineViewport;
            viewportStateCreateInfo.scissorCount = 1;
            viewportStateCreateInfo.pScissors = &pipelineScissor;
        }

        // 5. rasterizer

        VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };

        {
            rasterizerCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizerCreateInfo.lineWidth = 1;
            rasterizerCreateInfo.cullMode = VK_CULL_MODE_NONE;
            rasterizerCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
            // 这是什么?
            // rasterizerCreateInfo.rasterizerDiscardEnable = false;
            // rasterizerCreateInfo.depthBiasEnable = false;
            // rasterizerCreateInfo.depthBiasClamp = 0;
            // rasterizerCreateInfo.depthClampEnable = true;
            // rasterizerCreateInfo.depthBiasConstantFactor = 0;
            // rasterizerCreateInfo.depthBiasSlopeFactor = 0;
        }

        // 6. multisampling

        VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };

        {
            multisamplingCreateInfo.rasterizationSamples =
                VK_SAMPLE_COUNT_1_BIT;

            // multisamplingCreateInfo.sampleShadingEnable = false;
            // multisamplingCreateInfo.minSampleShading = 1;
            // multisamplingCreateInfo.alphaToCoverageEnable = false;
            // multisamplingCreateInfo.pSampleMask = nullptr;
            // multisamplingCreateInfo.alphaToOneEnable = false;
        }

        // 7. depth stencil
        VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };

        {
            depthStencilCreateInfo.depthTestEnable = true;
            depthStencilCreateInfo.depthWriteEnable = true;
            depthStencilCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
            // depthStencilCreateInfo.depthBoundsTestEnable = false;
            // depthStencilCreateInfo.stencilTestEnable = false;
            // depthStencilCreateInfo.front = {};
            // depthStencilCreateInfo.back = {};
            // depthStencilCreateInfo.minDepthBounds = 0.0F;
            // depthStencilCreateInfo.maxDepthBounds = 1.0F;
        }

        // 8. color blend
        VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };

        {
            VkPipelineColorBlendAttachmentState colorBlendAttachment {};

            colorBlendAttachment.colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

            // colorBlendAttachment.blendEnable = false;
            // colorBlendAttachment.srcColorBlendFactor =
            // VK_BLEND_FACTOR_SRC_ALPHA;
            // colorBlendAttachment.dstColorBlendFactor =
            //     VK_BLEND_FACTOR_DST_COLOR;
            // colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            // colorBlendAttachment.srcAlphaBlendFactor =
            //     VK_BLEND_FACTOR_SRC_ALPHA;
            // colorBlendAttachment.dstAlphaBlendFactor =
            //     VK_BLEND_FACTOR_DST_ALPHA;
            // colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            colorBlendingCreateInfo.attachmentCount = 1;
            colorBlendingCreateInfo.pAttachments = &colorBlendAttachment;
            // colorBlendingCreateInfo.logicOpEnable = false;
            // colorBlendingCreateInfo.logicOp = VK_LOGIC_OP_COPY;
            // colorBlendingCreateInfo.blendConstants[0] = 0.0F;
            // colorBlendingCreateInfo.blendConstants[1] = 0.0F;
            // colorBlendingCreateInfo.blendConstants[2] = 0.0F;
            // colorBlendingCreateInfo.blendConstants[3] = 0.0F;

            // 9. dynamic state
            VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
            };

            {
                std::array<VkDynamicState, 2> dynamicStates {
                    VK_DYNAMIC_STATE_VIEWPORT,
                    VK_DYNAMIC_STATE_SCISSOR,
                    // VK_DYNAMIC_STATE_DEPTH_BIAS,
                    // VK_DYNAMIC_STATE_BLEND_CONSTANTS
                };

                dynamicStateCreateInfo.dynamicStateCount = dynamicStates.size();
                dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();
            }

            // 10. layout
            {
                VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
                };

                {
                    pipelineLayoutCreateInfo.setLayoutCount = 0;
                    pipelineLayoutCreateInfo.pSetLayouts = nullptr;
                    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
                    pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
                }

                if (vkCreatePipelineLayout(context->device(),
                                           &pipelineLayoutCreateInfo, nullptr,
                                           &pipelineLayout)) {
                    LUMEN_APP_LOG_ERROR("Failed to create pipeline layout");
                }
            }

            // 11. pipeline
            VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
            };

            {
                // clang-format off
                graphicsPipelineCreateInfo.stageCount = 2;
                graphicsPipelineCreateInfo.pStages = shaderStages.data();
                graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
                graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
                // graphicsPipelineCreateInfo.pTessellationState;
                graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
                graphicsPipelineCreateInfo.pRasterizationState = &rasterizerCreateInfo;
                graphicsPipelineCreateInfo.pMultisampleState = &multisamplingCreateInfo;
                graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilCreateInfo;
                graphicsPipelineCreateInfo.pColorBlendState = &colorBlendingCreateInfo;
                graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
                graphicsPipelineCreateInfo.layout =pipelineLayout;
                graphicsPipelineCreateInfo.renderPass = renderPass;
                graphicsPipelineCreateInfo.subpass = 0;
                // graphicsPipelineCreateInfo.basePipelineHandle;
                // graphicsPipelineCreateInfo.basePipelineIndex;
                // clang-format on
            }

            if (vkCreateGraphicsPipelines(context->device(), nullptr, 1,
                                          &graphicsPipelineCreateInfo, nullptr,
                                          &graphicsPipeline)) {
                LUMEN_APP_LOG_ERROR("Failed to create graphics pipeline");
            }
        }

        vkDestroyShaderModule(context->device(), vertexShader, nullptr);
        vkDestroyShaderModule(context->device(), fragmentShader, nullptr);
    }

    constexpr int MAX_FRAMES_IN_FLIGHT = 3;

    // Command Pool
    VkCommandPool commandPool {};
    {
        VkCommandPoolCreateInfo commandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        commandPoolCreateInfo.flags =
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex =
            context->graphics_queue_family();
        if (vkCreateCommandPool(context->device(), &commandPoolCreateInfo,
                                nullptr, &commandPool)) {
            LUMEN_APP_LOG_ERROR("Failed to create command pool");
        }
    }
    std::vector<VkCommandBuffer> commandBuffers(
        context->swapchain_image_views().size());
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };

        commandBufferAllocateInfo.commandPool = commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = commandBuffers.size();

        if (vkAllocateCommandBuffers(context->device(),
                                     &commandBufferAllocateInfo,
                                     commandBuffers.data())) {
            LUMEN_APP_LOG_ERROR("Failed to allocate command buffers");
        }
    }

    std::vector<VkSemaphore> imageAvailableSemaphores(
        MAX_FRAMES_IN_FLIGHT); // image 可用
                               // 渲染完成
    std::vector<VkSemaphore> presentSemaphores(
        context->swapchain_image_views().size());              // 显示完成
    std::vector<VkFence> inFlightFences(MAX_FRAMES_IN_FLIGHT); // CPU 等 GPU
    std::vector<VkFence> imagesInFlight(
        context->swapchain_image_views().size());

    for (size_t i = 0; i < context->swapchain_image_views().size(); i++) {
        imagesInFlight[i] = VK_NULL_HANDLE;
    }

    {
        VkSemaphoreCreateInfo semaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };

        VkFenceCreateInfo fenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(context->device(), &semaphoreCreateInfo,
                                  nullptr, &imageAvailableSemaphores[i])) {
                LUMEN_APP_LOG_ERROR(
                    "Failed to create image available semaphore");
            }

            if (vkCreateFence(context->device(), &fenceCreateInfo, nullptr,
                              &inFlightFences[i])) {
                LUMEN_APP_LOG_ERROR("Failed to create in flight fence");
            }
        }

        for (size_t i = 0; i < context->swapchain_image_views().size(); i++) {
            if (vkCreateSemaphore(context->device(), &semaphoreCreateInfo,
                                  nullptr, &presentSemaphores[i])) {
                LUMEN_APP_LOG_ERROR("Failed to create present semaphore");
            }
        }
    }

    std::uint32_t currentFrame = 0;

    // 渲染循环
    while (running && pump.poll()) {

        VkSemaphore imageAvailable = imageAvailableSemaphores[currentFrame];
        VkFence fence = inFlightFences[currentFrame];

        // 等上一帧 GPU 完成
        vkWaitForFences(device, 1, &fence, true, UINT64_MAX);

        // 获取 swapchiain image
        std::uint32_t imageIndex {};
        vkAcquireNextImageKHR(device, context->swapchain(), UINT64_MAX,
                              imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE,
                            UINT64_MAX);
        }

        // 标记 image 被当前 frame 使用
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        // reset fence（重要）
        vkResetFences(device, 1, &fence);

        // 重置并录制 command buffer
        vkResetCommandBuffer(commandBuffers[imageIndex], 0);
        // recordCommandBuffer(commandBuffers[imageIndex], imageIndex);

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

        {
            // 开始 Render Pass
            VkRenderPassBeginInfo renderPassBeginInfo {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            };

            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = framebuffers[imageIndex];

            renderPassBeginInfo.renderArea.offset = { .x = 0, .y = 0 };
            renderPassBeginInfo.renderArea.extent = { .width = window.width(),
                                                      .height =
                                                          window.height() };

            VkClearValue clearValues[2];
            clearValues[0].color = { { 0.1, 0.1, 0.1, 1.0 } };
            clearValues[1].depthStencil = { .depth = 1.0, .stencil = 0 };

            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;

            vkCmdBeginRenderPass(commandBuffers[imageIndex],
                                 &renderPassBeginInfo,
                                 VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(commandBuffers[imageIndex],
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              graphicsPipeline);

            VkViewport dynamicViewport {};
            dynamicViewport.x = 0.0F;
            dynamicViewport.y = 0.0F;
            dynamicViewport.width = static_cast<float>(window.width());
            dynamicViewport.height = static_cast<float>(window.height());
            dynamicViewport.minDepth = 0.0F;
            dynamicViewport.maxDepth = 1.0F;
            vkCmdSetViewport(commandBuffers[imageIndex], 0, 1,
                             &dynamicViewport);

            VkRect2D dynamicScissor {};
            dynamicScissor.offset = { .x = 0, .y = 0 };
            dynamicScissor.extent = {
                .width = static_cast<std::uint32_t>(window.width()),
                .height = static_cast<std::uint32_t>(window.height())
            };
            vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &dynamicScissor);

            vkCmdDraw(commandBuffers[imageIndex], 3, 1, 0, 0);

            vkCmdEndRenderPass(commandBuffers[imageIndex]);
        }

        vkEndCommandBuffer(commandBuffers[imageIndex]);

        // 提交 GPU
        VkSubmitInfo submitInfo { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };

        VkSemaphore waitSemaphores[] { imageAvailable };
        VkPipelineStageFlags waitStags[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStags;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &presentSemaphores[imageIndex];

        vkQueueSubmit(context->graphics_queue(), 1, &submitInfo, fence);

        // present
        VkPresentInfoKHR presentInfo { .sType =
                                           VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        auto swapchain = context->swapchain();
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        // presentInfo.pResults;

        vkQueuePresentKHR(context->present_queue(), &presentInfo);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(device);

    for (VkFramebuffer fb : framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vmaDestroyImage(context->allocator(), depthImage, depthAllocation);

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        // vkDestroySemaphore(device, presentSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    for (VkSemaphore semaphore : presentSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    core::log::Logger::shutdown();
    SDL_Quit();
    return 0;
}
