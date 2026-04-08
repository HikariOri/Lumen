/**
 * @file main.cpp
 * @brief Vulkan 旋转矩形 + 纹理采样示例：`platform::Window` +
 * `EventPump`；交换链路径使用 `RenderTargetBundle` + `RenderPass`。
 */

#include <optional>

#include "core/log/logger.hpp"
#include "platform/event_dispatcher.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "vulkan/buffer.hpp"
#include "vulkan/context.hpp"
#include "vulkan/image.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/render_pass.hpp"
#include "vulkan/render_target.hpp"
#include "vulkan/render_target_bundle.hpp"
#include "vulkan/shader_reflection.hpp"

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
    float uv[2];
    float color[3];
};

/** 轴对齐矩形（NDC）+ UV；纹理顶行对应 v=0（与 stb 行序一致）。 */
const Vertex k_vertices[] = {
    // 三角形 1：左下、右下、左上
    { { -0.45f, -0.28f }, { 0.f, 1.f }, { 1.f, 1.f, 1.f } },
    { { 0.45f, -0.28f }, { 1.f, 1.f }, { 1.f, 1.f, 1.f } },
    { { -0.45f, 0.28f }, { 0.f, 0.f }, { 1.f, 1.f, 1.f } },
    // 三角形 2
    { { 0.45f, -0.28f }, { 1.f, 1.f }, { 1.f, 1.f, 1.f } },
    { { 0.45f, 0.28f }, { 1.f, 0.f }, { 1.f, 1.f, 1.f } },
    { { -0.45f, 0.28f }, { 0.f, 0.f }, { 1.f, 1.f, 1.f } },
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

/** @brief 着色器目录（可执行文件旁 `shaders/`）。SDL3 下 `SDL_GetBasePath`
 * 返回只读串，由 SDL 持有，勿 SDL_free。 */
[[nodiscard]] ghc::filesystem::path base_path_shaders_dir() {
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
        return ghc::filesystem::path("shaders");
    }
    return ghc::filesystem::path(base) / "shaders";
}

/** @brief 可执行文件旁 `assets/`（CMake post-build 拷贝纹理目录）。 */
[[nodiscard]] ghc::filesystem::path base_path_assets_dir() {
    const char *base = SDL_GetBasePath();
    if (base == nullptr) {
        return ghc::filesystem::path("assets");
    }
    return ghc::filesystem::path(base) / "assets";
}

