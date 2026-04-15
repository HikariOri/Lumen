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
#include "renderer/render_graph.hpp"
#include "renderer/ubo.hpp"
#include "utils.hpp"
#include "vulkan/buffer.hpp"
#include "vulkan/context.hpp"
#include "vulkan/pipeline_builder.hpp"
#include "vulkan/sampler.hpp"
#include "vulkan/shader/material/shader_material.hpp"
#include "vulkan/shader/reflection/shader_reflection.hpp"
#include "vulkan/texture.hpp"

#include <cmath>
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
    windowConfig.title = "imgui_test";
    windowConfig.icon_path = "./assets/textures/ikun2026_happy_new_year.jpg";

    auto window = *lumen::platform::Window::create(windowConfig);

    bool running { true };
    bool framebufferResized { false };

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
                framebufferResized = true;
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
        "imgui_test", VK_MAKE_API_VERSION(0, 1, 0, 0),
        window.get_vulkan_instance_extensions(),
        static_cast<std::uint32_t>(window.width()),
        static_cast<std::uint32_t>(window.height()),
        [&](VkInstance instance) {
            return window.create_vulkan_surface(instance);
        },
        true);

    auto device = context->device();
    VmaAllocator allocator = context->allocator();
    // VkExtent2D swapchainExtent = context->swapchain_extent();
    VkFormat swapchainFormat = context->swapchain_format();
    VkFormat depthFormat = context->depth_format();
    VkFormat offscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat swapchainImageFormat = swapchainFormat;
    VkFormat depthImageFormat = depthFormat;
    VkFormat offscreenImageFormat = offscreenFormat;
    VkFormat swapchainImageViewFormat = swapchainFormat;
    VkFormat depthImageViewFormat = depthFormat;
    VkFormat offscreenImageViewFormat = offscreenFormat;

    const auto vertexSpirv = load_spirv("./shaders/imgui_test.vert.spv");
    const auto fragmentSpirv = load_spirv("./shaders/imgui_test.frag.spv");
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
    auto scene_target_width = static_cast<std::uint32_t>(width);
    auto scene_target_height = static_cast<std::uint32_t>(height);

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

    // Pass1 全屏三角形无顶点属性（仅用 gl_VertexIndex），勿用 renderer::Vertex
    VkPipelineVertexInputStateCreateInfo pass1_vertex_input {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo =
        mergedReflection.create_vertex_input_state<renderer::Vertex>();

    VkPipeline pass1_pipeline {};
    VkPipeline graphicsPipeline {};

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

    // 描述符：set0=FrameUBO，set1=ObjectUBO（与 imgui_test.vert 一致）

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

    auto renderGraph =
        std::make_unique<renderer::RenderGraph>(device, allocator);
    renderer::TextureHandle swapHandle = UINT32_MAX;
    renderer::TextureHandle offscreenHandle = UINT32_MAX;
    renderer::TextureHandle sceneHandle = UINT32_MAX;
    renderer::TextureHandle depthHandle = UINT32_MAX;
    std::uint32_t imageIndex {};

    VkSampler offscreen_sampler = vulkan::create_sampler(
        device, { .magFilter = VK_FILTER_LINEAR,
                  .minFilter = VK_FILTER_LINEAR,
                  .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                  .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  .minLod = 0.0F,
                  .maxLod = 0.0F });
    if (offscreen_sampler == VK_NULL_HANDLE) {
        LUMEN_APP_LOG_ERROR("Failed to create offscreen sampler");
    }
    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet offscreen_imgui_set = VK_NULL_HANDLE;
    float shadertoy_clear_color[4] { 0.1F, 0.1F, 0.1F, 1.0F };

    swapHandle = renderGraph->importSwapchain(
        { .width = static_cast<std::uint32_t>(width),
          .height = static_cast<std::uint32_t>(height),
          .format = context->swapchain_format() },
        context->swapchain_images(), context->swapchain_image_views());
    offscreenHandle =
        renderGraph->createTexture({ .width = 1024, .height = 1024 });
    sceneHandle = renderGraph->createTexture(
        { .width = scene_target_width, .height = scene_target_height });
    depthHandle = renderGraph->createDepth(
        { .width = scene_target_width,
          .height = scene_target_height,
          .format = depthFormat,
          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT });
    renderGraph->add_pass(
        "ShaderToy",
        { .writes = { offscreenHandle },
          .extent = { .width = 1024, .height = 1024 } },
        [&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pass1_pipeline);
            VkViewport pass1_viewport {};
            pass1_viewport.x = 0.0F;
            pass1_viewport.y = 0.0F;
            pass1_viewport.width = static_cast<float>(1024);
            pass1_viewport.height = static_cast<float>(1024);
            pass1_viewport.minDepth = 0.0F;
            pass1_viewport.maxDepth = 1.0F;
            vkCmdSetViewport(cmd, 0, 1, &pass1_viewport);
            VkRect2D pass1_scissor {};
            pass1_scissor.offset = { .x = 0, .y = 0 };
            pass1_scissor.extent = { .width = 1024, .height = 1024 };
            vkCmdSetScissor(cmd, 0, 1, &pass1_scissor);
            const std::vector<uint32_t> dynamicOffsets {
                static_cast<uint32_t>(static_cast<VkDeviceSize>(currentFrame) *
                                      frameUBOalignedUboSize),
                static_cast<uint32_t>(static_cast<VkDeviceSize>(currentFrame) *
                                      ubjectUBOalignedUboSize),
            };
            pass1Material->bind_descriptor_sets(cmd, 0, pass1DescriptorSets,
                                                dynamicOffsets);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });

    renderGraph->add_pass(
        "CubeToViewport",
        { .reads = { offscreenHandle },
          .writes = { sceneHandle },
          .depth = depthHandle,
          .extent = { .width = scene_target_width,
                      .height = scene_target_height },
          .clearColor = { { 1, 1, 1, 1 } } },
        [&](VkCommandBuffer cmd) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              graphicsPipeline);
            VkViewport vp { .x = 0,
                            .y = 0,
                            .width = static_cast<float>(scene_target_width),
                            .height = static_cast<float>(scene_target_height),
                            .minDepth = 0,
                            .maxDepth = 1 };
            VkRect2D scissor { .offset = { .x = 0, .y = 0 },
                               .extent = { .width = scene_target_width,
                                           .height = scene_target_height } };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            const std::vector<uint32_t> dynamicOffsets {
                static_cast<uint32_t>(static_cast<VkDeviceSize>(currentFrame) *
                                      frameUBOalignedUboSize),
                static_cast<uint32_t>(static_cast<VkDeviceSize>(currentFrame) *
                                      ubjectUBOalignedUboSize),
            };
            material->bind_descriptor_sets(cmd, 0, descriptorSets,
                                           dynamicOffsets);
            cube.bind(cmd);
            cube.draw(cmd);
        });

    renderGraph->add_pass("ImGuiToSwapchain",
                          { .reads = { sceneHandle },
                            .writes = { swapHandle },
                            .extent = { .width = scene_target_width,
                                        .height = scene_target_height },
                            .clearColor = { { 0, 0, 0, 1 } } },
                          [&](VkCommandBuffer cmd) {
                              ImGui_ImplVulkan_RenderDrawData(
                                  ImGui::GetDrawData(), cmd);
                          });
    renderGraph->compile();

    material->update_combined(
        2, 1, renderGraph->getTexture(offscreenHandle).view, offscreen_sampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        ImGui::StyleColorsDark();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
            ImGuiStyle &style = ImGui::GetStyle();
            style.WindowRounding = 0.0F;
            style.Colors[ImGuiCol_WindowBg].w = 1.0F;
        }
        ImGui_ImplSDL3_InitForVulkan(window.sdl_window());

        ImGui_ImplVulkan_InitInfo init_info = {};
        {
            init_info.Instance = context->instance();
            init_info.PhysicalDevice = context->physical_device();
            init_info.Device = device;
            init_info.QueueFamily = context->graphics_queue_family();
            init_info.Queue = context->graphics_queue();
            // init_info.PipelineCache = YOUR_PIPELINE_CACHE;
            // init_info.DescriptorPool = YOUR_DESCRIPTOR_POOL;
            init_info.DescriptorPoolSize = 1000;
            init_info.MinImageCount =
                static_cast<std::uint32_t>(MAX_FRAMES_IN_FLIGHT);
            init_info.ImageCount = static_cast<std::uint32_t>(
                context->swapchain_image_views().size());
            // init_info.Allocator = YOUR_ALLOCATOR;
            init_info.RenderPass =
                renderGraph->render_pass_named("ImGuiToSwapchain");
            init_info.Subpass =
                renderGraph->subpass_index_for("ImGuiToSwapchain");
            init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            // init_info.CheckVkResultFn = check_vk_result;
        }

        ImGui_ImplVulkan_Init(&init_info);
        ImGui_ImplVulkan_CreateFontsTexture();
        ImGui_ImplVulkan_DestroyFontsTexture();

        offscreen_imgui_set = ImGui_ImplVulkan_AddTexture(
            offscreen_sampler, renderGraph->getTexture(sceneHandle).view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    pump.add_sdl_event_handler([&](const void *sdl_event) {
        ImGui_ImplSDL3_ProcessEvent(
            reinterpret_cast<const SDL_Event *>(sdl_event));
    });

    {
        vulkan::PipelineBuilder pass1_builder;
        pass1_builder.device = device;
        pass1_builder.vertShader = pass1_vert_shader;
        pass1_builder.fragShader = pass1_frag_shader;
        pass1_builder.renderPass = renderGraph->render_pass_named("ShaderToy");
        pass1_builder.subpass = renderGraph->subpass_index_for("ShaderToy");
        pass1_builder.layout = pass1_pipeline_layout;
        pass1_builder.vertexInputState = &pass1_vertex_input;
        pass1_builder.depthTest = false;
        pass1_builder.depthWrite = false;
        pass1_pipeline = pass1_builder.build();
    }
    {
        vulkan::PipelineBuilder builder;
        builder.device = device;
        builder.vertShader = vertexShader;
        builder.fragShader = fragmentShader;
        builder.renderPass = renderGraph->render_pass_named("CubeToViewport");
        builder.subpass = renderGraph->subpass_index_for("CubeToViewport");
        builder.layout = pipelineLayout;
        builder.vertexInputState = &vertexInputStateCreateInfo;
        graphicsPipeline = builder.build();
    }

    auto rebuild_swapchain_dependent = [&]() -> bool {
        vkQueueWaitIdle(context->graphics_queue());
        vkQueueWaitIdle(context->present_queue());
        vkDeviceWaitIdle(device);
        int w = 0;
        int h = 0;
        window.get_framebuffer_size(&w, &h);
        if (w <= 0 || h <= 0) {
            return false;
        }

        auto rc = context->recreate_swapchain(static_cast<std::uint32_t>(w),
                                              static_cast<std::uint32_t>(h));
        if (!rc.has_value()) {
            LUMEN_APP_LOG_ERROR("recreate_swapchain 失败");
            return false;
        }

        width = static_cast<int>(context->swapchain_width());
        height = static_cast<int>(context->swapchain_height());

        if (!commandBuffers.empty()) {
            vkFreeCommandBuffers(
                device, commandPool,
                static_cast<std::uint32_t>(commandBuffers.size()),
                commandBuffers.data());
        }
        commandBuffers.assign(context->swapchain_image_views().size(),
                              VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo commandBufferAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        commandBufferAllocateInfo.commandPool = commandPool;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount =
            static_cast<std::uint32_t>(commandBuffers.size());
        vkAllocateCommandBuffers(context->device(), &commandBufferAllocateInfo,
                                 commandBuffers.data());

        for (VkSemaphore semaphore : presentSemaphores) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        presentSemaphores.assign(context->swapchain_image_views().size(),
                                 VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        for (std::size_t i = 0; i < presentSemaphores.size(); i++) {
            vkCreateSemaphore(context->device(), &semaphoreCreateInfo, nullptr,
                              &presentSemaphores[i]);
        }

        renderGraph->resize_swapchain(
            swapHandle,
            { .width = static_cast<std::uint32_t>(width),
              .height = static_cast<std::uint32_t>(height),
              .format = context->swapchain_format() },
            context->swapchain_images(), context->swapchain_image_views());
        // renderGraph->resize_renderpass(
        //     offscreenHandle, static_cast<std::uint32_t>(scene_target_width),
        //     static_cast<std::uint32_t>(scene_target_height));
        renderGraph->resize_renderpass(sceneHandle, scene_target_width,
                                       scene_target_height);
        renderGraph->resize_renderpass(depthHandle, scene_target_width,
                                       scene_target_height);
        material->update_combined(
            2, 1, renderGraph->getTexture(offscreenHandle).view,
            offscreen_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (offscreen_imgui_set != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(offscreen_imgui_set);
        }
        offscreen_imgui_set = ImGui_ImplVulkan_AddTexture(
            offscreen_sampler, renderGraph->getTexture(sceneHandle).view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    };

    auto rebuild_scene_viewport_target = [&](std::uint32_t new_width,
                                             std::uint32_t new_height) -> bool {
        if (new_width == 0 || new_height == 0) {
            return false;
        }
        if (new_width == scene_target_width &&
            new_height == scene_target_height) {
            return true;
        }
        vkQueueWaitIdle(context->graphics_queue());
        vkQueueWaitIdle(context->present_queue());
        vkDeviceWaitIdle(device);
        scene_target_width = new_width;
        scene_target_height = new_height;
        renderGraph->resize_renderpass(sceneHandle, new_width, new_height);
        // renderGraph->resize_renderpass(offscreenHandle, new_width,
        // new_height);
        renderGraph->resize_renderpass(depthHandle, new_width, new_height);

        material->update_combined(
            2, 1, renderGraph->getTexture(offscreenHandle).view,
            offscreen_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (offscreen_imgui_set != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(offscreen_imgui_set);
        }
        offscreen_imgui_set = ImGui_ImplVulkan_AddTexture(
            offscreen_sampler, renderGraph->getTexture(sceneHandle).view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    };

    // 渲染循环
    while (running && pump.poll()) {
        if (framebufferResized) {
            framebufferResized = false;
            if (!rebuild_swapchain_dependent()) {
                continue;
            }
        }

        {
            renderGraph->set_pass_clear_color(
                "CubeToViewport",
                { shadertoy_clear_color[0], shadertoy_clear_color[1],
                  shadertoy_clear_color[2], shadertoy_clear_color[3] });
        }

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
                    static_cast<float>(scene_target_width) /
                    std::max(1.0f, static_cast<float>(scene_target_height));
                frameUBO.proj =
                    glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

                // int width = 0;
                // int height = 0;
                // window.get_framebuffer_size(&width, &height);
                frameUBO.sceenSize = { static_cast<float>(scene_target_width),
                                       static_cast<float>(
                                           scene_target_height) };
                // GLM 默认 OpenGL 约定；Vulkan  framebuffer Y 向下，需翻转投影
                // Y
                frameUBO.proj[1][1] *= -1.0f;
                frameUBO.viewProj = frameUBO.proj * frameUBO.view;

                // 每帧获取可写区域
                VkDeviceSize offset;
                auto ptr =
                    frameUniformBuffer.get_mapped_frame(frameIndex, offset);
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
                VkDeviceSize offset {};
                auto ptr =
                    objectiformBuffer.get_mapped_frame(frameIndex, offset);
                memcpy(ptr, &objectUBO, sizeof(renderer::ubo::ObjectUBO));
            }
        }

        {
            const float t = static_cast<float>(SDL_GetTicks()) * 0.001F;
            VkClearColorValue dynamicClear {};
            dynamicClear.float32[0] = 0.5F + 0.5F * std::sin(t);
            dynamicClear.float32[1] = 0.5F + 0.5F * std::sin(t + 2.0944F);
            dynamicClear.float32[2] = 0.5F + 0.5F * std::sin(t + 4.1888F);
            dynamicClear.float32[3] = 1.0F;
            renderGraph->set_pass_clear_color("ShaderToy", dynamicClear);
        }
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
        {
            ImGui::Begin("Settings");
            ImGui::ColorEdit4("Cube Clear Color", shadertoy_clear_color);
            ImGui::End();
        }
        {
            ImGui::Begin("Scene");
            ImVec2 avail = ImGui::GetContentRegionAvail();

            if (avail.x < 1.0F) {
                avail.x = 1.0F;
            }
            if (avail.y < 1.0F) {
                avail.y = 1.0F;
            }
            if (offscreen_imgui_set != VK_NULL_HANDLE) {
                const std::uint32_t desired_width =
                    static_cast<std::uint32_t>(std::max(1.0F, avail.x));
                const std::uint32_t desired_height =
                    static_cast<std::uint32_t>(std::max(1.0F, avail.y));
                rebuild_scene_viewport_target(desired_width, desired_height);
                ImGui::Image(reinterpret_cast<ImTextureID>(offscreen_imgui_set),
                             avail);
                const bool image_hovered = ImGui::IsItemHovered();
                if (image_hovered && !ImGui::IsWindowDocked() &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 pos = ImGui::GetWindowPos();
                    const ImVec2 delta = ImGui::GetIO().MouseDelta;
                    pos.x += delta.x;
                    pos.y += delta.y;
                    ImGui::SetWindowPos(pos);
                }
            }
            ImGui::End();
        }
        ImGui::Render();

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
        {

            VkAcquireNextImageInfoKHR acquireNextImageInfo {
                .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR
            };

            acquireNextImageInfo.deviceMask = 1;
            acquireNextImageInfo.swapchain = context->swapchain();
            acquireNextImageInfo.timeout = UINT64_MAX;
            acquireNextImageInfo.semaphore =
                imageAvailableSemaphores[currentFrame];
            acquireNextImageInfo.fence = VK_NULL_HANDLE;

            const VkResult acquireResult = vkAcquireNextImage2KHR(
                device, &acquireNextImageInfo, &imageIndex);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
                acquireResult == VK_SUBOPTIMAL_KHR) {
                framebufferResized = true;
                continue;
            }
        }

        {
            vkResetCommandBuffer(commandBuffers[imageIndex], 0);

            VkCommandBufferBeginInfo beginInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
            };
            vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

            renderGraph->execute(commandBuffers[imageIndex], imageIndex);

            vkEndCommandBuffer(commandBuffers[imageIndex]);
        }

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

            const VkResult presentResult =
                vkQueuePresentKHR(context->present_queue(), &presentInfo);
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
                presentResult == VK_SUBOPTIMAL_KHR) {
                framebufferResized = true;
            }
        }
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) !=
            0) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
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

    vkDestroyPipeline(device, pass1_pipeline, nullptr);
    vkDestroyShaderModule(device, pass1_vert_shader, nullptr);
    vkDestroyShaderModule(device, pass1_frag_shader, nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyShaderModule(device, vertexShader, nullptr);
    vkDestroyShaderModule(device, fragmentShader, nullptr);
    if (offscreen_imgui_set != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(offscreen_imgui_set);
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, imgui_descriptor_pool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorPool(device, pass1DescriptorPool, nullptr);
    pass1Reflection.destroy_layouts(device);
    mergedReflection.destroy_layouts(device);
    vkDestroySampler(device, offscreen_sampler, nullptr);

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
