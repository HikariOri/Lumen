#pragma once

#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>

namespace rhi {

/// 同步上传通道：用于 **未** 处于 `Device::begin_frame`～`end_frame` 之间的路径（如启动时 `create_*`）。
/// `flush()` 会 `submit` 并 **wait**，勿在帧循环热路径使用。
class UploadContext {
public:
    UploadContext() = default;
    UploadContext(const UploadContext &) = delete;
    UploadContext &operator=(const UploadContext &) = delete;
    UploadContext(UploadContext &&) = delete;
    UploadContext &operator=(UploadContext &&) = delete;
    ~UploadContext();

    [[nodiscard]] bool init(vk::Device device, vk::Queue queue,
                            VmaAllocator allocator,
                            std::uint32_t queue_family_index,
                            vk::DeviceSize staging_capacity = 16u * 1024u * 1024u);
    void shutdown();

    /// 将数据复制到 `dst` 的 `[dst_offset, dst_offset + size)`；自动保证 `TransferDst` 路径。
    void upload_buffer(vk::Buffer dst, vk::DeviceSize dst_offset, const void *data,
                       std::size_t size);

    /// 2D 图像 mip0：`extent` 为像素尺寸；内部 layout 转换 + `copyBufferToImage`。
    void upload_image(vk::Image image, vk::Format format, vk::Extent3D extent,
                      const void *data, std::size_t row_pitch_bytes);

    /// 结束录制、提交、等待、重置池与环形偏移。
    void flush();

    [[nodiscard]] vk::DeviceSize staging_capacity() const { return staging_capacity_; }

private:
    [[nodiscard]] bool ensure_staging_(std::size_t need);
    void begin_if_needed_();
    void grow_staging_(vk::DeviceSize min_required);

    vk::Device vk_device_;
    vk::Queue queue_;
    std::uint32_t queue_family_ { 0 };
    VmaAllocator allocator_ { VK_NULL_HANDLE };

    vk::CommandPool cmd_pool_;
    vk::CommandBuffer cmd_;
    vk::Fence fence_;

    vk::Buffer staging_buffer_;
    VmaAllocation staging_alloc_ { VK_NULL_HANDLE };
    void *staging_mapped_ { nullptr };
    vk::DeviceSize staging_capacity_ { 0 };

    vk::DeviceSize ring_offset_ { 0 };
    bool recording_ { false };
};

} // namespace rhi
