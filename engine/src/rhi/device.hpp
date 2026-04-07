#pragma once

#include "rhi/buffer.hpp"
#include "rhi/buffer_pool.hpp"
#include "rhi/context.hpp"
#include "rhi/frame_context.hpp"
#include "rhi/image.hpp"
#include "rhi/image_pool.hpp"
#include "rhi/resource_tracker.hpp"
#include "rhi/upload_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace rhi {

struct PipelineHandle {
    std::uint32_t id {};
};

struct DescriptorSetHandle {
    std::uint32_t id {};
};

/// GPU 资源入口：多帧 `FrameContext`（异步上传 + 主 CB）、同步
/// `UploadContext`（仅加载期）、池与跟踪器。
class Device {
public:
    static constexpr std::uint32_t k_frames_in_flight = 3;

    Device() = default;
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    ~Device();

    [[nodiscard]] bool init(Context &ctx);
    void shutdown();

    /// 等待本帧槽 fence → 重置池与上传环 → 开始录制主命令缓冲（可与 copy / draw
    /// 共用）。
    void begin_frame();

    /// 结束主命令缓冲并 `submit`（不阻塞 CPU）；随后轮转帧槽。
    /// 交换链绘制时传入 `acquire` / `present` 所用 semaphore（可为空）。
    void end_frame(
        vk::Semaphore wait_image_available = {},
        vk::PipelineStageFlags wait_dst_stage_mask =
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::Semaphore signal_render_finished = {});

    [[nodiscard]] bool frame_open() const { return in_frame_; }

    [[nodiscard]] FrameContext &current_frame() { return frames_[frame_slot_]; }
    [[nodiscard]] const FrameContext &current_frame() const {
        return frames_[frame_slot_];
    }

    /// 仅在 `begin_frame`～`end_frame` 之间有效。
    [[nodiscard]] vk::CommandBuffer frame_command_buffer() const;

    [[nodiscard]] std::uint32_t frame_slot() const { return frame_slot_; }

    [[nodiscard]] BufferHandle create_buffer(const BufferDesc &desc);
    [[nodiscard]] ImageHandle create_image(const ImageDesc &desc);

    void destroy_buffer(BufferHandle h);
    void destroy_image(ImageHandle h);

    /// 在帧内：写入本帧 `UploadRing` 并录制 `copyBuffer`（无
    /// submit）；帧外：走同步上传并 `flush`。
    void upload_buffer(BufferHandle h, const void *data, std::size_t size,
                       vk::DeviceSize dst_offset = 0);

    /// 仅刷新同步上传通道（`UploadContext`）。已处于 `begin_frame`
    /// 时不提交交换链帧。
    void flush_uploads();

    [[nodiscard]] DescriptorSetHandle create_descriptor_set();

    [[nodiscard]] vk::Device vk_device() const { return device_; }
    [[nodiscard]] vk::PhysicalDevice physical_device() const {
        return physical_device_;
    }
    [[nodiscard]] vk::Queue graphics_queue() const { return queue_; }
    [[nodiscard]] std::uint32_t graphics_queue_family() const {
        return queue_family_;
    }
    [[nodiscard]] vk::Queue present_queue() const { return present_queue_; }
    [[nodiscard]] std::uint32_t present_queue_family() const {
        return present_queue_family_;
    }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }

    [[nodiscard]] BufferPool &buffer_pool() { return buffer_pool_; }
    [[nodiscard]] const BufferPool &buffer_pool() const { return buffer_pool_; }
    [[nodiscard]] ImagePool &image_pool() { return image_pool_; }
    [[nodiscard]] const ImagePool &image_pool() const { return image_pool_; }

    [[nodiscard]] ResourceTracker *tracker() { return tracker_.get(); }
    [[nodiscard]] const ResourceTracker *tracker() const {
        return tracker_.get();
    }

    [[nodiscard]] const BufferResource *try_get(BufferHandle h) const;
    [[nodiscard]] const ImageResource *try_get(ImageHandle h) const;

private:
    void fill_vma_alloc_(MemoryUsage mem, VmaAllocationCreateInfo &aci) const;
    void apply_buffer_tracker_(BufferHandle h, BufferUsage usage);
    void apply_image_tracker_sampled_(ImageHandle h);

    void record_image_upload_frame_(FrameContext &frame, vk::Image image,
                                    vk::Format format, vk::Extent3D extent,
                                    const void *data, std::size_t row_pitch);

    vk::Device device_;
    vk::PhysicalDevice physical_device_ {};
    vk::Queue queue_ {};
    std::uint32_t queue_family_ { 0 };
    vk::Queue present_queue_ {};
    std::uint32_t present_queue_family_ { 0 };
    VmaAllocator allocator_ { VK_NULL_HANDLE };

    std::array<FrameContext, k_frames_in_flight> frames_;
    std::uint32_t frame_slot_ { 0 };
    bool in_frame_ { false };

    BufferPool buffer_pool_;
    ImagePool image_pool_;

    /// 加载期或未调用 `begin_frame` 时的阻塞上传（`flush` 会 wait）。
    std::unique_ptr<UploadContext> sync_upload_;
    std::unique_ptr<ResourceTracker> tracker_;

    bool initialized_ { false };
};

} // namespace rhi
