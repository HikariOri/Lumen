#pragma once

#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>

namespace rhi {

/// 每帧一块 CPU 可见 staging，线性分配；帧开始时 `reset()`
/// 整段复用（安全、无跨帧环回竞争）。
class UploadRingBuffer {
public:
    UploadRingBuffer() = default;
    UploadRingBuffer(const UploadRingBuffer &) = delete;
    UploadRingBuffer &operator=(const UploadRingBuffer &) = delete;
    UploadRingBuffer(UploadRingBuffer &&) = delete;
    UploadRingBuffer &operator=(UploadRingBuffer &&) = delete;
    ~UploadRingBuffer() = default;

    [[nodiscard]] bool init(VmaAllocator allocator,
                            vk::DeviceSize capacity_bytes);
    void shutdown(VmaAllocator allocator);

    /// `align` 任意正整数（内部 `align_up`）。失败时 `cpu == nullptr`。
    struct Allocation {
        void *cpu { nullptr };
        vk::Buffer buffer {};
        vk::DeviceSize offset { 0 };

        [[nodiscard]] explicit operator bool() const { return cpu != nullptr; }
    };

    [[nodiscard]] Allocation allocate(std::size_t size, std::size_t align);

    void reset() { head_ = 0; }

    [[nodiscard]] vk::DeviceSize capacity() const { return capacity_; }

private:
    vk::Buffer buffer_ {};
    VmaAllocation allocation_ { VK_NULL_HANDLE };
    void *mapped_ { nullptr };
    vk::DeviceSize capacity_ { 0 };
    vk::DeviceSize head_ { 0 };
    VmaAllocator allocator_ { VK_NULL_HANDLE };
};

} // namespace rhi
