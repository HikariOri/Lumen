#pragma once

#include "rhi/upload_ring_buffer.hpp"
#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstdint>

namespace rhi {

/// 单槽「帧飞行」上下文：命令池 + 主命令缓冲 + fence + 本帧上传环形 staging。
/// 用法：`begin_recording` → 录制 copy / draw → `submit`；与
/// `Device::frame_slot_` 轮转配合。
///
/// （路线图）每帧 descriptor set 池化 / arena 与上传环并列管理可后续在此层扩展。
class FrameContext {
public:
    FrameContext() = default;
    FrameContext(const FrameContext &) = delete;
    FrameContext &operator=(const FrameContext &) = delete;
    FrameContext(FrameContext &&) = delete;
    FrameContext &operator=(FrameContext &&) = delete;
    ~FrameContext() = default;

    [[nodiscard]] bool init(vk::Device device, std::uint32_t queue_family,
                            VmaAllocator allocator, vk::DeviceSize ring_bytes);
    void shutdown(vk::Device device, VmaAllocator allocator);

    /// 等待本槽上一轮提交完成，重置命令池与上传环，并开始录制主命令缓冲。
    void begin_recording(vk::Device device);

    /// 结束录制并提交到队列（**不**阻塞 CPU）。
    void submit(vk::Device device, vk::Queue queue);

    /// 同 `submit`，但可接入交换链：`wait_image_available` 由
    /// `acquireNextImageKHR` 信号、`signal_render_finished` 供 `presentKHR`
    /// 等待；任一句柄为空则对应计数为 0。
    void submit(vk::Device device, vk::Queue queue,
                vk::Semaphore wait_image_available,
                vk::PipelineStageFlags wait_dst_stage_mask,
                vk::Semaphore signal_render_finished);

    [[nodiscard]] UploadRingBuffer &upload_ring() { return upload_ring_; }
    [[nodiscard]] const UploadRingBuffer &upload_ring() const {
        return upload_ring_;
    }

    [[nodiscard]] vk::CommandBuffer command_buffer() const { return cmd_; }

    [[nodiscard]] std::uint64_t serial() const { return serial_; }

    [[nodiscard]] bool is_recording() const { return recording_; }

private:
    vk::CommandPool cmd_pool_ {};
    vk::CommandBuffer cmd_ {};
    vk::Fence fence_ {};
    UploadRingBuffer upload_ring_ {};
    std::uint64_t serial_ { 0 };
    bool recording_ { false };
};

} // namespace rhi
