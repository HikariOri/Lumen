/**
 * @file main.cpp
 * @brief 手写 2-Pass：Pass1 离屏 ShaderToy → 纹理；Pass2 贴到立方体并呈现。
 */

#include "core/log/logger.hpp"
#include "pch.hpp"
#include "platform/event.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "renderer/mesh.hpp"
#include "renderer/ubo.hpp"
#include "utils.hpp"
#include "vulkan/buffer.hpp"
#include "vulkan/context.hpp"
#include "vulkan/pipeline_builder.hpp"
#include "vulkan/shader/material/shader_material.hpp"
#include "vulkan/shader/reflection/shader_reflection.hpp"

#include <print>

int main() {
    // 初始化日志
    if (!core::log::Logger::init()) {
        std::println(stderr, "日志初始化失败");
        return 1;
    }

    // 初始化窗口
    lumen::platform::WindowConfig windowConfig {};
    windowConfig.fullscreen = false;
    windowConfig.width = 720;
    windowConfig.height = 720;
    windowConfig.title = "two_pass_test";
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
        "two_pass_test", VK_MAKE_API_VERSION(0, 1, 0, 0),
        window.get_vulkan_instance_extensions(),
        static_cast<std::uint32_t>(window.width()),
        static_cast<std::uint32_t>(window.height()),
        [&](VkInstance instance) {
            return window.create_vulkan_surface(instance);
        },
        true);

    auto device = context->device();

    const auto vertexSpirv = load_spirv("./shaders/two_pass_test.vert.spv");
    const auto fragmentSpirv = load_spirv("./shaders/two_pass_test.frag.spv");
    const auto pass1VertSpirv =
        load_spirv("./shaders/pass1_shadertoy.vert.spv");
    const auto pass1FragSpirv =
        load_spirv("./shaders/pass1_shadertoy.frag.spv");

    // 约定：set0 / set1 为 ring UBO → 反射后将 UNIFORM_BUFFER 映为
    // UNIFORM_BUFFER_DYNAMIC
    vulkan::shader::reflection::ReflectOptions reflectOpts {};
    reflectOpts.ringUniformMaxSet = 1u;

    vulkan::shader::reflection::ShaderReflection pass1Reflection {};
    pass1Reflection.reflect(VK_SHADER_STAGE_VERTEX_BIT, pass1VertSpirv,
                            reflectOpts);
    pass1Reflection.reflect(VK_SHADER_STAGE_FRAGMENT_BIT, pass1FragSpirv,
                            reflectOpts);

    vulkan::shader::reflection::ShaderReflection mergedReflection {};
    mergedReflection.reflect(VK_SHADER_STAGE_VERTEX_BIT, vertexSpirv,
                             reflectOpts);
    vulkan::shader::reflection::ShaderReflection fragmentReflection {};
    fragmentReflection.reflect(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSpirv,
                               reflectOpts);
    mergedReflection.merge(fragmentReflection);
    // std::cout << mergedReflection.to_json().dump(2) << std::endl;

    if (!mergedReflection.validateVertexLayout(
            vulkan::shader::reflection::reflect_vertex_members<
                renderer::Vertex>(),
            sizeof(renderer::Vertex))) {
        LUMEN_APP_LOG_CRITICAL("顶点布局与着色器不一致（见引擎日志）");
        return 1;
    }

    mergedReflection.create_layouts(device);
    pass1Reflection.create_layouts(device);
    VkPipelineLayout pipelineLayout = mergedReflection.pipeline_layout();

    // 3. 直接创建材质（自动 Pool + Set）
    auto material = std::make_unique<vulkan::shader::material::ShaderMaterial>(
        device, mergedReflection);
    auto pass1Material =
        std::make_unique<vulkan::shader::material::ShaderMaterial>(
            device, pass1Reflection);

    // VkDescriptorSetLayout descriptorSetLayoutFrame {};
    // VkDescriptorSetLayout descriptorSetLayoutObject {};
    // {
    //     const auto &ord = mergedReflection.set_layouts();
    //     if (ord.size() > 0U) {
    //         descriptorSetLayoutFrame = ord[0];
    //     }
    //     if (ord.size() > 1U) {
    //         descriptorSetLayoutObject = ord[1];
    //     }
    // }

    VkShaderModule vertexShader {};
    VkShaderModule fragmentShader {};
    {
        VkShaderModuleCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };

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

    // constexpr std::uint32_t k_offscreen_width = 512;
    // constexpr std::uint32_t k_offscreen_height = 512;
    int width = 0;
    int height = 0;
    window.get_framebuffer_size(&width, &height);

    // Pass 1：离屏 ShaderToy → R8G8B8A8，结束时 layout 为 SHADER_READ_ONLY
    VkRenderPass pass1_render_pass {};
    {
        VkAttachmentDescription color_attachment {};
        color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_ref {};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;

        VkRenderPassCreateInfo rp_ci {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
        };
        rp_ci.attachmentCount = 1;
        rp_ci.pAttachments = &color_attachment;
        rp_ci.subpassCount = 1;
        rp_ci.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &rp_ci, nullptr, &pass1_render_pass) !=
            VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create pass1 render pass");
        }
    }

    VkImage offscreen_image {};
    VkImageView offscreen_view {};
    VmaAllocation offscreen_allocation {};
    VkSampler offscreen_sampler {};
    VkFramebuffer pass1_framebuffer {};

    {
        VkImageCreateInfo ici { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = { .width = static_cast<std::uint32_t>(width),
                       .height = static_cast<std::uint32_t>(height),
                       .depth = 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo aci {};
        aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(context->allocator(), &ici, &aci, &offscreen_image,
                           &offscreen_allocation, nullptr) != VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create offscreen image");
        }

        VkImageViewCreateInfo vci {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        vci.image = offscreen_image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &vci, nullptr, &offscreen_view) !=
            VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create offscreen image view");
        }

        VkSamplerCreateInfo sci { .sType =
                                      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(device, &sci, nullptr, &offscreen_sampler) !=
            VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create offscreen sampler");
        }

        VkImageView pass1_atts[] = { offscreen_view };
        VkFramebufferCreateInfo fb_ci {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        fb_ci.renderPass = pass1_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments = pass1_atts;
        fb_ci.width = static_cast<std::uint32_t>(width);
        fb_ci.height = static_cast<std::uint32_t>(height);
        fb_ci.layers = 1;

        if (vkCreateFramebuffer(device, &fb_ci, nullptr, &pass1_framebuffer) !=
            VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("Failed to create pass1 framebuffer");
        }
    }

    VkShaderModule pass1_vert_shader {};
    VkShaderModule pass1_frag_shader {};
    {
        VkShaderModuleCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };

        createInfo.codeSize = pass1VertSpirv.size();
        createInfo.pCode =
            reinterpret_cast<const std::uint32_t *>(pass1VertSpirv.data());
        if (vkCreateShaderModule(device, &createInfo, nullptr,
                                 &pass1_vert_shader)) {
            LUMEN_APP_LOG_ERROR("Failed to create pass1 vertex shader module");
        }

        createInfo.codeSize = pass1FragSpirv.size();
        createInfo.pCode =
            reinterpret_cast<const std::uint32_t *>(pass1FragSpirv.data());
        if (vkCreateShaderModule(device, &createInfo, nullptr,
                                 &pass1_frag_shader)) {
            LUMEN_APP_LOG_ERROR(
                "Failed to create pass1 fragment shader module");
        }
    }

    struct Pass1PushConstants {
        glm::vec2 resolution {};
        float time {};
    };

    VkPipelineLayout pass1_pipeline_layout = pass1Reflection.pipeline_layout();
    // {
    //     VkPushConstantRange pcr {};
    //     pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    //     pcr.offset = 0;
    //     pcr.size = sizeof(Pass1PushConstants);

    //     VkPipelineLayoutCreateInfo pl_ci {
    //         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    //     };
    //     pl_ci.pushConstantRangeCount = 1;
    //     pl_ci.pPushConstantRanges = &pcr;

    //     if (vkCreatePipelineLayout(device, &pl_ci, nullptr,
    //                                &pass1_pipeline_layout) != VK_SUCCESS) {
    //         LUMEN_APP_LOG_ERROR("Failed to create pass1 pipeline layout");
    //     }
    // }

    // Pass1 全屏三角形无顶点属性（仅用 gl_VertexIndex），勿用 renderer::Vertex
    VkPipelineVertexInputStateCreateInfo pass1_vertex_input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipeline pass1_pipeline {};
    {
        vulkan::PipelineBuilder pass1_builder;
        pass1_builder.device = device;
        pass1_builder.vertShader = pass1_vert_shader;
        pass1_builder.fragShader = pass1_frag_shader;
        pass1_builder.renderPass = pass1_render_pass;
        pass1_builder.subpass = 0;
        pass1_builder.layout = pass1_pipeline_layout;
        pass1_builder.vertexInputState = &pass1_vertex_input;
        pass1_builder.depthTest = false;
        pass1_builder.depthWrite = false;
        pass1_pipeline = pass1_builder.build();
    }

    // Pass 2：Swapchain 颜色 + 深度 → 屏幕
    VkRenderPass pass2_render_pass {};

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
                               nullptr, &pass2_render_pass)) {
            LUMEN_APP_LOG_ERROR("Failed to create pass2 render pass");
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

            framebufferCreateInfo.renderPass = pass2_render_pass;
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

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo =
        mergedReflection.create_vertex_input_state<renderer::Vertex>();
    // 创建图形管线（VkPipelineLayout / set layouts 已由着色器反射创建）
    VkPipeline graphicsPipeline {};

    vulkan::PipelineBuilder builder;
    builder.device = device;
    builder.vertShader = vertexShader;
    builder.fragShader = fragmentShader;
    builder.renderPass = pass2_render_pass;
    builder.subpass = 0;
    builder.layout = pipelineLayout;
    builder.vertexInputState = &vertexInputStateCreateInfo;
    graphicsPipeline = builder.build();

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

    vulkan::UploadContext::instance().init(device, context->graphics_queue(),
                                           commandPool);
    auto &uploadContext = vulkan::UploadContext::instance();

    auto cube = renderer::createCubeMesh(context->allocator(), uploadContext);

    const VkDeviceSize frameUBOalignedUboSize =
        getMinUniformBufferOffsetAlignment<renderer::ubo::FrameUBO>(
            context.get());
    const VkDeviceSize ubjectUBOalignedUboSize =
        getMinUniformBufferOffsetAlignment<renderer::ubo::ObjectUBO>(
            context.get());

    // 描述符：set0=FrameUBO，set1=ObjectUBO（与 two_pass_test.vert 一致）

    vulkan::DynamicRingBuffer frameUniformBuffer {};
    frameUniformBuffer.init(context->allocator(),
                            sizeof(renderer::ubo::FrameUBO),
                            MAX_FRAMES_IN_FLIGHT, frameUBOalignedUboSize);

    vulkan::DynamicRingBuffer objectiformBuffer {};
    objectiformBuffer.init(context->allocator(),
                           sizeof(renderer::ubo::ObjectUBO),
                           MAX_FRAMES_IN_FLIGHT, ubjectUBOalignedUboSize);

    // VkDescriptorPool descriptorPool {};
    VkDescriptorPool descriptorPool =
        mergedReflection.create_descriptor_pool(device);
    VkDescriptorPool pass1DescriptorPool =
        pass1Reflection.create_descriptor_pool(device);
    std::vector<VkDescriptorSet> descriptorSets = material->get_all_sets();
    std::vector<VkDescriptorSet> pass1DescriptorSets =
        pass1Material->get_all_sets();
    material->update_dynamic_uniforms(
        { { .set = 0,
            .binding = 0,
            .buffer = frameUniformBuffer.buffer.buffer(),
            .offset = 0,
            .size = frameUBOalignedUboSize },
          { .set = 1,
            .binding = 0,
            .buffer = objectiformBuffer.buffer.buffer(),
            .offset = 0,
            .size = ubjectUBOalignedUboSize } });

    material->update_combined(2, 1, offscreen_view, offscreen_sampler,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    pass1Material->update_dynamic_uniforms(
        { { .set = 0,
            .binding = 0,
            .buffer = frameUniformBuffer.buffer.buffer(),
            .offset = 0,
            .size = frameUBOalignedUboSize },
          { .set = 1,
            .binding = 0,
            .buffer = objectiformBuffer.buffer.buffer(),
            .offset = 0,
            .size = ubjectUBOalignedUboSize } });

    // 渲染循环
    while (running && pump.poll()) {

        // 更新 UBO
        int frameIndex = currentFrame;
        {
            auto time = static_cast<float>(SDL_GetTicks() / 1000.0F);

            {
                renderer::ubo::FrameUBO frameUBO {};
                frameUBO.time = time;
                frameUBO.view = glm::lookAt(
                    glm::vec3(3, 3, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
                const float aspect =
                    static_cast<float>(window.width()) /
                    std::max(1.0f, static_cast<float>(window.height()));
                frameUBO.proj =
                    glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

                int width = 0;
                int height = 0;
                window.get_framebuffer_size(&width, &height);
                frameUBO.sceenSize = { static_cast<float>(width),
                                       static_cast<float>(height) };
                // GLM 默认 OpenGL 约定；Vulkan  framebuffer Y 向下，需翻转投影
                // Y
                frameUBO.proj[1][1] *= -1.0f;
                frameUBO.viewProj = frameUBO.proj * frameUBO.view;

                // 每帧获取可写区域
                VkDeviceSize offset;
                auto ptr =
                    frameUniformBuffer.getMappedFrame(frameIndex, offset);
                memcpy(ptr, &frameUBO, sizeof(renderer::ubo::FrameUBO));
            }

            {
                renderer::ubo::ObjectUBO objectUBO {};
                objectUBO.model = glm::rotate(glm::mat4(1), time * 0.5f,
                                              glm::vec3(0.5f, 1.0f, 0.0f));
                const glm::mat3 normal3 =
                    glm::transpose(glm::inverse(glm::mat3(objectUBO.model)));
                objectUBO.normalMatrix = glm::mat4(normal3);

                // 每帧获取可写区域
                VkDeviceSize offset;
                auto ptr = objectiformBuffer.getMappedFrame(frameIndex, offset);
                memcpy(ptr, &objectUBO, sizeof(renderer::ubo::ObjectUBO));
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
            // Pass 1：ShaderToy → 离屏纹理
            VkClearValue pass1_clear {};
            pass1_clear.color.float32[0] = 0.0F;
            pass1_clear.color.float32[1] = 0.0F;
            pass1_clear.color.float32[2] = 0.0F;
            pass1_clear.color.float32[3] = 1.0F;

            VkRenderPassBeginInfo pass1_begin {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            };
            pass1_begin.renderPass = pass1_render_pass;
            pass1_begin.framebuffer = pass1_framebuffer;
            pass1_begin.renderArea.offset = { .x = 0, .y = 0 };
            pass1_begin.renderArea.extent = { .width = static_cast<std::uint32_t>(width),
                                              .height = static_cast<std::uint32_t>(height) };
            pass1_begin.clearValueCount = 1;
            pass1_begin.pClearValues = &pass1_clear;

            vkCmdBeginRenderPass(commandBuffers[imageIndex], &pass1_begin,
                                 VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(commandBuffers[imageIndex],
                              VK_PIPELINE_BIND_POINT_GRAPHICS, pass1_pipeline);

            VkViewport pass1_viewport {};
            pass1_viewport.x = 0.0F;
            pass1_viewport.y = 0.0F;
            pass1_viewport.width = static_cast<float>(width);
            pass1_viewport.height = static_cast<float>(height);
            pass1_viewport.minDepth = 0.0F;
            pass1_viewport.maxDepth = 1.0F;
            vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &pass1_viewport);

            VkRect2D pass1_scissor {};
            pass1_scissor.offset = { .x = 0, .y = 0 };
            pass1_scissor.extent = { .width = static_cast<std::uint32_t>(width),
                                     .height = static_cast<std::uint32_t>(height) };
            vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &pass1_scissor);

            const std::vector<uint32_t> dynamicOffsets {
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      frameUBOalignedUboSize),
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      ubjectUBOalignedUboSize),
            };
            pass1Material->bind_descriptor_sets(commandBuffers[imageIndex], 0,
                                                pass1DescriptorSets,
                                                dynamicOffsets);

            vkCmdDraw(commandBuffers[imageIndex], 3, 1, 0, 0);

            vkCmdEndRenderPass(commandBuffers[imageIndex]);
        }

        {
            // Pass 2：立方体 → Swapchain
            VkRenderPassBeginInfo renderPassBeginInfo {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
            };

            renderPassBeginInfo.renderPass = pass2_render_pass;
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

            const std::vector<uint32_t> dynamicOffsets {
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      frameUBOalignedUboSize),
                static_cast<uint32_t>(static_cast<VkDeviceSize>(frameIndex) *
                                      ubjectUBOalignedUboSize),
            };
            material->bind_descriptor_sets(commandBuffers[imageIndex], 0,
                                           descriptorSets, dynamicOffsets);

            // vertexBuffer.bind_vertex(commandBuffers[imageIndex]);
            // indexBuffer.bind_index(commandBuffers[imageIndex]);
            cube.bind(commandBuffers[imageIndex]);
            cube.draw(commandBuffers[imageIndex]);

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

    vkResetCommandBuffer(uploadContext.commandBuffer, 0);
    for (VkCommandBuffer cb : commandBuffers) {
        vkResetCommandBuffer(cb, 0);
    }

    uploadContext.destroy();
    vkDestroyCommandPool(device, commandPool, nullptr);

    for (VkFramebuffer fb : framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }

    vkDestroyPipeline(device, pass1_pipeline, nullptr);
    vkDestroyShaderModule(device, pass1_vert_shader, nullptr);
    vkDestroyShaderModule(device, pass1_frag_shader, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorPool(device, pass1DescriptorPool, nullptr);
    pass1Reflection.destroy_layouts(device); 
    mergedReflection.destroy_layouts(device);
    vkDestroyRenderPass(device, pass1_render_pass, nullptr);
    vkDestroyFramebuffer(device, pass1_framebuffer, nullptr);
    vkDestroySampler(device, offscreen_sampler, nullptr);
    vkDestroyImageView(device, offscreen_view, nullptr);
    vmaDestroyImage(context->allocator(), offscreen_image,
                    offscreen_allocation);

    vkDestroyRenderPass(device, pass2_render_pass, nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vmaDestroyImage(context->allocator(), depthImage, depthAllocation);

    frameUniformBuffer.destroy();
    objectiformBuffer.destroy();
    cube.destroy();

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

    return 0;
}
