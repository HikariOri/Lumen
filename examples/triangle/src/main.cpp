/**
 * @file main.cpp
 * @brief Vulkan 三角形示例：`platform::Window` + `EventPump`，其余为最小自管
 * Vulkan
 */

#include <VkBootstrap.h>

#include <vector>

#include "core/log/logger.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "vulkan/pipeline.hpp"

namespace {

void app_log_err(const std::string &msg) {
    if (auto lg = core::log::Logger::app()) {
        lg->error(msg);
    }
}

constexpr uint32_t k_frames_in_flight = 2;

struct UniformBufferObject {
    float time { 0.f };
};

struct Vertex {
    float position[2];
    float color[3];
};

const Vertex k_vertices[] = {
    { { 0.f, -0.5f }, { 1.f, 0.f, 0.f } },
    { { 0.5f, 0.5f }, { 0.f, 1.f, 0.f } },
    { { -0.5f, 0.5f }, { 0.f, 0.f, 1.f } },
};

[[nodiscard]] std::vector<uint32_t>
read_spirv(const ghc::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const auto size = f.tellg();
    if (size <= 0 || (static_cast<size_t>(size) % sizeof(uint32_t)) != 0U) {
        return {};
    }
    std::vector<uint32_t> code(static_cast<size_t>(size / sizeof(uint32_t)));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(code.data()), size);
    return code;
}

