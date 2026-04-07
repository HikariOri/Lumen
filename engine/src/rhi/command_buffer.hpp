#pragma once

#include "rhi/vulkan.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace rhi {

/**
 * @brief 对已 `begin` 的主/次级命令缓冲的薄封装，录制 API 使用 snake_case。
 *
 * 不拥有 `vk::CommandBuffer` 生命周期；通常来自 `CommandPool::allocate` 或
 * `Device::frame_command_buffer()`。
 */
class CommandBuffer {
public:
    CommandBuffer() = default;
    explicit CommandBuffer(vk::CommandBuffer cmd) noexcept : cmd_(cmd) {}

    [[nodiscard]] vk::CommandBuffer vk_cmd() const noexcept { return cmd_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(cmd_);
    }

    void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize src_offset,
                     vk::DeviceSize dst_offset, vk::DeviceSize size);
    void copy_buffer(vk::Buffer src, vk::Buffer dst,
                     const vk::BufferCopy &region);
    void copy_buffer(vk::Buffer src, vk::Buffer dst,
                     vk::ArrayProxy<const vk::BufferCopy> regions);

    void
    copy_buffer_to_image(vk::Buffer src, vk::Image dst,
                         vk::ImageLayout dst_layout,
                         vk::ArrayProxy<const vk::BufferImageCopy> regions);

    void pipeline_barrier(
        vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage,
        vk::DependencyFlags dependency,
        vk::ArrayProxy<const vk::MemoryBarrier> memory_barriers,
        vk::ArrayProxy<const vk::BufferMemoryBarrier> buffer_barriers,
        vk::ArrayProxy<const vk::ImageMemoryBarrier> image_barriers);

    void bind_pipeline(vk::PipelineBindPoint bind_point, vk::Pipeline pipeline);

    void bind_descriptor_sets(
        vk::PipelineBindPoint bind_point, vk::PipelineLayout layout,
        std::uint32_t first_set,
        vk::ArrayProxy<const vk::DescriptorSet> descriptor_sets,
        vk::ArrayProxy<const std::uint32_t> dynamic_offsets);

    void bind_vertex_buffers(std::uint32_t first_binding,
                             vk::ArrayProxy<const vk::Buffer> buffers,
                             vk::ArrayProxy<const vk::DeviceSize> offsets);

    void bind_index_buffer(vk::Buffer buffer, vk::DeviceSize offset,
                           vk::IndexType index_type);

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance);

    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                      std::uint32_t first_index, std::int32_t vertex_offset,
                      std::uint32_t first_instance);

    void dispatch(std::uint32_t group_x, std::uint32_t group_y,
                  std::uint32_t group_z);

    void begin_render_pass(const vk::RenderPassBeginInfo &info,
                           vk::SubpassContents contents);

    void end_render_pass();

    void set_viewport(std::uint32_t first_viewport,
                      vk::ArrayProxy<const vk::Viewport> viewports);

    void set_scissor(std::uint32_t first_scissor,
                     vk::ArrayProxy<const vk::Rect2D> scissors);

    void
    clear_color_image(vk::Image image, vk::ImageLayout layout,
                      const vk::ClearColorValue &color,
                      vk::ArrayProxy<const vk::ImageSubresourceRange> ranges);

private:
    vk::CommandBuffer cmd_ {};
};

/**
 * @brief 与队列族绑定的命令池；用于离帧一次性提交或独立录制路径。
 *
 * @note 与 `FrameContext` 内置池分离，避免与 `begin_frame`/`end_frame`
 * 轮转耦合。
 */
class CommandPool {
public:
    CommandPool() = default;
    CommandPool(const CommandPool &) = delete;
    CommandPool &operator=(const CommandPool &) = delete;
    CommandPool(CommandPool &&other) noexcept;
    CommandPool &operator=(CommandPool &&other) noexcept;
    ~CommandPool();

    [[nodiscard]] bool
    init(vk::Device device, std::uint32_t queue_family_index,
         vk::CommandPoolCreateFlags flags =
             vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

    void shutdown();

    [[nodiscard]] std::vector<vk::CommandBuffer>
    allocate(std::uint32_t count,
             vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);

    void free(vk::ArrayProxy<const vk::CommandBuffer> buffers);

    void reset_pool();

    /// 分配 1 个
    /// primary、`begin`→`record(CommandBuffer&)`→`end`→`submit`→**wait**，再释放缓冲。
    [[nodiscard]] bool
    submit_one_shot(vk::Queue queue,
                    const std::function<void(CommandBuffer &cmd)> &record);

    [[nodiscard]] vk::CommandPool vk_pool() const noexcept { return pool_; }
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(pool_);
    }

private:
    void destroy_();
    void free_one_(vk::CommandBuffer cb);

    vk::Device device_ {};
    vk::CommandPool pool_ {};
};

} // namespace rhi
