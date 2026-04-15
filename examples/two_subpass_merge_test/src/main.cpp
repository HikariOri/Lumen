/**
 * @file main.cpp
 * @brief RenderGraph 合并 Subpass：Pass1 MRT 画两个三角形 → Pass2 以 input
 * attachment 合成到 swapchain。
 */

#include "core/log/logger.hpp"
#include "pch.hpp"
#include "platform/event.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "renderer/render_graph.hpp"
#include "utils.hpp"
#include "vulkan/context.hpp"
#include "vulkan/pipeline_builder.hpp"
#include "vulkan/shader/material/shader_material.hpp"
#include "vulkan/shader/reflection/shader_reflection.hpp"

#include <array>
#include <print>

namespace {

struct ExecCtx {
    VkPipeline pass1_pipeline = VK_NULL_HANDLE;
    VkPipeline pass2_pipeline = VK_NULL_HANDLE;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace

int main() {
    if (!core::log::Logger::init()) {
        std::println(stderr, "日志初始化失败");
        return 1;
    }

    lumen::platform::WindowConfig windowConfig {};
    windowConfig.fullscreen = false;
    windowConfig.width = 800;
    windowConfig.height = 600;
    windowConfig.title = "two_subpass_merge_test";

    auto window = *lumen::platform::Window::create(windowConfig);

    bool running { true };
    bool framebufferResized { false };

    lumen::platform::EventPump pump;
    pump.set_on_application_event([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);

        d.dispatch<lumen::platform::EventKeyDown>(
            [&](lumen::platform::EventKeyDown &k) {
                if (k.key == lumen::platform::Key::Escape) {
                    running = false;
                    return true;
                }
                return false;
            });
        d.dispatch<lumen::platform::EventWindowResize>(
            [&](lumen::platform::EventWindowResize &) {
                framebufferResized = true;
                return false;
            });
    });

    auto context = *vulkan::Context::create(
        "two_subpass_merge_test", VK_MAKE_API_VERSION(0, 1, 0, 0),
        window.get_vulkan_instance_extensions(),
        static_cast<std::uint32_t>(window.width()),
        static_cast<std::uint32_t>(window.height()),
        [&](VkInstance instance) {
            return window.create_vulkan_surface(instance);
        },
        true);

    VkDevice device = context->device();
    VmaAllocator allocator = context->allocator();
    VkFormat depthFormat = context->depth_format();

    const auto pass1VertSpirv = load_spirv("./shaders/pass1_mrt.vert.spv");
    const auto pass1FragSpirv = load_spirv("./shaders/pass1_mrt.frag.spv");
    const auto pass2VertSpirv = load_spirv("./shaders/pass2_comp.vert.spv");
    const auto pass2FragSpirv = load_spirv("./shaders/pass2_comp.frag.spv");

    if (pass1VertSpirv.empty() || pass1FragSpirv.empty() ||
        pass2VertSpirv.empty() || pass2FragSpirv.empty()) {
        LUMEN_APP_LOG_CRITICAL("着色器 SPIR-V 加载失败");
        return 1;
    }

    VkShaderModule pass1_vert {};
    VkShaderModule pass1_frag {};
    VkShaderModule pass2_vert {};
    VkShaderModule pass2_frag {};
    {
        VkShaderModuleCreateInfo ci {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
        };
        ci.codeSize = pass1VertSpirv.size();
        ci.pCode =
            reinterpret_cast<const std::uint32_t *>(pass1VertSpirv.data());
        vkCreateShaderModule(device, &ci, nullptr, &pass1_vert);
        ci.codeSize = pass1FragSpirv.size();
        ci.pCode =
            reinterpret_cast<const std::uint32_t *>(pass1FragSpirv.data());
        vkCreateShaderModule(device, &ci, nullptr, &pass1_frag);
        ci.codeSize = pass2VertSpirv.size();
        ci.pCode =
            reinterpret_cast<const std::uint32_t *>(pass2VertSpirv.data());
        vkCreateShaderModule(device, &ci, nullptr, &pass2_vert);
        ci.codeSize = pass2FragSpirv.size();
        ci.pCode =
            reinterpret_cast<const std::uint32_t *>(pass2FragSpirv.data());
        vkCreateShaderModule(device, &ci, nullptr, &pass2_frag);
    }

    int fbw = 0;
    int fbh = 0;
    window.get_framebuffer_size(&fbw, &fbh);

    ExecCtx exec {};
    exec.width = static_cast<std::uint32_t>(fbw);
    exec.height = static_cast<std::uint32_t>(fbh);