[[nodiscard]] uint32_t find_memory_type(VkPhysicalDevice phys,
                                        uint32_t type_filter,
                                        VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem {};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    for (uint32_t i { 0 }; i < mem.memoryTypeCount; ++i) {
        if ((type_filter & (1U << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

/** @brief 着色器目录（可执行文件旁 `shaders/`）。SDL3 下 `SDL_GetBasePath`
 * 返回只读串，由 SDL 持有，勿 SDL_free。 */
[[nodiscard]] ghc::filesystem::path base_path_shaders_dir() {
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
        return ghc::filesystem::path("shaders");
    }
    return ghc::filesystem::path(base) / "shaders";
}

class TriangleExample {
public:
    bool init(lumen::platform::Window &window) {
        window_ = &window;

        auto vert_code =
            read_spirv(base_path_shaders_dir() / "triangle.vert.spv");
        auto frag_code =
            read_spirv(base_path_shaders_dir() / "triangle.frag.spv");
        if (vert_code.empty() || frag_code.empty()) {
            LUMEN_APP_LOG_ERROR(
                "SPIR-V load failed (triangle.*.spv next to executable)");
            return false;
        }
        vert_spirv_ = std::move(vert_code);
        frag_spirv_ = std::move(frag_code);

        vkb::InstanceBuilder inst_builder;
        inst_builder.set_app_name("triangle")
            .set_engine_name("lumen")
            .set_app_version(1, 0, 0);
        const auto sdl_exts = window_->get_vulkan_instance_extensions();
        inst_builder.set_headless(true).enable_extensions(sdl_exts);

#if !defined(NDEBUG)
        inst_builder.request_validation_layers().use_default_debug_messenger();
#endif
        inst_builder.require_api_version(1, 2, 0);

        const auto inst_ret = inst_builder.build();
        if (!inst_ret) {
            app_log_err(std::string("VkInstance failed ec=") +
                        std::to_string(inst_ret.error().value()));
            return false;
        }
        vkb_instance_ = inst_ret.value();

        surface_ = window_->create_vulkan_surface(vkb_instance_.instance);
        if (surface_ == VK_NULL_HANDLE) {
            LUMEN_APP_LOG_ERROR("Vulkan surface create failed");
            return false;
        }

        vkb::PhysicalDeviceSelector phys_selector { vkb_instance_, surface_ };
        phys_selector.set_minimum_version(1, 2)
            .set_surface(surface_)
            .require_present(true);
        const auto phys_ret = phys_selector.select();
        if (!phys_ret) {
            app_log_err(std::string("PhysicalDevice select failed ec=") +
                        std::to_string(phys_ret.error().value()));
            return false;
        }
        vkb_phys_ = phys_ret.value();

        vkb::DeviceBuilder dev_builder { vkb_phys_ };
        const auto dev_ret = dev_builder.build();
        if (!dev_ret) {
            app_log_err(std::string("VkDevice failed ec=") +
                        std::to_string(dev_ret.error().value()));
            return false;
        }
        vkb_device_ = dev_ret.value();
        device_ = vkb_device_.device;

        const auto gq = vkb_device_.get_queue(vkb::QueueType::graphics);
        const auto pq = vkb_device_.get_queue(vkb::QueueType::present);
        if (!gq || !pq) {
            LUMEN_APP_LOG_ERROR("Graphics or present queue unavailable");
            return false;
        }
        graphics_queue_ = gq.value();
        present_queue_ = pq.value();

        if (!create_swapchain_internal(VK_NULL_HANDLE)) {
            return false;
        }
        if (!create_render_pass()) {
            return false;
        }
        if (!create_pipeline()) {
            return false;
        }
        if (!create_framebuffer()) {
            return false;
        }
        if (!create_vertex_buffer()) {
            return false;
        }
        if (!create_uniform_buffers()) {
            return false;
        }
        if (!create_descriptor_pool_and_sets()) {
            return false;
        }
        if (!create_command_pool_and_buffers()) {
            return false;
        }
        if (!create_sync()) {
            return false;
        }
        return true;
    }

    void shutdown() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        vkDeviceWaitIdle(device_);

        destroy_sync();
        destroy_command();

        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }

        for (auto b : uniform_buffers_) {
            if (b.map != nullptr) {
                vkUnmapMemory(device_, b.mem);
            }
            if (b.buf != VK_NULL_HANDLE) {
                vkDestroyBuffer(device_, b.buf, nullptr);
            }
            if (b.mem != VK_NULL_HANDLE) {
                vkFreeMemory(device_, b.mem, nullptr);
            }
        }
        uniform_buffers_.clear();

        if (vertex_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, vertex_buffer_, nullptr);
            vertex_buffer_ = VK_NULL_HANDLE;
        }
        if (vertex_buffer_mem_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, vertex_buffer_mem_, nullptr);
            vertex_buffer_mem_ = VK_NULL_HANDLE;
        }

        cleanup_swapchain();

        vkb::destroy_device(vkb_device_);

        if (surface_ != VK_NULL_HANDLE) {
            vkb::destroy_surface(vkb_instance_, surface_);
            surface_ = VK_NULL_HANDLE;
        }

        vkb::destroy_instance(vkb_instance_);
        device_ = VK_NULL_HANDLE;
    }

    void set_framebuffer_resized() { framebuffer_resized_ = true; }

    void draw_frame(float time_sec) {
        if (swapchain_ == VK_NULL_HANDLE || extent_.width == 0 ||
            extent_.height == 0) {
            return;
        }

        vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE,
                        UINT64_MAX);

        uint32_t image_index { 0 };
        const VkResult acq = vkAcquireNextImageKHR(
            device_, swapchain_, UINT64_MAX, image_available_[current_frame_],
            VK_NULL_HANDLE, &image_index);

        if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) {
            framebuffer_resized_ = true;
        }
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            try_recreate_swapchain();
            return;
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            app_log_err(std::string("vkAcquireNextImageKHR: ") +
                        std::to_string(static_cast<int>(acq)));
            return;
        }

        if (images_in_flight_[image_index] != VK_NULL_HANDLE) {
            vkWaitForFences(device_, 1, &images_in_flight_[image_index],
                            VK_TRUE, UINT64_MAX);
        }
        images_in_flight_[image_index] = in_flight_fences_[current_frame_];

        {
            UniformBufferObject ubo { .time = time_sec };
            auto &ub = uniform_buffers_[current_frame_];
            std::memcpy(ub.map, &ubo, sizeof(ubo));
        }

        record_command_buffer(image_index, current_frame_);

        vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo sub_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        sub_info.waitSemaphoreCount = 1;
        sub_info.pWaitSemaphores = &image_available_[current_frame_];
        sub_info.pWaitDstStageMask = &wait_stage;
        sub_info.commandBufferCount = 1;
        sub_info.pCommandBuffers = &command_buffers_[image_index];
        // 与 image_index 对应：present 在图像再次 acquire
        // 前可能一直占用该信号量， 不能按 current_frame_ 复用同一
        // render_finished（VUID-vkQueueSubmit-pSignalSemaphores-00067）。
        sub_info.signalSemaphoreCount = 1;
        sub_info.pSignalSemaphores = &render_finished_[image_index];

        if (vkQueueSubmit(graphics_queue_, 1, &sub_info,
                          in_flight_fences_[current_frame_]) != VK_SUCCESS) {
            LUMEN_APP_LOG_ERROR("vkQueueSubmit failed");
            return;
        }

        VkPresentInfoKHR present { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished_[image_index];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index;

        const VkResult pr = vkQueuePresentKHR(present_queue_, &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            framebuffer_resized_ = true;
        } else if (pr != VK_SUCCESS) {
            app_log_err(std::string("vkQueuePresentKHR: ") +
                        std::to_string(static_cast<int>(pr)));
        }

        current_frame_ = (current_frame_ + 1U) % k_frames_in_flight;
    }

    void try_recreate_swapchain() {
        if (!framebuffer_resized_) {
            return;
        }
        int w { 0 };
        int h { 0 };
        window_->get_framebuffer_size(&w, &h);
        if (w == 0 || h == 0) {
            return;
        }

        vkDeviceWaitIdle(device_);
        framebuffer_resized_ = false;

        cleanup_swapchain_only();

        if (!create_swapchain_internal(VK_NULL_HANDLE)) {
            LUMEN_APP_LOG_ERROR("Swapchain recreate failed");
            return;
        }

        if (!create_framebuffer()) {
            LUMEN_APP_LOG_ERROR("Framebuffer recreate failed");
            return;
        }

        destroy_render_finished_semaphores_();
        if (!create_render_finished_semaphores_()) {
            LUMEN_APP_LOG_ERROR("render_finished semaphores recreate failed");
            return;
        }

        destroy_command();
        if (!create_command_pool_and_buffers()) {
            LUMEN_APP_LOG_ERROR("Command pool recreate failed");
            return;
        }

        images_in_flight_.assign(swap_image_views_.size(), VK_NULL_HANDLE);
    }

