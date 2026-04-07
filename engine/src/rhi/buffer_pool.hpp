#pragma once

#include "rhi/buffer.hpp"

#include <vk_mem_alloc.h>

#include <vector>

namespace rhi {

/// 缓冲句柄槽位池：复用已释放槽位，负责按句柄销毁 VMA 缓冲。
class BufferPool {
public:
    BufferPool() = default;
    BufferPool(const BufferPool &) = delete;
    BufferPool &operator=(const BufferPool &) = delete;
    BufferPool(BufferPool &&) = default;
    BufferPool &operator=(BufferPool &&) = default;
    ~BufferPool() = default;

    /// 登记已由 `vmaCreateBuffer` 创建好的资源，返回池内句柄（`id` 从 1 起）。
    [[nodiscard]] BufferHandle insert(BufferResource resource);

    void destroy(BufferHandle h, VmaAllocator allocator);

    [[nodiscard]] const BufferResource *get(BufferHandle h) const;
    [[nodiscard]] BufferResource *get(BufferHandle h);

    /// 销毁池中仍存活的所有缓冲（`Device::shutdown` 使用）。
    void clear(VmaAllocator allocator);

    [[nodiscard]] std::size_t slot_count() const { return slots_.size(); }

private:
    struct Slot {
        BufferResource res {};
        bool live { false };
    };

    std::vector<Slot> slots_;
};

} // namespace rhi