    vulkan::shader::reflection::ShaderReflection pass1Reflection {};
    pass1Reflection.reflect(VK_SHADER_STAGE_VERTEX_BIT, pass1VertSpirv);
    pass1Reflection.reflect(VK_SHADER_STAGE_FRAGMENT_BIT, pass1FragSpirv);
    pass1Reflection.create_layouts(device);

    vulkan::shader::reflection::ShaderReflection pass2Reflection {};
    pass2Reflection.reflect(VK_SHADER_STAGE_VERTEX_BIT, pass2VertSpirv);
    pass2Reflection.reflect(VK_SHADER_STAGE_FRAGMENT_BIT, pass2FragSpirv);
    pass2Reflection.create_layouts(device);

    auto pass2Material =
        std::make_unique<vulkan::shader::material::ShaderMaterial>(
            device, pass2Reflection);
    std::vector<VkDescriptorSet> pass2DescriptorSets =
        pass2Material->get_all_sets();

    std::unique_ptr<renderer::RenderGraph> renderGraph;
    renderer::TextureHandle rt1 = UINT32_MAX;
    renderer::TextureHandle rt2 = UINT32_MAX;
    renderer::TextureHandle depthHandle = UINT32_MAX;
    renderer::TextureHandle swapHandle = UINT32_MAX;

    constexpr const char *kPass1Name = "MrtPass";
    constexpr const char *kPass2Name = "ComposePass";

    VkPipelineVertexInputStateCreateInfo empty_vi {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    constexpr int MAX_FRAMES_IN_FLIGHT = 3;

    VkCommandPool commandPool {};
    {
        VkCommandPoolCreateInfo cpci {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = context->graphics_queue_family(),
        };
        vkCreateCommandPool(device, &cpci, nullptr, &commandPool);
    }

    std::vector<VkCommandBuffer> commandBuffers(
        context->swapchain_image_views().size());
    {
        VkCommandBufferAllocateInfo cbai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount =
                static_cast<std::uint32_t>(commandBuffers.size()),
        };
        vkAllocateCommandBuffers(device, &cbai, commandBuffers.data());
    }

    std::vector<VkSemaphore> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> presentSemaphores(
        context->swapchain_image_views().size());

    VkSemaphore timelineSemaphore {};

    {
        VkSemaphoreCreateInfo sci {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkCreateSemaphore(device, &sci, nullptr,
                              &imageAvailableSemaphores[i]);
        }
        for (std::size_t i = 0; i < presentSemaphores.size(); ++i) {
            vkCreateSemaphore(device, &sci, nullptr, &presentSemaphores[i]);
        }

        VkSemaphoreTypeCreateInfo tci {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        };
        VkSemaphoreCreateInfo tsci {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &tci,
        };
        vkCreateSemaphore(device, &tsci, nullptr, &timelineSemaphore);
    }

    std::uint32_t currentFrame = 0;
    std::uint64_t timelineValue = 0;
    std::uint32_t imageIndex = 0;

    auto recreate_swapchain_dependent_objects = [&]() -> bool {
        int width = 0;
        int height = 0;
        window.get_framebuffer_size(&width, &height);
        if (width <= 0 || height <= 0) {
            return false;
        }

        auto rc =
            context->recreate_swapchain(static_cast<std::uint32_t>(width),
                                        static_cast<std::uint32_t>(height));
        if (!rc.has_value()) {
            const std::string err = rc.error();
            LUMEN_APP_LOG_ERROR("recreate_swapchain 失败: {}", err.c_str());
            return false;
        }

        exec.width = context->swapchain_width();
        exec.height = context->swapchain_height();

        if (!commandBuffers.empty()) {
            vkFreeCommandBuffers(
                device, commandPool,
                static_cast<std::uint32_t>(commandBuffers.size()),
                commandBuffers.data());
        }
        commandBuffers.assign(context->swapchain_image_views().size(),
                              VK_NULL_HANDLE);
        {
            VkCommandBufferAllocateInfo cbai {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount =
                    static_cast<std::uint32_t>(commandBuffers.size()),
            };
            vkAllocateCommandBuffers(device, &cbai, commandBuffers.data());
        }

        for (VkSemaphore s : presentSemaphores) {
            vkDestroySemaphore(device, s, nullptr);
        }
        presentSemaphores.assign(context->swapchain_image_views().size(),
                                 VK_NULL_HANDLE);
        {
            VkSemaphoreCreateInfo sci {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
            };
            for (std::size_t i = 0; i < presentSemaphores.size(); ++i) {
                vkCreateSemaphore(device, &sci, nullptr, &presentSemaphores[i]);
            }
        }

        if (exec.pass1_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, exec.pass1_pipeline, nullptr);
            exec.pass1_pipeline = VK_NULL_HANDLE;
        }
        if (exec.pass2_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, exec.pass2_pipeline, nullptr);
            exec.pass2_pipeline = VK_NULL_HANDLE;
        }