private:
    struct HostUniformBuffer {
        VkBuffer buf { VK_NULL_HANDLE };
        VkDeviceMemory mem { VK_NULL_HANDLE };
        void *map { nullptr };
    };

    bool create_swapchain_internal(VkSwapchainKHR old_swapchain) {
        vkb::SwapchainBuilder sc_builder { vkb_device_ };
        sc_builder.set_old_swapchain(old_swapchain);
        const auto sc_ret = sc_builder.build();
        if (!sc_ret) {
            app_log_err(std::string("Swapchain create failed ec=") +
                        std::to_string(sc_ret.error().value()));
            return false;
        }
        vkb_swapchain_ = sc_ret.value();
        swapchain_ = vkb_swapchain_.swapchain;
        extent_ = vkb_swapchain_.extent;
        swap_format_ = vkb_swapchain_.image_format;

        const auto views_ret = vkb_swapchain_.get_image_views();
        if (!views_ret) {
            LUMEN_APP_LOG_ERROR("Swapchain image views failed");
            return false;
        }
        swap_image_views_ = views_ret.value();
        return true;
    }

    void cleanup_swapchain_only() {
        for (auto fb : framebuffers_) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device_, fb, nullptr);
            }
        }
        framebuffers_.clear();

        if (!swap_image_views_.empty()) {
            vkb_swapchain_.destroy_image_views(swap_image_views_);
            swap_image_views_.clear();
        }

        if (swapchain_ != VK_NULL_HANDLE) {
            vkb::destroy_swapchain(vkb_swapchain_);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void cleanup_swapchain() {
        cleanup_swapchain_only();

        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
            render_pass_ = VK_NULL_HANDLE;
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_,
                                         nullptr);
            descriptor_set_layout_ = VK_NULL_HANDLE;
        }
    }

    bool create_render_pass() {
        VkAttachmentDescription color {};
        color.format = swap_format_;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref {};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep { VK_SUBPASS_EXTERNAL, 0 };
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        rp.dependencyCount = 1;
        rp.pDependencies = &dep;

        return vkCreateRenderPass(device_, &rp, nullptr, &render_pass_) ==
               VK_SUCCESS;
    }

    bool create_pipeline() {
        VkDescriptorSetLayoutBinding uboBind {};
        uboBind.binding = 0;
        uboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBind.descriptorCount = 1;
        uboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsl {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        dsl.bindingCount = 1;
        dsl.pBindings = &uboBind;
        if (vkCreateDescriptorSetLayout(device_, &dsl, nullptr,
                                        &descriptor_set_layout_) !=
            VK_SUCCESS) {
            return false;
        }

        VkPipelineLayoutCreateInfo pl {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &descriptor_set_layout_;
        if (vkCreatePipelineLayout(device_, &pl, nullptr, &pipeline_layout_) !=
            VK_SUCCESS) {
            return false;
        }

        const std::vector<VkVertexInputBindingDescription> vertex_bindings {
            VkVertexInputBindingDescription {
                .binding = 0,
                .stride = sizeof(Vertex),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
        };
        const std::vector<VkVertexInputAttributeDescription> vertex_attributes {
            VkVertexInputAttributeDescription {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = offsetof(Vertex, position),
            },
            VkVertexInputAttributeDescription {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, color),
            },
        };

        vulkan::GraphicsPipelineBuilder pipelineBuilder(device_);
        pipelineBuilder.set_vertex_shader(vert_spirv_)
            .set_fragment_shader(frag_spirv_)
            .set_vertex_layout(vertex_attributes, vertex_bindings)
            .set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .set_cull_mode(VK_CULL_MODE_NONE)
            .set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .set_viewport_dynamic()
            .set_depth_test(false, false)
            .set_blend_off()
            .set_pipeline_layout(pipeline_layout_)
            .set_render_pass(render_pass_, 0);

        pipeline_ = pipelineBuilder.build();
        return pipeline_ != VK_NULL_HANDLE;
    }

    bool create_framebuffer() {
        framebuffers_.resize(swap_image_views_.size());
        for (size_t i { 0 }; i < swap_image_views_.size(); ++i) {
            VkFramebufferCreateInfo fb {
                VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
            };
            fb.renderPass = render_pass_;
            fb.attachmentCount = 1;
            fb.pAttachments = &swap_image_views_[i];
            fb.width = extent_.width;
            fb.height = extent_.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(device_, &fb, nullptr, &framebuffers_[i]) !=
                VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    bool create_vertex_buffer() {
        VkBufferCreateInfo bi { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = sizeof(k_vertices);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bi, nullptr, &vertex_buffer_) !=
            VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements req {};
        vkGetBufferMemoryRequirements(device_, vertex_buffer_, &req);
        const uint32_t mem_index =
            find_memory_type(vkb_phys_, req.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mem_index == UINT32_MAX) {
            return false;
        }
        VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = mem_index;
        if (vkAllocateMemory(device_, &ai, nullptr, &vertex_buffer_mem_) !=
            VK_SUCCESS) {
            return false;
        }
        void *dst { nullptr };
        vkMapMemory(device_, vertex_buffer_mem_, 0, sizeof(k_vertices), 0,
                    &dst);
        std::memcpy(dst, k_vertices, sizeof(k_vertices));
        vkUnmapMemory(device_, vertex_buffer_mem_);
        vkBindBufferMemory(device_, vertex_buffer_, vertex_buffer_mem_, 0);
        return true;
    }

    bool create_uniform_buffers() {
        uniform_buffers_.resize(k_frames_in_flight);
        for (auto &ub : uniform_buffers_) {
            VkBufferCreateInfo bi { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bi.size = sizeof(UniformBufferObject);
            bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device_, &bi, nullptr, &ub.buf) != VK_SUCCESS) {
                return false;
            }
            VkMemoryRequirements req {};
            vkGetBufferMemoryRequirements(device_, ub.buf, &req);
            const uint32_t mem_index =
                find_memory_type(vkb_phys_, req.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (mem_index == UINT32_MAX) {
                return false;
            }
            VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = mem_index;
            if (vkAllocateMemory(device_, &ai, nullptr, &ub.mem) !=
                VK_SUCCESS) {
                return false;
            }
            vkBindBufferMemory(device_, ub.buf, ub.mem, 0);
            vkMapMemory(device_, ub.mem, 0, sizeof(UniformBufferObject), 0,
                        &ub.map);
        }
        return true;
    }

    bool create_descriptor_pool_and_sets() {
        VkDescriptorPoolSize pool_size {};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = k_frames_in_flight;

        VkDescriptorPoolCreateInfo pci {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        pci.maxSets = k_frames_in_flight;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device_, &pci, nullptr, &descriptor_pool_) !=
            VK_SUCCESS) {
            return false;
        }

        std::vector<VkDescriptorSetLayout> layouts(k_frames_in_flight,
                                                   descriptor_set_layout_);
        VkDescriptorSetAllocateInfo ai {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        ai.descriptorPool = descriptor_pool_;
        ai.descriptorSetCount = k_frames_in_flight;
        ai.pSetLayouts = layouts.data();
        descriptor_sets_.resize(k_frames_in_flight);
        if (vkAllocateDescriptorSets(device_, &ai, descriptor_sets_.data()) !=
            VK_SUCCESS) {
            return false;
        }

        for (uint32_t i { 0 }; i < k_frames_in_flight; ++i) {
            VkDescriptorBufferInfo binfo {};
            binfo.buffer = uniform_buffers_[i].buf;
            binfo.offset = 0;
            binfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet write {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
            };
            write.dstSet = descriptor_sets_[i];
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &binfo;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
        return true;
    }

    bool create_command_pool_and_buffers() {
        const auto qf = vkb_device_.get_queue_index(vkb::QueueType::graphics);
        if (!qf) {
            return false;
        }
        VkCommandPoolCreateInfo pci {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = qf.value();
        if (vkCreateCommandPool(device_, &pci, nullptr, &command_pool_) !=
            VK_SUCCESS) {
            return false;
        }

        command_buffers_.resize(swap_image_views_.size());
        if (command_buffers_.empty()) {
            return false;
        }
        VkCommandBufferAllocateInfo ai {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        ai.commandPool = command_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());
        return vkAllocateCommandBuffers(device_, &ai,
                                        command_buffers_.data()) == VK_SUCCESS;
    }

    void destroy_command() {
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        command_buffers_.clear();
    }

    void record_command_buffer(uint32_t image_index, uint32_t frame_index) {
        VkCommandBuffer cb = command_buffers_[image_index];
        vkResetCommandBuffer(cb, 0);

        VkCommandBufferBeginInfo bi {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        vkBeginCommandBuffer(cb, &bi);

        VkClearValue clear {};
        clear.color = { { 0.05f, 0.06f, 0.09f, 1.f } };
        VkRenderPassBeginInfo rp { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = render_pass_;
        rp.framebuffer = framebuffers_[image_index];
        rp.renderArea.extent = extent_;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        VkViewport viewport {};
        viewport.width = static_cast<float>(extent_.width);
        viewport.height = static_cast<float>(extent_.height);
        viewport.maxDepth = 1.f;
        vkCmdSetViewport(cb, Offset, 1, &viewport);

        VkRect2D scissor {};
        scissor.extent = extent_;
        vkCmdSetScissor(cb, Offset, 1, &scissor);

        const VkDeviceSize vb_offset { 0 };
        vkCmdBindVertexBuffers(cb, 0, 1, &vertex_buffer_, &vb_offset);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1,
                                &descriptor_sets_[frame_index], 0, nullptr);

        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    bool create_sync() {
        image_available_.resize(k_frames_in_flight);
        in_flight_fences_.resize(k_frames_in_flight);
        images_in_flight_.assign(swap_image_views_.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo sci { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fci { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i { 0 }; i < k_frames_in_flight; ++i) {
            if (vkCreateSemaphore(device_, &sci, nullptr,
                                  &image_available_[i]) != VK_SUCCESS ||
                vkCreateFence(device_, &fci, nullptr, &in_flight_fences_[i]) !=
                    VK_SUCCESS) {
                return false;
            }
        }
        return create_render_finished_semaphores_();
    }

    void destroy_render_finished_semaphores_() {
        for (VkSemaphore sem : render_finished_) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, sem, nullptr);
            }
        }
        render_finished_.clear();
    }

    bool create_render_finished_semaphores_() {
        const size_t imageCount = swap_image_views_.size();
        render_finished_.resize(imageCount);
        VkSemaphoreCreateInfo sci { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        for (size_t i { 0 }; i < imageCount; ++i) {
            if (vkCreateSemaphore(device_, &sci, nullptr,
                                  &render_finished_[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    void destroy_sync() {
        for (uint32_t i { 0 }; i < k_frames_in_flight; ++i) {
            if (image_available_.size() > i &&
                image_available_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, image_available_[i], nullptr);
            }
            if (in_flight_fences_.size() > i &&
                in_flight_fences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            }
        }
        image_available_.clear();
        in_flight_fences_.clear();
        destroy_render_finished_semaphores_();
        images_in_flight_.clear();
    }

    static constexpr uint32_t Offset = 0U;

    lumen::platform::Window *window_ { nullptr };

    std::vector<uint32_t> vert_spirv_;
    std::vector<uint32_t> frag_spirv_;

    vkb::Instance vkb_instance_;
    vkb::PhysicalDevice vkb_phys_;
    vkb::Device vkb_device_;
    VkSurfaceKHR surface_ { VK_NULL_HANDLE };

    vkb::Swapchain vkb_swapchain_;
    VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
    std::vector<VkImageView> swap_image_views_;
    VkFormat swap_format_ { VK_FORMAT_UNDEFINED };
    VkExtent2D extent_ {};

    VkRenderPass render_pass_ { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout_ { VK_NULL_HANDLE };
    VkPipeline pipeline_ { VK_NULL_HANDLE };
    VkDescriptorSetLayout descriptor_set_layout_ { VK_NULL_HANDLE };
    std::vector<VkFramebuffer> framebuffers_;

    VkBuffer vertex_buffer_ { VK_NULL_HANDLE };
    VkDeviceMemory vertex_buffer_mem_ { VK_NULL_HANDLE };
    std::vector<HostUniformBuffer> uniform_buffers_;

    VkDescriptorPool descriptor_pool_ { VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> descriptor_sets_;

    VkCommandPool command_pool_ { VK_NULL_HANDLE };
    std::vector<VkCommandBuffer> command_buffers_;

    std::vector<VkSemaphore> image_available_;
    /// 每帧 swapchain 图像各一个，与 acquire 得到的 image_index 对应（避免与
    /// present 冲突）。
    std::vector<VkSemaphore> render_finished_;
    std::vector<VkFence> in_flight_fences_;
    std::vector<VkFence> images_in_flight_;

    VkDevice device_ { VK_NULL_HANDLE };
    VkQueue graphics_queue_ { VK_NULL_HANDLE };
    VkQueue present_queue_ { VK_NULL_HANDLE };

    uint32_t current_frame_ { 0 };
    bool framebuffer_resized_ { false };
};

} // namespace

int main() {
    if (!core::log::Logger::init()) {
        return 1;
    }

    lumen::platform::Window window;
    lumen::platform::WindowConfig cfg;
    cfg.title = "Lumen 三角形示例";
    cfg.width = 960;
    cfg.height = 540;
    if (!window.create(cfg)) {
        core::log::Logger::shutdown();
        return 1;
    }

    TriangleExample app;
    if (!app.init(window)) {
        core::log::Logger::shutdown();
        return 1;
    }

    lumen::platform::EventPump pump;
    pump.set_on_application_event([&](lumen::platform::DispatchableEvent &de) {
        lumen::platform::EventDispatcher d(de);
        d.dispatch<lumen::platform::EventWindowResize>(
            [&](lumen::platform::EventWindowResize &) {
                app.set_framebuffer_resized();
            });
    });

    auto start = std::chrono::steady_clock::now();
    while (pump.poll()) {
        app.try_recreate_swapchain();
        const auto now = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(now - start).count();
        app.draw_frame(t);
    }

    app.shutdown();
    core::log::Logger::shutdown();
    SDL_Quit();
    return 0;
}