class RotatingRectExample {
public:
    bool init(lumen::platform::Window &window) {
        window_ = &window;

        auto vertCode =
            read_spirv(base_path_shaders_dir() / "triangle.vert.spv");
        auto fragCode =
            read_spirv(base_path_shaders_dir() / "triangle.frag.spv");
        if (vertCode.empty() || fragCode.empty()) {
            LUMEN_APP_LOG_ERROR(
                "SPIR-V load failed (triangle.*.spv next to executable)");
            return false;
        }
        vert_spirv_ = std::move(vertCode);
        frag_spirv_ = std::move(fragCode);

        int fbw { 0 };
        int fbh { 0 };
        window_->get_framebuffer_size(&fbw, &fbh);
        if (fbw <= 0 || fbh <= 0) {
            fbw = static_cast<int>(window_->width());
            fbh = static_cast<int>(window_->height());
        }
        const auto sdl_exts = window_->get_vulkan_instance_extensions();
        const bool enable_validation =
#if defined(NDEBUG)
            false;
#else
            true;
#endif
        auto ctx_ret =
            vulkan::ContextBuilder()
                .set_application_name("rotating-rect")
                .set_application_version(VK_MAKE_API_VERSION(0, 1, 0, 0))
                .set_instance_extensions(sdl_exts)
                .set_initial_size(static_cast<std::uint32_t>(fbw),
                                  static_cast<std::uint32_t>(fbh))
                .set_surface_from_instance([this](VkInstance inst) {
                    return window_->create_vulkan_surface(inst);
                })
                .set_enable_validation(enable_validation)
                .build();
        if (!ctx_ret) {
            app_log_err(ctx_ret.error());
            return false;
        }
        vulkan_ctx_ = std::move(ctx_ret.value());
        sync_vulkan_handles_from_context();
        if (!create_render_pass()) {
            return false;
        }
        if (!create_texture_()) {
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
        if (vulkan_ctx_ == nullptr) {
            return;
        }
        vkDeviceWaitIdle(device_);

        destroy_sync();
        destroy_command();

        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }

        for (auto &b : uniform_buffers_) {
            if (b.mapped != nullptr) {
                b.buffer.unmap();
                b.mapped = nullptr;
            }
        }
        uniform_buffers_.clear();

        vertex_buffer_ = vulkan::Buffer {};

        destroy_texture_();

        cleanup_swapchain();

        vulkan_ctx_.reset();
        device_ = VK_NULL_HANDLE;
        graphics_queue_ = VK_NULL_HANDLE;
        present_queue_ = VK_NULL_HANDLE;
        swapchain_ = VK_NULL_HANDLE;
        swap_image_views_.clear();
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
            std::memcpy(ub.mapped, &ubo, sizeof(ubo));
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

        if (auto r = vulkan_ctx_->recreate_swapchain(
                static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
            !r) {
            app_log_err(r.error());
            return;
        }
        sync_vulkan_handles_from_context();

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
        vulkan::Buffer buffer;
        void *mapped { nullptr };
    };

    void sync_vulkan_handles_from_context() {
        device_ = vulkan_ctx_->device();
        graphics_queue_ = vulkan_ctx_->graphics_queue();
        present_queue_ = vulkan_ctx_->present_queue();
        swapchain_ = vulkan_ctx_->swapchain();
        swap_format_ = vulkan_ctx_->swapchain_format();
        extent_.width = vulkan_ctx_->swapchain_width();
        extent_.height = vulkan_ctx_->swapchain_height();
        swap_image_views_ = vulkan_ctx_->swapchain_image_views();
    }

    void cleanup_swapchain_only() {
        for (vulkan::RenderTargetBundle &b : swap_target_bundles_) {
            b.destroy(device_);
        }
        swap_target_bundles_.clear();
    }

    void cleanup_swapchain() {
        cleanup_swapchain_only();

        vulkan_render_pass_.reset();
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
        }
        destroy_reflected_descriptor_set_layouts_();
    }