        renderGraph =
            std::make_unique<renderer::RenderGraph>(device, allocator);
        renderGraph->set_subpass_merging(true);

        rt1 =
            renderGraph->createTexture({ .width = exec.width,
                                         .height = exec.height,
                                         .format = VK_FORMAT_R8G8B8A8_UNORM });
        rt2 =
            renderGraph->createTexture({ .width = exec.width,
                                         .height = exec.height,
                                         .format = VK_FORMAT_R8G8B8A8_UNORM });
        depthHandle = renderGraph->createDepth(
            { .width = exec.width,
              .height = exec.height,
              .format = depthFormat,
              .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT });
        swapHandle = renderGraph->importSwapchain(
            { .width = exec.width,
              .height = exec.height,
              .format = context->swapchain_format() },
            context->swapchain_images(), context->swapchain_image_views());

        renderGraph->add_pass(
            kPass1Name,
            { .reads = {},
              .writes = { rt1, rt2 },
              .depth = depthHandle,
              .extent = { exec.width, exec.height } },
            [&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  exec.pass1_pipeline);

                VkViewport vp { .x = 0.0F,
                                .y = 0.0F,
                                .width = static_cast<float>(exec.width),
                                .height = static_cast<float>(exec.height),
                                .minDepth = 0.0F,
                                .maxDepth = 1.0F };
                vkCmdSetViewport(cmd, 0, 1, &vp);

                VkRect2D sc { .offset = { 0, 0 },
                              .extent = { exec.width, exec.height } };
                vkCmdSetScissor(cmd, 0, 1, &sc);

                std::uint32_t tri = 0;
                vkCmdPushConstants(cmd, pass1Reflection.pipeline_layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(tri),
                                   &tri);
                vkCmdDraw(cmd, 3, 1, 0, 0);

                tri = 1;
                vkCmdPushConstants(cmd, pass1Reflection.pipeline_layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(tri),
                                   &tri);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });

        renderGraph->add_pass(
            kPass2Name,
            { .reads = { rt1, rt2 },
              .writes = { swapHandle },
              .depth = depthHandle,
              .extent = { exec.width, exec.height } },
            [&](VkCommandBuffer cmd) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  exec.pass2_pipeline);
                pass2Material->bind_descriptor_sets(cmd, 0, pass2DescriptorSets,
                                                    {});

                VkViewport vp { .x = 0.0F,
                                .y = 0.0F,
                                .width = static_cast<float>(exec.width),
                                .height = static_cast<float>(exec.height),
                                .minDepth = 0.0F,
                                .maxDepth = 1.0F };
                vkCmdSetViewport(cmd, 0, 1, &vp);

                VkRect2D sc { .offset = { 0, 0 },
                              .extent = { exec.width, exec.height } };
                vkCmdSetScissor(cmd, 0, 1, &sc);

                vkCmdDraw(cmd, 3, 1, 0, 0);
            });

        renderGraph->compile();

        pass2Material->update_input_attachments(
            { { .set = 0,
                .binding = 0,
                .view = renderGraph->getTexture(rt1).view,
                .sampler = VK_NULL_HANDLE,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
              { .set = 0,
                .binding = 1,
                .view = renderGraph->getTexture(rt2).view,
                .sampler = VK_NULL_HANDLE,
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } });

        VkRenderPass merged_rp = renderGraph->render_pass_named(kPass1Name);

        {
            vulkan::PipelineBuilder b;
            b.device = device;
            b.vertShader = pass1_vert;
            b.fragShader = pass1_frag;
            b.renderPass = merged_rp;
            b.subpass = renderGraph->subpass_index_for(kPass1Name);
            b.layout = pass1Reflection.pipeline_layout();
            b.vertexInputState = &empty_vi;
            b.color_attachment_count = 2;
            b.depthTest = true;
            b.depthWrite = true;
            exec.pass1_pipeline = b.build();
        }

        {
            vulkan::PipelineBuilder b;
            b.device = device;
            b.vertShader = pass2_vert;
            b.fragShader = pass2_frag;
            b.renderPass = merged_rp;
            b.subpass = renderGraph->subpass_index_for(kPass2Name);
            b.layout = pass2Reflection.pipeline_layout();
            b.vertexInputState = &empty_vi;
            b.depthTest = false;
            b.depthWrite = false;
            exec.pass2_pipeline = b.build();
        }

        return exec.pass1_pipeline != VK_NULL_HANDLE &&
               exec.pass2_pipeline != VK_NULL_HANDLE;
    };

    // if (!recreate_swapchain_dependent_objects()) {
    //     LUMEN_APP_LOG_CRITICAL("初始交换链依赖资源构建失败");
    //     return 1;
    // }

    while (running && pump.poll()) {
        if (framebufferResized) {
            framebufferResized = false;
            if (!recreate_swapchain_dependent_objects()) {
                continue;
            }
        }
        {
            uint64_t waitValue = timelineValue;
            VkSemaphoreWaitInfo wi {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO
            };
            wi.semaphoreCount = 1;
            wi.pSemaphores = &timelineSemaphore;
            wi.pValues = &waitValue;
            vkWaitSemaphores(device, &wi, UINT64_MAX);
        }

        {
            VkAcquireNextImageInfoKHR ai {
                .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
                .swapchain = context->swapchain(),
                .timeout = UINT64_MAX,
                .semaphore = imageAvailableSemaphores[currentFrame],
                .deviceMask = 1,
            };
            const VkResult acq =
                vkAcquireNextImage2KHR(device, &ai, &imageIndex);
            if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
                framebufferResized = true;
                continue;
            }
        }

        vkResetCommandBuffer(commandBuffers[imageIndex], 0);
        VkCommandBufferBeginInfo bi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        vkBeginCommandBuffer(commandBuffers[imageIndex], &bi);

        renderGraph->execute(commandBuffers[imageIndex], imageIndex);

        vkEndCommandBuffer(commandBuffers[imageIndex]);

        {
            const std::uint64_t signalValue = timelineValue + 1;
            std::array<VkSemaphoreSubmitInfo, 2> waitInfos {};
            waitInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfos[0].semaphore = imageAvailableSemaphores[currentFrame];
            waitInfos[0].value = 0;
            waitInfos[0].stageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            waitInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitInfos[1].semaphore = timelineSemaphore;
            waitInfos[1].value = timelineValue;
            waitInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            std::array<VkSemaphoreSubmitInfo, 2> signalInfos {};
            signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfos[0].semaphore = timelineSemaphore;
            signalInfos[0].value = signalValue;
            signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalInfos[1].semaphore = presentSemaphores[imageIndex];
            signalInfos[1].value = 0;
            signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkCommandBufferSubmitInfo cbsi {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = commandBuffers[imageIndex],
            };

            VkSubmitInfo2 si { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            si.waitSemaphoreInfoCount =
                static_cast<std::uint32_t>(waitInfos.size());
            si.pWaitSemaphoreInfos = waitInfos.data();
            si.commandBufferInfoCount = 1;
            si.pCommandBufferInfos = &cbsi;
            si.signalSemaphoreInfoCount =
                static_cast<std::uint32_t>(signalInfos.size());
            si.pSignalSemaphoreInfos = signalInfos.data();

            vkQueueSubmit2(context->graphics_queue(), 1, &si, VK_NULL_HANDLE);
        }

        {
            VkPresentInfoKHR pi { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            VkSwapchainKHR swapchain = context->swapchain();
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &presentSemaphores[imageIndex];
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &imageIndex;
            const VkResult present =
                vkQueuePresentKHR(context->present_queue(), &pi);
            if (present == VK_ERROR_OUT_OF_DATE_KHR ||
                present == VK_SUBOPTIMAL_KHR) {
                framebufferResized = true;
            }
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        timelineValue++;
    }

    vkDeviceWaitIdle(device);

    vkDestroyPipeline(device, exec.pass1_pipeline, nullptr);
    vkDestroyPipeline(device, exec.pass2_pipeline, nullptr);

    vkDestroyShaderModule(device, pass1_vert, nullptr);
    vkDestroyShaderModule(device, pass1_frag, nullptr);
    vkDestroyShaderModule(device, pass2_vert, nullptr);
    vkDestroyShaderModule(device, pass2_frag, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
    }
    for (VkSemaphore s : presentSemaphores) {
        vkDestroySemaphore(device, s, nullptr);
    }
    vkDestroySemaphore(device, timelineSemaphore, nullptr);
    pass1Reflection.destroy_layouts(device);
    pass2Reflection.destroy_layouts(device);

    core::log::Logger::shutdown();
    return 0;
}
