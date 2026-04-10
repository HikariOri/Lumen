/**
 * @file main.cpp
 * @brief Sandbox：窗口 + `EventPump` / `EventDispatcher` 事件处理示例。
 */

#include "core/log/logger.hpp"
#include "pch.hpp"
#include "platform/event.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "vulkan/context.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <print>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "utils.hpp"

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
};

struct UploadContext {
    VkFence fence;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
};

// 建议：每帧一个，用 ring buffer 管理
struct FrameUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewProj;

    glm::vec3 cameraPos;
    float time {};

    glm::vec2 sceenSize;

    glm::vec4 exposureIblMips;

    int debugMode;
};

// 每 Object 一个，用 ring buffer 管理
struct ObjectUBO {
    glm::mat4 model;
    glm::mat4 normalMatrix;
};

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

    // shader 数据
    std::vector<Vertex> vertices = {
        { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.5f, 0.5f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } },
    };

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
    VkDescriptorSetLayout descriptorSetLayoutFrame {};
    VkDescriptorSetLayout descriptorSetLayoutObject {};
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

            VkVertexInputBindingDescription binding {};
            binding.binding = 0;
            binding.stride = sizeof(Vertex);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            std::array<VkVertexInputAttributeDescription, 2> attributes {};
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
            attributes[0].offset = offsetof(Vertex, pos);

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributes[1].offset = offsetof(Vertex, color);

            vertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
            vertexInputStateCreateInfo.pVertexBindingDescriptions = &binding;

            vertexInputStateCreateInfo.vertexAttributeDescriptionCount =
                attributes.size();
            vertexInputStateCreateInfo.pVertexAttributeDescriptions =
                attributes.data();
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

            // 10. layout：与 sandbox.vert 一致 — set0=FrameUBO，set1=ObjectUBO
            {
                VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
                };

                VkDescriptorSetLayoutBinding frameBinding {};
                frameBinding.binding = 0;
                frameBinding.descriptorType =
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                frameBinding.descriptorCount = 1;
                frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                VkDescriptorSetLayoutCreateInfo frameLayoutInfo {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
                };
                frameLayoutInfo.bindingCount = 1;
                frameLayoutInfo.pBindings = &frameBinding;
                vkCreateDescriptorSetLayout(context->device(), &frameLayoutInfo,
                                            nullptr, &descriptorSetLayoutFrame);

                VkDescriptorSetLayoutBinding objectBinding {};
                objectBinding.binding = 0;
                objectBinding.descriptorType =
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                objectBinding.descriptorCount = 1;
                objectBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

                VkDescriptorSetLayoutCreateInfo objectLayoutInfo {
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
                };
                objectLayoutInfo.bindingCount = 1;
                objectLayoutInfo.pBindings = &objectBinding;
                vkCreateDescriptorSetLayout(context->device(),
                                            &objectLayoutInfo, nullptr,
                                            &descriptorSetLayoutObject);

                const std::array<VkDescriptorSetLayout, 2> pipelineSetLayouts {
                    descriptorSetLayoutFrame,
                    descriptorSetLayoutObject,
                };

                pipelineLayoutCreateInfo.setLayoutCount =
                    static_cast<std::uint32_t>(pipelineSetLayouts.size());
                pipelineLayoutCreateInfo.pSetLayouts =
                    pipelineSetLayouts.data();

                pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
                pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

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
    // std::vector<VkSemaphore> renderFinishedSemaphores {
    // MAX_FRAMES_IN_FLIGHT
    // }; // 渲染完成
    std::vector<VkSemaphore> presentSemaphores(
        context->swapchain_image_views().size()); // 显示完成

    VkSemaphore timelineSemaphore {};

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
        }

        for (size_t i = 0; i < context->swapchain_image_views().size(); i++) {
            if (vkCreateSemaphore(context->device(), &semaphoreCreateInfo,
                                  nullptr, &presentSemaphores[i])) {
                LUMEN_APP_LOG_ERROR("Failed to create present semaphore");
            }
        }

        VkSemaphoreTypeCreateInfo timelineInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO
        };
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        createInfo.pNext = &timelineInfo;

        vkCreateSemaphore(device, &createInfo, nullptr, &timelineSemaphore);
    }

    std::uint32_t currentFrame = 0;

    std::uint64_t timelineValue = 0;

    UploadContext uploadContext {};
    {
        VkFenceCreateInfo fenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fenceCreateInfo, nullptr, &uploadContext.fence);

        VkCommandPoolCreateInfo commandPoolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        commandPoolCreateInfo.flags =
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCreateInfo.queueFamilyIndex =
            context->graphics_queue_family();
        vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr,
                            &uploadContext.commandPool);

        VkCommandBufferAllocateInfo commandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        commandBufferAllocateInfo.commandPool = uploadContext.commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &commandBufferAllocateInfo,
                                 &uploadContext.commandBuffer);
    }

    const auto immediate_submit =
        [&](std::function<void(VkCommandBuffer cmd)> &&function) {
            // 1. 等 GPU 完成上一次上传
            vkWaitForFences(device, 1, &uploadContext.fence, VK_TRUE,
                            UINT64_MAX);

            // 2. 重置 fence
            vkResetFences(device, 1, &uploadContext.fence);

            // 3. 重置  command buffer
            vkResetCommandBuffer(uploadContext.commandBuffer, 0);

            // 4. 开始录制
            VkCommandBufferBeginInfo beginInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
            };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(uploadContext.commandBuffer, &beginInfo);

            // 5. 用户录制 copy / barrier 等命令
            function(uploadContext.commandBuffer);

            vkEndCommandBuffer(uploadContext.commandBuffer);

            // 6.提交
            VkSubmitInfo2 submitInfo { .sType =
                                           VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };

            VkCommandBufferSubmitInfo commandBufferInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO
            };
            commandBufferInfo.commandBuffer = uploadContext.commandBuffer;
            // commandBufferInfo.deviceMask = 1;

            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandBufferInfo;

            vkQueueSubmit2(context->graphics_queue(), 1, &submitInfo,
                           uploadContext.fence);

            // 须在本次 submit 之后再等 fence，否则 staging 可能在 copy
            // 完成前被销毁。
            vkWaitForFences(device, 1, &uploadContext.fence, VK_TRUE,
                            UINT64_MAX);
        };

    const auto create_buffer = [&](std::size_t size, VkBufferUsageFlags usage,
                                   VmaMemoryUsage memoryUsage,
                                   VmaAllocationCreateFlags flags) {
        AllocatedBuffer buffer {};

        VkBufferCreateInfo bufferCreateInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO
        };
        bufferCreateInfo.size = size;
        bufferCreateInfo.usage = usage;

        VmaAllocationCreateInfo allocationCreateInfo {};
        allocationCreateInfo.usage = memoryUsage;
        allocationCreateInfo.flags = flags;

        vmaCreateBuffer(context->allocator(), &bufferCreateInfo,
                        &allocationCreateInfo, &buffer.buffer,
                        &buffer.allocation, nullptr);

        return buffer;
    };

    // vertex buffer
    AllocatedBuffer vertexBuffer {};
    {
        // 创建 staging buffer
        AllocatedBuffer stagingBuffer = create_buffer(
            sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        // 映射内存
        void *mapped {};
        vmaMapMemory(context->allocator(), stagingBuffer.allocation, &mapped);
        // 复制数据
        memcpy(mapped, vertices.data(), sizeof(Vertex) * vertices.size());
        // 解映射
        vmaUnmapMemory(context->allocator(), stagingBuffer.allocation);

        vertexBuffer =
            create_buffer(sizeof(Vertex) * vertices.size(),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_GPU_ONLY,
                          VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        // copy 内存

        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copy {};
            copy.srcOffset = 0;
            copy.dstOffset = 0;
            copy.size = sizeof(Vertex) * vertices.size();

            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, vertexBuffer.buffer, 1,
                            &copy);
        });

        vmaDestroyBuffer(context->allocator(), stagingBuffer.buffer,
                         stagingBuffer.allocation);
    }

    AllocatedBuffer frameUniformBuffer {};
    void *frameUBOMapped {};

    VkPhysicalDeviceProperties physicalDeviceProps {};
    vkGetPhysicalDeviceProperties(context->physical_device(),
                                  &physicalDeviceProps);
    const VkDeviceSize minUniformBufferOffsetAlignment =
        physicalDeviceProps.limits.minUniformBufferOffsetAlignment;
    const VkDeviceSize frameUBOalignedUboSize =
        (static_cast<VkDeviceSize>(sizeof(FrameUBO)) +
         minUniformBufferOffsetAlignment - 1) &
        ~(minUniformBufferOffsetAlignment - 1);

    // 分配 UBO（多帧环形：勿在内层再声明 uniformBuffer，否则会遮蔽外层句柄）
    frameUniformBuffer = create_buffer(
        static_cast<std::size_t>(
            frameUBOalignedUboSize *
            static_cast<VkDeviceSize>(MAX_FRAMES_IN_FLIGHT)),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    vmaMapMemory(context->allocator(), frameUniformBuffer.allocation,
                 &frameUBOMapped);

    AllocatedBuffer objectiformBuffer {};
    void *objectUBOMapped {};

    const VkDeviceSize ubjectUBOalignedUboSize =
        (static_cast<VkDeviceSize>(sizeof(ObjectUBO)) +
         minUniformBufferOffsetAlignment - 1) &
        ~(minUniformBufferOffsetAlignment - 1);

    objectiformBuffer = create_buffer(
        static_cast<std::size_t>(
            ubjectUBOalignedUboSize *
            static_cast<VkDeviceSize>(MAX_FRAMES_IN_FLIGHT)),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    vmaMapMemory(context->allocator(), objectiformBuffer.allocation,
                 &objectUBOMapped);

    // 描述符：set0=FrameUBO，set1=ObjectUBO（与 sandbox.vert 一致）
    VkDescriptorPool descriptorPool {};
    std::array<VkDescriptorSet, 2> descriptorSets {};
    {
        VkDescriptorPoolSize poolSize {};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSize.descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        poolCreateInfo.maxSets = 2;
        poolCreateInfo.poolSizeCount = 1;
        poolCreateInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolCreateInfo, nullptr,
                               &descriptorPool);

        const std::array<VkDescriptorSetLayout, 2> allocSetLayouts {
            descriptorSetLayoutFrame,
            descriptorSetLayoutObject,
        };

        VkDescriptorSetAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount =
            static_cast<std::uint32_t>(allocSetLayouts.size());
        allocInfo.pSetLayouts = allocSetLayouts.data();

        vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data());

        VkDescriptorBufferInfo bufferInfoFrame {};
        bufferInfoFrame.buffer = frameUniformBuffer.buffer;
        bufferInfoFrame.offset = 0;
        bufferInfoFrame.range = frameUBOalignedUboSize;

        VkDescriptorBufferInfo bufferInfoObject {};
        bufferInfoObject.buffer = objectiformBuffer.buffer;
        bufferInfoObject.offset = 0;
        bufferInfoObject.range = ubjectUBOalignedUboSize;

        std::array<VkWriteDescriptorSet, 2> writeDescriptorSets {};

        writeDescriptorSets[0].sType = writeDescriptorSets[1].sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

        writeDescriptorSets[0].dstSet = descriptorSets[0];
        writeDescriptorSets[0].dstBinding = 0;
        writeDescriptorSets[0].dstArrayElement = 0;
        writeDescriptorSets[0].descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].pBufferInfo = &bufferInfoFrame;

        writeDescriptorSets[1].dstSet = descriptorSets[1];
        writeDescriptorSets[1].dstBinding = 0;
        writeDescriptorSets[1].dstArrayElement = 0;
        writeDescriptorSets[1].descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writeDescriptorSets[1].descriptorCount = 1;
        writeDescriptorSets[1].pBufferInfo = &bufferInfoObject;

        vkUpdateDescriptorSets(device, writeDescriptorSets.size(),
                               writeDescriptorSets.data(), 0, nullptr);
    }

    // 渲染循环
    while (running && pump.poll()) {

        // 更新 UBO
        int frameIndex = currentFrame;
        {
            auto time = static_cast<float>(SDL_GetTicks() / 1000.0F);
            {
                char *dst = (char *)frameUBOMapped +
                            frameIndex * frameUBOalignedUboSize;
                FrameUBO frameUBO {};
                frameUBO.time = time;
                memcpy(dst, &frameUBO, sizeof(frameUBO));
            }

            {
                ObjectUBO objectUBO {};
                char *dst = (char *)objectUBOMapped +
                            frameIndex * ubjectUBOalignedUboSize;

                objectUBO.model =
                    glm::rotate(glm::mat4(1.0F), time, glm::vec3(0, 0, 1));
                const glm::mat3 normal3 =
                    glm::transpose(glm::inverse(glm::mat3(objectUBO.model)));
                objectUBO.normalMatrix = glm::mat4(normal3);
                memcpy(dst, &objectUBO, sizeof(objectUBO));
            }
        }
        // 等上一帧 GPU 完成
        {
            uint64_t waitValue = timelineValue;

            VkSemaphoreWaitInfo waitInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO
            };
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &timelineSemaphore;
            waitInfo.pValues = &waitValue;

            vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
        }

        // 获取 swapchiain image index
        std::uint32_t imageIndex {};
        {
            // VkDevice                                    device,
            // const VkAcquireNextImageInfoKHR*            pAcquireInfo,
            // uint32_t*                                   pImageIndex;

            VkAcquireNextImageInfoKHR acquireNextImageInfo {
                .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR
            };

            acquireNextImageInfo.deviceMask = 1;
            acquireNextImageInfo.swapchain = context->swapchain();
            acquireNextImageInfo.timeout = UINT64_MAX;
            acquireNextImageInfo.semaphore =
                imageAvailableSemaphores[currentFrame];
            acquireNextImageInfo.fence = VK_NULL_HANDLE;

            vkAcquireNextImage2KHR(device, &acquireNextImageInfo, &imageIndex);
        }

        // 重置并录制 command buffer
        vkResetCommandBuffer(commandBuffers[imageIndex], 0);

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

            VkDeviceSize offsets {};

            vkCmdBindVertexBuffers(commandBuffers[imageIndex], 0, 1,
                                   &vertexBuffer.buffer, &offsets);

            const std::array<uint32_t, 2> dynamicOffsets {
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      frameUBOalignedUboSize),
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      ubjectUBOalignedUboSize),
            };
            vkCmdBindDescriptorSets(
                commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 0,
                static_cast<std::uint32_t>(descriptorSets.size()),
                descriptorSets.data(),
                static_cast<std::uint32_t>(dynamicOffsets.size()),
                dynamicOffsets.data());

            vkCmdDraw(commandBuffers[imageIndex], 3, 1, 0, 0);

            vkCmdEndRenderPass(commandBuffers[imageIndex]);
        }

        vkEndCommandBuffer(commandBuffers[imageIndex]);

        // 提交 GPU（混用 binary + timeline 时，Timeline 扩展里 value
        // 数组长度须与 wait/signal 数量一致；binary 对应项的值会被忽略）
        {
            const std::uint64_t signalValue = timelineValue + 1;

            // wait
            std::array<VkSemaphoreSubmitInfo, 2> waitInfos {};

            // imageAvailableSemaphores[currentFrame] (binary)
            waitInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfos[0].semaphore = imageAvailableSemaphores[currentFrame];
            waitInfos[0].value = 0; // 忽略
            waitInfos[0].stageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            waitInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfos[1].semaphore = timelineSemaphore;
            waitInfos[1].value = timelineValue;
            waitInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            // signal
            std::array<VkSemaphoreSubmitInfo, 2> signalInfos {};

            // timeline
            signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfos[0].semaphore = timelineSemaphore;
            signalInfos[0].value = signalValue;
            signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            // presentSemaphores[imageIndex] (binary)
            signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfos[1].semaphore = presentSemaphores[imageIndex];
            signalInfos[1].value = 0; // 忽略
            signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            // command buffer
            VkCommandBufferSubmitInfo commandBufferInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO
            };

            commandBufferInfo.commandBuffer = commandBuffers[imageIndex];
            // uint32_t           deviceMask;

            VkSubmitInfo2 submitInfo { .sType =
                                           VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };

            submitInfo.waitSemaphoreInfoCount = waitInfos.size();
            submitInfo.pWaitSemaphoreInfos = waitInfos.data();

            submitInfo.signalSemaphoreInfoCount = signalInfos.size();
            submitInfo.pSignalSemaphoreInfos = signalInfos.data();

            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandBufferInfo;

            vkQueueSubmit2(context->graphics_queue(), 1, &submitInfo,
                           VK_NULL_HANDLE);
        }

        // present
        {
            VkPresentInfoKHR presentInfo {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
            };

            auto swapchain = context->swapchain();
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;
            // presentInfo.pResults;

            vkQueuePresentKHR(context->present_queue(), &presentInfo);
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        timelineValue++;
    }

    vkDeviceWaitIdle(device);

    // 清空录制内容再销毁 pool：部分校验层在 pool 仍存在时，仍认为 CB「引用」
    // vertex/uniform buffer（即使已 wait idle），导致
    // VUID-vkDestroyBuffer-buffer-00922。
    vkResetCommandBuffer(uploadContext.commandBuffer, 0);
    for (VkCommandBuffer cb : commandBuffers) {
        vkResetCommandBuffer(cb, 0);
    }

    vkDestroyCommandPool(device, uploadContext.commandPool, nullptr);
    vkDestroyFence(device, uploadContext.fence, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);

    for (VkFramebuffer fb : framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayoutObject, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayoutFrame, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vmaDestroyImage(context->allocator(), depthImage, depthAllocation);

    vmaUnmapMemory(context->allocator(), frameUniformBuffer.allocation);
    vmaDestroyBuffer(context->allocator(), frameUniformBuffer.buffer,
                     frameUniformBuffer.allocation);

    vmaUnmapMemory(context->allocator(), objectiformBuffer.allocation);
    vmaDestroyBuffer(context->allocator(), objectiformBuffer.buffer,
                     objectiformBuffer.allocation);

    vmaDestroyBuffer(context->allocator(), vertexBuffer.buffer,
                     vertexBuffer.allocation);

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    }

    for (VkSemaphore semaphore : presentSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }

    if (timelineSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, timelineSemaphore, nullptr);
    }

    core::log::Logger::shutdown();
    SDL_Quit();
    return 0;
}
