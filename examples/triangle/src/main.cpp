/**
 * @file main.cpp
 * @brief 最小 Vulkan 示例：彩色三角形绕 Z 轴旋转（SDL3 + `Context` + UBO 描述符 +
 * `Material` + `CommandPool` + 帧/按图同步封装 + `RenderGraph` 单 pass 写交换链）。
 */

#include "core/log/logger.hpp"
#include "platform/window.hpp"
#include "vulkan/buffer.hpp"
#include "vulkan/command_pool.hpp"
#include "vulkan/context.hpp"
#include "vulkan/material.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/render_graph.hpp"
#include "vulkan/render_target.hpp"
#include "vulkan/shader_program.hpp"
#include "vulkan/sync_objects.hpp"
#include "vulkan/ubo.hpp"

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <expected>
#include <print>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t k_frames_in_flight { 2 };

/**
 * @brief CPU 顶点布局须与 `build_vertex_input_state(顶点着色器反射)` 的
 * stride/offset 一致（`vec2` + `vec3` 紧密共 20 字节）。
 */
struct GpuVertex {
    float position[2];
    float color[3];
};
static_assert(sizeof(GpuVertex) == 20);

} // namespace

int main() {

    // 初始化窗口
    lumen::platform::Window window {};
    lumen::platform::WindowConfig win_cfg {};
    win_cfg.title = "Lumen — 旋转三角形";
    win_cfg.width = 1280;
    win_cfg.height = 720;
    if (!window.create(win_cfg)) {
        std::println(stderr, "窗口创建失败");
        return 1;
    }
    int fb_w { 0 };
    int fb_h { 0 };
    // 获得窗口的framebuffer尺寸
    window.get_framebuffer_size(&fb_w, &fb_h);
    if (fb_w <= 0 || fb_h <= 0) {
        std::println(stderr, "framebuffer 尺寸无效");
        return 1;
    }
    // 初始化Vulkan上下文
    auto ctx_result =
        vulkan::ContextBuilder {}
            .set_application_name("triangle")
            .set_application_version(VK_MAKE_API_VERSION(0, 1, 0, 0))
            .set_instance_extensions(window.get_vulkan_instance_extensions())
            .set_initial_size(static_cast<std::uint32_t>(fb_w),
                              static_cast<std::uint32_t>(fb_h))
            .set_surface_from_instance([&window](const VkInstance inst) {
                return window.create_vulkan_surface(inst);
            })
            .set_enable_validation(true)
            .build();
    if (!ctx_result.has_value()) {
        std::println(stderr, "Vulkan Context: {}", ctx_result.error());
        return 1;
    }
    // 加载 shader
    std::unique_ptr<vulkan::Context> ctx = std::move(*ctx_result);
    auto vert_load =
        vulkan::Shader::load_spv(ctx->device(), "./shaders/triangle.vert.spv",
                                 VK_SHADER_STAGE_VERTEX_BIT);
    auto frag_load =
        vulkan::Shader::load_spv(ctx->device(), "./shaders/triangle.frag.spv",
                                 VK_SHADER_STAGE_FRAGMENT_BIT);
    if (!vert_load.has_value()) {
        std::println(stderr, "顶点着色器: {}", vert_load.error());
        return 1;
    }
    if (!frag_load.has_value()) {
        std::println(stderr, "片段着色器: {}", frag_load.error());
        return 1;
    }

    // 创建 shader program
    auto program_result = vulkan::ShaderProgram::create_graphics(
        std::move(*vert_load), std::move(*frag_load));
    if (!program_result.has_value()) {
        std::println(stderr, "ShaderProgram: {}", program_result.error());
        return 1;
    }
    vulkan::ShaderProgram gpu_program = std::move(*program_result);

    // 获取顶点输入布局
    auto vertex_input_result = gpu_program.vertex_input_state(0);
    if (!vertex_input_result.has_value()) {
        std::println(stderr, "vertex_input_state: {}",
                     vertex_input_result.error());
        return 1;
    }
    vulkan::VertexInputState vertex_input = std::move(*vertex_input_result);
    if (sizeof(GpuVertex) != vertex_input.stride) {
        std::println(
            stderr,
            "GpuVertex 大小 {} 与反射 stride {} 不一致，请对齐顶点结构或着色器",
            sizeof(GpuVertex), vertex_input.stride);
        return 1;
    }

    // 获取管线布局和描述符布局
    auto layout_resources_result =
        gpu_program.create_layout_resources(ctx->device());
    if (!layout_resources_result.has_value()) {
        std::println(stderr, "create_layout_resources: {}",
                     layout_resources_result.error());
        return 1;
    }
    vulkan::ShaderProgramLayoutResources program_layouts =
        std::move(*layout_resources_result);

    constexpr std::uint32_t k_descriptor_pool_max_sets { 8 };
    auto desc_pool_result =
        gpu_program.create_descriptor_pool(ctx->device(), k_descriptor_pool_max_sets);
    if (!desc_pool_result.has_value()) {
        std::println(stderr, "create_descriptor_pool: {}",
                     desc_pool_result.error());
        return 1;
    }
    vulkan::DescriptorPool desc_pool = std::move(*desc_pool_result);

    const std::vector<VkDescriptorSetLayout> &set_layouts =
        program_layouts.descriptor_set_layouts();
    if (set_layouts.empty() || set_layouts[0] == VK_NULL_HANDLE) {
        std::println(stderr, "set 0 无有效 VkDescriptorSetLayout（需在顶点着色器声明 "
                             "layout(set=0) uniform）");
        return 1;
    }

    const std::vector<GpuVertex> vertices {
        { { 0.F, 0.55F }, { 1.F, 0.2F, 0.3F } },
        { { -0.5F, -0.45F }, { 0.2F, 1.F, 0.3F } },
        { { 0.5F, -0.45F }, { 0.3F, 0.5F, 1.F } },
    };

    // 创建 VBO
    vulkan::BufferCreateInfo vbuf_info {};
    vbuf_info.size = vertices.size() * vertex_input.stride;
    vbuf_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbuf_info.memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    auto vbuf_result = vulkan::Buffer::create(ctx->allocator(), vbuf_info);
    if (!vbuf_result.has_value()) {
        std::println(stderr, "顶点缓冲区: {}", vbuf_result.error());
        return 1;
    }
    vulkan::Buffer vertex_buffer = std::move(*vbuf_result);
    // 上传数据
    if (auto map = vertex_buffer.map(); map.has_value()) {
        std::memcpy(map.value(), vertices.data(), vbuf_info.size);
        vertex_buffer.unmap();
    } else {
        std::println(stderr, "map 顶点缓冲: {}", map.error());
        return 1;
    }

    vulkan::BufferCreateInfo ubo_info {};
    ubo_info.size = sizeof(vulkan::UboViewProj);
    ubo_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    ubo_info.memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    auto ubo_buf_result = vulkan::Buffer::create(ctx->allocator(), ubo_info);
    if (!ubo_buf_result.has_value()) {
        std::println(stderr, "UBO 缓冲区: {}", ubo_buf_result.error());
        return 1;
    }
    vulkan::Buffer ubo_buffer = std::move(*ubo_buf_result);

    auto material_result =
        vulkan::Material::create(ctx->device(), desc_pool.pool(), set_layouts[0]);
    if (!material_result.has_value()) {
        std::println(stderr, "Material: {}", material_result.error());
        return 1;
    }
    vulkan::Material triangle_material = std::move(*material_result);
    triangle_material.write_uniform_buffer(0, ubo_buffer, 0, sizeof(vulkan::UboViewProj));

    vulkan::RenderGraph render_graph { *ctx };
    const auto &sc_views = ctx->swapchain_image_views();
    const auto &sc_images = ctx->swapchain_images();
    if (sc_views.empty() || sc_images.empty()) {
        std::println(stderr, "交换链图像为空");
        return 1;
    }
    const vulkan::RgResourceHandle swap_h = render_graph.import_swapchain(
        vulkan::render_target_from_swapchain_view(
            sc_views[0], ctx->swapchain_format(), ctx->swapchain_width(),
            ctx->swapchain_height()),
        sc_images[0]);

    VkPipeline graphics_pipeline { VK_NULL_HANDLE };

    render_graph.add_pass(
        "Triangle",
        { { swap_h, vulkan::RgAccess::Write } }, {},
        [&ctx, &program_layouts, &vertex_buffer, &triangle_material,
         gp = &graphics_pipeline](VkCommandBuffer cmd,
                                  const vulkan::RenderTargetBundle &) {
            if (*gp == VK_NULL_HANDLE) {
                return;
            }
            const VkViewport viewport {
                .x = 0.F,
                .y = 0.F,
                .width = static_cast<float>(ctx->swapchain_width()),
                .height = static_cast<float>(ctx->swapchain_height()),
                .minDepth = 0.F,
                .maxDepth = 1.F,
            };
            const VkRect2D scissor {
                .offset = { 0, 0 },
                .extent = { ctx->swapchain_width(), ctx->swapchain_height() },
            };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *gp);

            const VkDescriptorSet descriptor_set =
                triangle_material.descriptor_set();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    program_layouts.pipeline_layout(), 0, 1,
                                    &descriptor_set, 0, nullptr);

            const VkDeviceSize vb_offset { 0 };
            const VkBuffer vb = vertex_buffer.buffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vb_offset);

            vkCmdDraw(cmd, 3, 1, 0, 0);
        });

    auto rebuild_draw_resources =
        [&render_graph, &gpu_program, &ctx, &vertex_input, &program_layouts,
         &graphics_pipeline, swap_h]() -> std::expected<void, std::string> {
        if (ctx->swapchain() == VK_NULL_HANDLE ||
            ctx->swapchain_image_views().empty()) {
            return std::unexpected(std::string("交换链无效"));
        }

        render_graph.clear_compiled();
        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
            graphics_pipeline = VK_NULL_HANDLE;
        }

        const auto &v = ctx->swapchain_image_views();
        const auto &im = ctx->swapchain_images();
        render_graph.set_resource_target(
            swap_h,
            vulkan::render_target_from_swapchain_view(
                v[0], ctx->swapchain_format(), ctx->swapchain_width(),
                ctx->swapchain_height()),
            im[0]);

        if (auto c = render_graph.compile(); !c.has_value()) {
            return c;
        }
        const VkRenderPass rp = render_graph.render_pass_for_pass(0);
        if (rp == VK_NULL_HANDLE) {
            return std::unexpected(
                std::string("RenderGraph::render_pass_for_pass(0) 无效"));
        }

        vulkan::GraphicsPipelineBuilder pb { ctx->device() };
        const vulkan::Shader *const vs = gpu_program.vertex_shader();
        const vulkan::Shader *const fs = gpu_program.fragment_shader();
        if (vs == nullptr || fs == nullptr) {
            return std::unexpected(
                std::string("ShaderProgram 缺少顶点或片段着色器"));
        }
        graphics_pipeline =
            pb.set_vertex_shader(vs->shader_module())
                .set_fragment_shader(fs->shader_module())
                .set_vertex_layout(vertex_input.attributes,
                                   vertex_input.bindings)
                .set_viewport_dynamic()
                .set_depth_test(false, false)
                .set_blend_off()
                .set_pipeline_layout(program_layouts.pipeline_layout())
                .set_render_pass(rp)
                .build();
        if (graphics_pipeline == VK_NULL_HANDLE) {
            return std::unexpected(
                std::string("GraphicsPipelineBuilder::build 失败"));
        }
        return {};
    };

    if (auto r = rebuild_draw_resources(); !r.has_value()) {
        std::println(stderr, "初始化绘制资源: {}", r.error());
        return 1;
    }

    auto cmd_pool_result = vulkan::CommandPool::create(
        ctx->device(), ctx->graphics_queue_family(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    if (!cmd_pool_result.has_value()) {
        std::println(stderr, "CommandPool: {}", cmd_pool_result.error());
        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
        }
        return 1;
    }
    vulkan::CommandPool command_pool = std::move(*cmd_pool_result);

    auto cmd_bufs_result = command_pool.allocate_primary(k_frames_in_flight);
    if (!cmd_bufs_result.has_value()) {
        std::println(stderr, "allocate_primary: {}", cmd_bufs_result.error());
        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
        }
        return 1;
    }
    std::vector<VkCommandBuffer> command_buffers = std::move(*cmd_bufs_result);

    auto frame_slots_result =
        vulkan::create_frame_in_flight_slots(ctx->device(), k_frames_in_flight);
    if (!frame_slots_result.has_value()) {
        std::println(stderr, "create_frame_in_flight_slots: {}",
                     frame_slots_result.error());
        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
        }
        return 1;
    }
    std::vector<vulkan::FrameInFlightSlot> frame_slots =
        std::move(*frame_slots_result);

    vulkan::PerImageSemaphores present_finished {};
    if (auto pr = present_finished.sync_count(
            ctx->device(), ctx->swapchain_image_views().size());
        !pr.has_value()) {
        std::println(stderr, "present_finished.sync_count: {}", pr.error());
        if (graphics_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
        }
        return 1;
    }

    std::vector<VkFence> images_in_flight;
    std::uint32_t current_frame { 0 };
    auto start_time = std::chrono::steady_clock::now();

    bool running { true };
    while (running) {
        running = window.poll_events();

        int cur_fb_w { 0 };
        int cur_fb_h { 0 };
        window.get_framebuffer_size(&cur_fb_w, &cur_fb_h);
        if (cur_fb_w > 0 && cur_fb_h > 0 &&
            (static_cast<std::uint32_t>(cur_fb_w) != ctx->swapchain_width() ||
             static_cast<std::uint32_t>(cur_fb_h) != ctx->swapchain_height())) {
            if (auto rec = ctx->recreate_swapchain(
                    static_cast<std::uint32_t>(cur_fb_w),
                    static_cast<std::uint32_t>(cur_fb_h));
                rec.has_value()) {
                if (auto br = rebuild_draw_resources(); !br.has_value()) {
                    LUMEN_LOG_ERROR("重建绘制资源: {}", br.error());
                }
                images_in_flight.assign(ctx->swapchain_image_views().size(),
                                        VK_NULL_HANDLE);
                if (auto sy = present_finished.sync_count(
                        ctx->device(), ctx->swapchain_image_views().size());
                    !sy.has_value()) {
                    LUMEN_LOG_ERROR("present_finished.sync_count: {}",
                                    sy.error());
                }
            } else {
                LUMEN_LOG_ERROR("recreate_swapchain: {}", rec.error());
            }
        }

        if (ctx->swapchain_width() == 0 || ctx->swapchain_height() == 0) {
            continue;
        }
        if (graphics_pipeline == VK_NULL_HANDLE) {
            continue;
        }

        if (auto sy = present_finished.sync_count(
                ctx->device(), ctx->swapchain_image_views().size());
            !sy.has_value()) {
            LUMEN_LOG_ERROR("present_finished.sync_count: {}", sy.error());
            break;
        }

        VkFence frame_fence = frame_slots[current_frame].inflight_fence.get();
        vkWaitForFences(ctx->device(), 1, &frame_fence, VK_TRUE, UINT64_MAX);

        std::uint32_t image_index { 0 };
        VkSemaphore acquire_sem =
            frame_slots[current_frame].image_available.get();
        const VkResult acquire = vkAcquireNextImageKHR(
            ctx->device(), ctx->swapchain(), UINT64_MAX, acquire_sem,
            VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            if (auto rec = ctx->recreate_swapchain(ctx->swapchain_width(),
                                                   ctx->swapchain_height());
                rec.has_value()) {
                if (auto br = rebuild_draw_resources(); !br.has_value()) {
                    LUMEN_LOG_ERROR("重建绘制资源: {}", br.error());
                }
                images_in_flight.assign(ctx->swapchain_image_views().size(),
                                        VK_NULL_HANDLE);
                if (auto sy = present_finished.sync_count(
                        ctx->device(), ctx->swapchain_image_views().size());
                    !sy.has_value()) {
                    LUMEN_LOG_ERROR("present_finished.sync_count: {}",
                                    sy.error());
                }
            }
            continue;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            LUMEN_LOG_ERROR("vkAcquireNextImageKHR ec={}",
                            static_cast<int>(acquire));
            break;
        }

        if (images_in_flight.size() != ctx->swapchain_image_views().size()) {
            images_in_flight.assign(ctx->swapchain_image_views().size(),
                                    VK_NULL_HANDLE);
        }

        if (images_in_flight[image_index] != VK_NULL_HANDLE) {
            vkWaitForFences(ctx->device(), 1, &images_in_flight[image_index],
                            VK_TRUE, UINT64_MAX);
        }
        images_in_flight[image_index] = frame_fence;

        vkResetFences(ctx->device(), 1, &frame_fence);

        const auto elapsed = std::chrono::steady_clock::now() - start_time;
        const float t = std::chrono::duration<float>(elapsed).count();
        const float aspect =
            static_cast<float>(ctx->swapchain_width()) /
            std::max(1.F, static_cast<float>(ctx->swapchain_height()));
        vulkan::UboViewProj ubo_data {};
        ubo_data.proj =
            glm::ortho(-aspect, aspect, -1.F, 1.F, -1.F, 1.F);
        ubo_data.view = glm::rotate(glm::mat4(1.F), t * glm::radians(90.F),
                                    glm::vec3(0.F, 0.F, 1.F));
        if (auto ubo_map = ubo_buffer.map(); ubo_map.has_value()) {
            std::memcpy(ubo_map.value(), &ubo_data, sizeof(ubo_data));
            ubo_buffer.unmap();
        } else {
            LUMEN_LOG_ERROR("UBO map: {}", ubo_map.error());
            break;
        }

        const auto &views = ctx->swapchain_image_views();
        const auto &imgs = ctx->swapchain_images();
        render_graph.set_resource_target(
            swap_h,
            vulkan::render_target_from_swapchain_view(
                views[image_index], ctx->swapchain_format(),
                ctx->swapchain_width(), ctx->swapchain_height()),
            imgs[image_index]);

        if (auto pf = render_graph.prepare_frame(ctx->device());
            !pf.has_value()) {
            LUMEN_LOG_ERROR("RenderGraph::prepare_frame: {}", pf.error());
            break;
        }

        vkResetCommandBuffer(command_buffers[current_frame], 0);

        VkCommandBufferBeginInfo begin_info {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffers[current_frame], &begin_info);

        if (auto ex = render_graph.execute(command_buffers[current_frame],
                                           ctx->device());
            !ex.has_value()) {
            LUMEN_LOG_ERROR("RenderGraph::execute: {}", ex.error());
            break;
        }

        vkEndCommandBuffer(command_buffers[current_frame]);

        VkPipelineStageFlags wait_stage {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        VkSemaphore signal_sem = present_finished.get(image_index);
        VkSubmitInfo submit_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &acquire_sem;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[current_frame];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &signal_sem;

        if (vkQueueSubmit(ctx->graphics_queue(), 1, &submit_info,
                          frame_fence) != VK_SUCCESS) {
            LUMEN_LOG_ERROR("vkQueueSubmit 失败");
            break;
        }

        VkSwapchainKHR swapchain = ctx->swapchain();
        VkPresentInfoKHR present_info { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &signal_sem;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.pImageIndices = &image_index;

        const VkResult present =
            vkQueuePresentKHR(ctx->present_queue(), &present_info);
        if (present == VK_ERROR_OUT_OF_DATE_KHR ||
            present == VK_SUBOPTIMAL_KHR) {
            int pw { 0 };
            int ph { 0 };
            window.get_framebuffer_size(&pw, &ph);
            if (pw > 0 && ph > 0) {
                if (auto rec =
                        ctx->recreate_swapchain(static_cast<std::uint32_t>(pw),
                                                static_cast<std::uint32_t>(ph));
                    rec.has_value()) {
                    if (auto br = rebuild_draw_resources(); !br.has_value()) {
                        LUMEN_LOG_ERROR("重建绘制资源: {}", br.error());
                    }
                    images_in_flight.assign(ctx->swapchain_image_views().size(),
                                            VK_NULL_HANDLE);
                    if (auto sy = present_finished.sync_count(
                            ctx->device(), ctx->swapchain_image_views().size());
                        !sy.has_value()) {
                        LUMEN_LOG_ERROR("present_finished.sync_count: {}",
                                        sy.error());
                    }
                }
            }
        } else if (present != VK_SUCCESS) {
            LUMEN_LOG_ERROR("vkQueuePresentKHR ec={}",
                            static_cast<int>(present));
            break;
        }

        current_frame = (current_frame + 1) % k_frames_in_flight;
    }

    vkDeviceWaitIdle(ctx->device());

    if (graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx->device(), graphics_pipeline, nullptr);
    }
    return 0;
}