    void destroy_reflected_descriptor_set_layouts_() {
        for (VkDescriptorSetLayout layout : descriptor_set_layouts_) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, layout, nullptr);
            }
        }
        descriptor_set_layouts_.clear();
    }

    bool create_texture_() {
        const ghc::filesystem::path texPath =
            base_path_assets_dir() / "textures" / "ikun2026_happy_new_year.jpg";
        int texW { 0 };
        int texH { 0 };
        int texChannels { 0 };
        stbi_uc *pixels { nullptr };
        pixels =
            stbi_load(texPath.string().c_str(), &texW, &texH, &texChannels, 4);
        if (pixels == nullptr || texW <= 0 || texH <= 0) {
            app_log_err("stbi_load failed: " + texPath.string());
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            return false;
        }

        const std::uint32_t qf = vulkan_ctx_->graphics_queue_family();

        const VkDeviceSize imageBytes =
            static_cast<VkDeviceSize>(texW * texH * 4);

        VmaAllocator allocator { vulkan_ctx_->allocator() };

        vulkan::BufferCreateInfo stagingCi {};
        stagingCi.size = imageBytes;
        stagingCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingCi.memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        auto stagingRet = vulkan::Buffer::create(allocator, stagingCi);
        if (!stagingRet) {
            stbi_image_free(pixels);
            app_log_err(stagingRet.error());
            return false;
        }
        vulkan::Buffer staging = std::move(stagingRet.value());
        {
            auto mapped = staging.map();
            if (!mapped) {
                stbi_image_free(pixels);
                app_log_err(mapped.error());
                return false;
            }
            std::memcpy(*mapped, pixels, static_cast<size_t>(imageBytes));
            staging.unmap();
        }
        stbi_image_free(pixels);

        vulkan::ImageCreateInfo imgCi {};
        imgCi.extent.width = static_cast<std::uint32_t>(texW);
        imgCi.extent.height = static_cast<std::uint32_t>(texH);
        imgCi.extent.depth = 1;
        imgCi.format = VK_FORMAT_R8G8B8A8_SRGB;
        imgCi.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgCi.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
        imgCi.viewDevice = device_;
        imgCi.viewAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        auto imageRet = vulkan::Image::create(allocator, imgCi);
        if (!imageRet) {
            app_log_err(imageRet.error());
            return false;
        }
        vulkan::Image texImage = std::move(imageRet.value());
        const VkImage vkTex = texImage.image();

        VkCommandPool uploadPool { VK_NULL_HANDLE };
        VkCommandPoolCreateInfo cpci {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        cpci.queueFamilyIndex = qf;
        if (vkCreateCommandPool(device_, &cpci, nullptr, &uploadPool) !=
            VK_SUCCESS) {
            return false;
        }

        VkCommandBuffer uploadCb { VK_NULL_HANDLE };
        VkCommandBufferAllocateInfo cbai {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
        };
        cbai.commandPool = uploadPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &cbai, &uploadCb) != VK_SUCCESS) {
            vkDestroyCommandPool(device_, uploadPool, nullptr);
            return false;
        }

        VkCommandBufferBeginInfo begin {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(uploadCb, &begin);

        VkImageMemoryBarrier toCopy {};
        toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toCopy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.image = vkTex;
        toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toCopy.subresourceRange.baseMipLevel = 0;
        toCopy.subresourceRange.levelCount = 1;
        toCopy.subresourceRange.baseArrayLayer = 0;
        toCopy.subresourceRange.layerCount = 1;
        toCopy.srcAccessMask = 0;
        toCopy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(uploadCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &toCopy);

        VkBufferImageCopy region {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent.width = static_cast<uint32_t>(texW);
        region.imageExtent.height = static_cast<uint32_t>(texH);
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(uploadCb, staging.buffer(), vkTex,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);

        VkImageMemoryBarrier toSample {};
        toSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.image = vkTex;
        toSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toSample.subresourceRange.baseMipLevel = 0;
        toSample.subresourceRange.levelCount = 1;
        toSample.subresourceRange.baseArrayLayer = 0;
        toSample.subresourceRange.layerCount = 1;
        toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(uploadCb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &toSample);

        vkEndCommandBuffer(uploadCb);

        VkSubmitInfo sub { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        sub.commandBufferCount = 1;
        sub.pCommandBuffers = &uploadCb;
        if (vkQueueSubmit(graphics_queue_, 1, &sub, VK_NULL_HANDLE) !=
            VK_SUCCESS) {
            vkDestroyCommandPool(device_, uploadPool, nullptr);
            return false;
        }
        vkQueueWaitIdle(graphics_queue_);
        vkDestroyCommandPool(device_, uploadPool, nullptr);

        VkSamplerCreateInfo sci { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.mipLodBias = 0.f;
        sci.anisotropyEnable = VK_FALSE;
        sci.maxAnisotropy = 1.f;
        sci.compareEnable = VK_FALSE;
        sci.minLod = 0.f;
        sci.maxLod = 0.f;
        sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sci.unnormalizedCoordinates = VK_FALSE;
        if (vkCreateSampler(device_, &sci, nullptr, &texture_sampler_) !=
            VK_SUCCESS) {
            return false;
        }

        texture_image_ = std::move(texImage);
        return true;
    }

    void destroy_texture_() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        if (texture_sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, texture_sampler_, nullptr);
            texture_sampler_ = VK_NULL_HANDLE;
        }
        texture_image_ = vulkan::Image {};
    }

    bool create_render_pass() {
        if (swap_image_views_.empty()) {
            app_log_err("create_render_pass: no swapchain image views");
            return false;
        }
        vulkan::RenderTargetBundle template_bundle;
        if (!template_bundle.add_color_target(vulkan::render_target_from_view(
                swap_image_views_.front(), swap_format_, extent_.width,
                extent_.height))) {
            app_log_err("create_render_pass: add_color_target failed");
            return false;
        }
        auto rp = vulkan::RenderPass::create(device_, template_bundle, true);
        if (!rp) {
            app_log_err(rp.error());
            return false;
        }
        vulkan_render_pass_.emplace(std::move(*rp));
        return true;
    }

    bool create_pipeline() {
        shaderPlb_.clear();
        const auto vertReflResult =
            vulkan::reflect_spirv(vert_spirv_, VK_SHADER_STAGE_VERTEX_BIT);
        if (!vertReflResult) {
            app_log_err(vertReflResult.error());
            return false;
        }
        const auto fragReflResult =
            vulkan::reflect_spirv(frag_spirv_, VK_SHADER_STAGE_FRAGMENT_BIT);
        if (!fragReflResult) {
            app_log_err(fragReflResult.error());
            return false;
        }
        const vulkan::ShaderReflection &vertRefl = *vertReflResult;
        const vulkan::ShaderReflection &fragRefl = *fragReflResult;
        if (auto addVert = shaderPlb_.add(vertRefl); !addVert) {
            app_log_err(addVert.error());
            return false;
        }
        if (auto addFrag = shaderPlb_.add(fragRefl); !addFrag) {
            app_log_err(addFrag.error());
            return false;
        }

        destroy_reflected_descriptor_set_layouts_();
        auto layoutsResult = shaderPlb_.create_descriptor_set_layouts(device_);
        if (!layoutsResult) {
            app_log_err(layoutsResult.error());
            return false;
        }
        descriptor_set_layouts_ = std::move(*layoutsResult);

        auto pipelineLayoutResult =
            shaderPlb_.create_pipeline_layout(device_, descriptor_set_layouts_);
        if (!pipelineLayoutResult) {
            app_log_err(pipelineLayoutResult.error());
            vulkan::PipelineLayoutBuilder::destroy_descriptor_set_layouts(
                device_, descriptor_set_layouts_);
            descriptor_set_layouts_.clear();
            return false;
        }
        pipeline_layout_ = *pipelineLayoutResult;

        const auto vertexInputResult =
            vulkan::build_vertex_input_state(vertRefl, 0);
        if (!vertexInputResult) {
            app_log_err(vertexInputResult.error());
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
            destroy_reflected_descriptor_set_layouts_();
            return false;
        }
        const vulkan::VertexInputState &vertexInput = *vertexInputResult;
        if (vertexInput.stride != sizeof(Vertex)) {
            app_log_err(std::string("vertex stride mismatch: reflected=") +
                        std::to_string(vertexInput.stride) +
                        " sizeof(Vertex)=" + std::to_string(sizeof(Vertex)));
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
            destroy_reflected_descriptor_set_layouts_();
            return false;
        }

        vulkan::GraphicsPipelineBuilder pipelineBuilder(device_);
        pipelineBuilder.set_vertex_shader(vert_spirv_)
            .set_fragment_shader(frag_spirv_)
            .set_vertex_layout(vertexInput.attributes, vertexInput.bindings)
            .set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .set_cull_mode(VK_CULL_MODE_NONE)
            .set_front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .set_viewport_dynamic()
            .set_depth_test(false, false)
            .set_blend_off()
            .set_pipeline_layout(pipeline_layout_)
            .set_render_pass(vulkan_render_pass_->vk_render_pass(), 0);

        pipeline_ = pipelineBuilder.build();
        if (pipeline_ == VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            pipeline_layout_ = VK_NULL_HANDLE;
            destroy_reflected_descriptor_set_layouts_();
            return false;
        }
        return true;
    }

    bool create_framebuffer() {
        if (!vulkan_render_pass_.has_value()) {
            app_log_err("create_framebuffer: render pass not created");
            return false;
        }
        swap_target_bundles_.clear();
        swap_target_bundles_.resize(swap_image_views_.size());
        const VkRenderPass rp = vulkan_render_pass_->vk_render_pass();
        for (size_t i { 0 }; i < swap_image_views_.size(); ++i) {
            if (!swap_target_bundles_[i].add_color_target(
                    vulkan::render_target_from_view(
                        swap_image_views_[i], swap_format_, extent_.width,
                        extent_.height))) {
                app_log_err("create_framebuffer: add_color_target failed");
                return false;
            }
            auto fb = swap_target_bundles_[i].get_framebuffer(device_, rp);
            if (!fb) {
                app_log_err(fb.error());
                return false;
            }
        }
        return true;
    }

    bool create_vertex_buffer() {
        vulkan::BufferCreateInfo bi {};
        bi.size = sizeof(k_vertices);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        auto ret =
            vulkan::Buffer::create(vulkan_ctx_->allocator(), bi);
        if (!ret) {
            app_log_err(ret.error());
            return false;
        }
        vertex_buffer_ = std::move(ret.value());
        auto mapped = vertex_buffer_.map();
        if (!mapped) {
            app_log_err(mapped.error());
            vertex_buffer_ = vulkan::Buffer {};
            return false;
        }
        std::memcpy(*mapped, k_vertices, sizeof(k_vertices));
        vertex_buffer_.unmap();
        return true;
    }

    bool create_uniform_buffers() {
        uniform_buffers_.resize(k_frames_in_flight);
        VmaAllocator allocator { vulkan_ctx_->allocator() };
        for (auto &ub : uniform_buffers_) {
            vulkan::BufferCreateInfo bi {};
            bi.size = sizeof(UniformBufferObject);
            bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bi.memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            auto ret = vulkan::Buffer::create(allocator, bi);
            if (!ret) {
                app_log_err(ret.error());
                return false;
            }
            ub.buffer = std::move(ret.value());
            auto mapped = ub.buffer.map();
            if (!mapped) {
                app_log_err(mapped.error());
                return false;
            }
            ub.mapped = mapped.value();
        }
        return true;
    }

    bool create_descriptor_pool_and_sets() {
        auto poolResult = vulkan::create_descriptor_pool_for_merged_bindings(
            device_, shaderPlb_.merged_bindings(), k_frames_in_flight);
        if (!poolResult) {
            app_log_err(poolResult.error());
            return false;
        }
        descriptor_pool_ = *poolResult;
        if (descriptor_pool_ == VK_NULL_HANDLE) {
            app_log_err(
                "create_descriptor_pool_and_sets: empty descriptor pool");
            return false;
        }

        if (descriptor_set_layouts_.empty()) {
            app_log_err("create_descriptor_pool_and_sets: no set layouts");
            return false;
        }
        std::vector<VkDescriptorSetLayout> layouts(
            k_frames_in_flight, descriptor_set_layouts_.front());
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

        const vulkan::BindingKey uboKey { 0, 0 };
        const auto &merged = shaderPlb_.merged_bindings();
        const auto uboIt = merged.find(uboKey);
        if (uboIt == merged.end()) {
            app_log_err(
                "create_descriptor_pool_and_sets: missing set=0 binding=0 "
                "from reflection");
            return false;
        }

        const vulkan::BindingKey texKey { 0, 1 };
        const auto texIt = merged.find(texKey);
        if (texIt == merged.end()) {
            app_log_err(
                "create_descriptor_pool_and_sets: missing set=0 binding=1 "
                "(combined image sampler) from reflection");
            return false;
        }

        for (uint32_t i { 0 }; i < k_frames_in_flight; ++i) {
            VkDescriptorBufferInfo binfo {};
            binfo.buffer = uniform_buffers_[i].buffer.buffer();
            binfo.offset = 0;
            binfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imgInfo {};
            imgInfo.sampler = texture_sampler_;
            imgInfo.imageView = texture_image_.view();
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[2] {};
            vulkan::init_write_descriptor_set(writes[0], descriptor_sets_[i],
                                              uboIt->second);
            writes[0].pBufferInfo = &binfo;

            vulkan::init_write_descriptor_set(writes[1], descriptor_sets_[i],
                                              texIt->second);
            writes[1].pImageInfo = &imgInfo;

            vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        }
        return true;
    }

    bool create_command_pool_and_buffers() {
        VkCommandPoolCreateInfo pci {
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
        };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = vulkan_ctx_->graphics_queue_family();
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

        if (!vulkan_render_pass_.has_value() ||
            image_index >= swap_target_bundles_.size()) {
            vkEndCommandBuffer(cb);
            return;
        }

        VkClearValue clear {};
        clear.color = { { 0.05f, 0.06f, 0.09f, 1.f } };
        auto fb_res = swap_target_bundles_[image_index].get_framebuffer(
            device_, vulkan_render_pass_->vk_render_pass());
        if (!fb_res) {
            app_log_err(fb_res.error());
            vkEndCommandBuffer(cb);
            return;
        }

        VkRenderPassBeginInfo rp { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass = vulkan_render_pass_->vk_render_pass();
        rp.framebuffer = fb_res.value();
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
        VkBuffer vb = vertex_buffer_.buffer();
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &vb_offset);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout_, 0, 1,
                                &descriptor_sets_[frame_index], 0, nullptr);

        vkCmdDraw(cb, 6, 1, 0, 0);
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

    vulkan::PipelineLayoutBuilder shaderPlb_;

    std::unique_ptr<vulkan::Context> vulkan_ctx_;

    VkSwapchainKHR swapchain_ { VK_NULL_HANDLE };
    std::vector<VkImageView> swap_image_views_;
    VkFormat swap_format_ { VK_FORMAT_UNDEFINED };
    VkExtent2D extent_ {};

    std::optional<vulkan::RenderPass> vulkan_render_pass_;
    std::vector<vulkan::RenderTargetBundle> swap_target_bundles_;
    VkPipelineLayout pipeline_layout_ { VK_NULL_HANDLE };
    VkPipeline pipeline_ { VK_NULL_HANDLE };
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts_;

    vulkan::Buffer vertex_buffer_;
    vulkan::Image texture_image_;
    VkSampler texture_sampler_ { VK_NULL_HANDLE };

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

    int exit_code { 0 };
    {
        lumen::platform::Window window;
        lumen::platform::WindowConfig cfg;
        cfg.title = "Lumen 旋转矩形示例";
        cfg.width = 960;
        cfg.height = 540;
        if (!window.create(cfg)) {
            exit_code = 1;
        } else {
            RotatingRectExample app;
            if (!app.init(window)) {
                exit_code = 1;
            } else {
                lumen::platform::EventPump pump;
                pump.set_on_application_event(
                    [&](lumen::platform::DispatchableEvent &de) {
                        lumen::platform::EventDispatcher d(de);
                        d.dispatch<lumen::platform::EventWindowResize>(
                            [&](lumen::platform::EventWindowResize &) {
                                app.set_framebuffer_resized();
                            });
                    });

                const auto start = std::chrono::steady_clock::now();
                while (pump.poll()) {
                    app.try_recreate_swapchain();
                    const auto now = std::chrono::steady_clock::now();
                    const float t =
                        std::chrono::duration<float>(now - start).count();
                    app.draw_frame(t);
                }

                app.shutdown();
            }
        }
    }

    core::log::Logger::shutdown();
    SDL_Quit();
    return exit_code;
}
