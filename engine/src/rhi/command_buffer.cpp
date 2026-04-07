#include "rhi/command_buffer.hpp"
#include "rhi/vulkan.hpp"

#include "core/log/logger.hpp"

#include <array>
#include <limits>

namespace rhi {

void CommandBuffer::copy_buffer(const vk::Buffer src, const vk::Buffer dst,
                                  const vk::DeviceSize src_offset,
                                  const vk::DeviceSize dst_offset,
                                  const vk::DeviceSize size) {
    const vk::BufferCopy region { src_offset, dst_offset, size };
    cmd_.copyBuffer(src, dst, region);
}

void CommandBuffer::copy_buffer(const vk::Buffer src, const vk::Buffer dst,
                                const vk::BufferCopy &region) {
    cmd_.copyBuffer(src, dst, region);
}

void CommandBuffer::copy_buffer(const vk::Buffer src, const vk::Buffer dst,
                                const vk::ArrayProxy<const vk::BufferCopy>
                                    regions) {
    cmd_.copyBuffer(src, dst, regions);
}

void CommandBuffer::copy_buffer_to_image(
    const vk::Buffer src, const vk::Image dst, const vk::ImageLayout dst_layout,
    const vk::ArrayProxy<const vk::BufferImageCopy> regions) {
    cmd_.copyBufferToImage(src, dst, dst_layout, regions);
}

void CommandBuffer::pipeline_barrier(
    const vk::PipelineStageFlags src_stage,
    const vk::PipelineStageFlags dst_stage,
    const vk::DependencyFlags dependency,
    const vk::ArrayProxy<const vk::MemoryBarrier> memory_barriers,
    const vk::ArrayProxy<const vk::BufferMemoryBarrier> buffer_barriers,
    const vk::ArrayProxy<const vk::ImageMemoryBarrier> image_barriers) {
    cmd_.pipelineBarrier(src_stage, dst_stage, dependency, memory_barriers,
                         buffer_barriers, image_barriers);
}

void CommandBuffer::bind_pipeline(const vk::PipelineBindPoint bind_point,
                                    const vk::Pipeline pipeline) {
    cmd_.bindPipeline(bind_point, pipeline);
}

void CommandBuffer::bind_descriptor_sets(
    const vk::PipelineBindPoint bind_point, const vk::PipelineLayout layout,
    const std::uint32_t first_set,
    const vk::ArrayProxy<const vk::DescriptorSet> descriptor_sets,
    const vk::ArrayProxy<const std::uint32_t> dynamic_offsets) {
    cmd_.bindDescriptorSets(bind_point, layout, first_set, descriptor_sets,
                            dynamic_offsets);
}

void CommandBuffer::bind_vertex_buffers(
    const std::uint32_t first_binding,
    const vk::ArrayProxy<const vk::Buffer> buffers,
    const vk::ArrayProxy<const vk::DeviceSize> offsets) {
    cmd_.bindVertexBuffers(first_binding, buffers, offsets);
}

void CommandBuffer::bind_index_buffer(const vk::Buffer buffer,
                                      const vk::DeviceSize offset,
                                      const vk::IndexType index_type) {
    cmd_.bindIndexBuffer(buffer, offset, index_type);
}

void CommandBuffer::draw(const std::uint32_t vertex_count,
                         const std::uint32_t instance_count,
                         const std::uint32_t first_vertex,
                         const std::uint32_t first_instance) {
    cmd_.draw(vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indexed(const std::uint32_t index_count,
                                 const std::uint32_t instance_count,
                                 const std::uint32_t first_index,
                                 const std::int32_t vertex_offset,
                                 const std::uint32_t first_instance) {
    cmd_.drawIndexed(index_count, instance_count, first_index, vertex_offset,
                     first_instance);
}

void CommandBuffer::dispatch(const std::uint32_t group_x,
                             const std::uint32_t group_y,
                             const std::uint32_t group_z) {
    cmd_.dispatch(group_x, group_y, group_z);
}

void CommandBuffer::begin_render_pass(const vk::RenderPassBeginInfo &info,
                                      const vk::SubpassContents contents) {
    cmd_.beginRenderPass(info, contents);
}

void CommandBuffer::end_render_pass() { cmd_.endRenderPass(); }

void CommandBuffer::set_viewport(
    const std::uint32_t first_viewport,
    const vk::ArrayProxy<const vk::Viewport> viewports) {
    cmd_.setViewport(first_viewport, viewports);
}

void CommandBuffer::set_scissor(const std::uint32_t first_scissor,
                                const vk::ArrayProxy<const vk::Rect2D> scissors) {
    cmd_.setScissor(first_scissor, scissors);
}

void CommandBuffer::clear_color_image(
    const vk::Image image, const vk::ImageLayout layout,
    const vk::ClearColorValue &color,
    const vk::ArrayProxy<const vk::ImageSubresourceRange> ranges) {
    cmd_.clearColorImage(image, layout, color, ranges);
}

bool CommandPool::init(const vk::Device device,
                       const std::uint32_t queue_family_index,
                       const vk::CommandPoolCreateFlags flags) {
    shutdown();
    if (!device) {
        return false;
    }
    device_ = device;
    vk::CommandPoolCreateInfo ci {};
    ci.queueFamilyIndex = queue_family_index;
    ci.flags = flags;
    const vk::Result r = device_.createCommandPool(&ci, nullptr, &pool_);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::init 失败 {}", static_cast<int>(r));
        device_ = nullptr;
        return false;
    }
    return true;
}

void CommandPool::shutdown() { destroy_(); }

std::vector<vk::CommandBuffer>
CommandPool::allocate(const std::uint32_t count,
                        const vk::CommandBufferLevel level) {
    if (!pool_ || count == 0) {
        return {};
    }
    std::vector<vk::CommandBuffer> out(count);
    vk::CommandBufferAllocateInfo ai {};
    ai.commandPool = pool_;
    ai.level = level;
    ai.commandBufferCount = count;
    if (device_.allocateCommandBuffers(&ai, out.data()) !=
        vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::allocate 失败 count={}", count);
        return {};
    }
    return out;
}

void CommandPool::free(const vk::ArrayProxy<const vk::CommandBuffer> buffers) {
    if (!pool_ || buffers.size() == 0) {
        return;
    }
    device_.freeCommandBuffers(pool_, buffers);
}

void CommandPool::reset_pool() {
    if (pool_) {
        device_.resetCommandPool(pool_, {});
    }
}

bool CommandPool::submit_one_shot(
    const vk::Queue queue,
    const std::function<void(CommandBuffer &cmd)> &record) {
    if (!pool_ || !device_ || !queue) {
        LUMEN_LOG_ERROR("rhi::CommandPool::submit_one_shot: 无效参数");
        return false;
    }
    std::vector<vk::CommandBuffer> buffers =
        allocate(1, vk::CommandBufferLevel::ePrimary);
    if (buffers.empty() || !buffers[0]) {
        return false;
    }
    vk::CommandBuffer raw = buffers[0];
    vk::CommandBufferBeginInfo bi {};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    if (raw.begin(&bi) != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::submit_one_shot: begin 失败");
        free_one_(raw);
        return false;
    }
    CommandBuffer wrapped { raw };
    record(wrapped);
    const vk::Result er = raw.end();
    if (er != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::submit_one_shot: end 失败 {}",
                        static_cast<int>(er));
        free_one_(raw);
        return false;
    }

    vk::Fence fence {};
    if (device_.createFence({}, nullptr, &fence) != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::submit_one_shot: createFence 失败");
        free_one_(raw);
        return false;
    }
    vk::SubmitInfo si {};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &raw;
    const vk::Result sr = queue.submit(1, &si, fence);
    if (sr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("rhi::CommandPool::submit_one_shot: submit 失败 {}",
                        static_cast<int>(sr));
        device_.destroyFence(fence, nullptr);
        free_one_(raw);
        return false;
    }
    static_cast<void>(device_.waitForFences(
        1, &fence, vk::True, std::numeric_limits<std::uint64_t>::max()));
    device_.destroyFence(fence, nullptr);
    free_one_(raw);
    return true;
}

void CommandPool::destroy_() {
    if (pool_) {
        device_.destroyCommandPool(pool_, nullptr);
        pool_ = nullptr;
    }
    device_ = nullptr;
}

void CommandPool::free_one_(const vk::CommandBuffer cb) {
    if (!cb || !pool_) {
        return;
    }
    const std::array<vk::CommandBuffer, 1> one {{ cb }};
    device_.freeCommandBuffers(pool_, one);
}

CommandPool::~CommandPool() { destroy_(); }

CommandPool::CommandPool(CommandPool &&other) noexcept
    : device_(other.device_), pool_(other.pool_) {
    other.device_ = nullptr;
    other.pool_ = nullptr;
}

CommandPool &CommandPool::operator=(CommandPool &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy_();
    device_ = other.device_;
    pool_ = other.pool_;
    other.device_ = nullptr;
    other.pool_ = nullptr;
    return *this;
}

} // namespace rhi
