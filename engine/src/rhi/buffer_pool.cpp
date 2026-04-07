#include "rhi/buffer_pool.hpp"

namespace rhi {

BufferHandle BufferPool::insert(BufferResource resource) {
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].live) {
            slots_[i].res = std::move(resource);
            slots_[i].live = true;
            return BufferHandle { i + 1 };
        }
    }
    slots_.push_back(Slot { std::move(resource), true });
    return BufferHandle {
        static_cast<std::uint32_t>(slots_.size())
    };
}

void BufferPool::destroy(BufferHandle h, VmaAllocator allocator) {
    if (!is_valid(h) || allocator == nullptr) {
        return;
    }
    const std::uint32_t i = h.id - 1;
    if (i >= slots_.size() || !slots_[i].live) {
        return;
    }
    Slot &slot = slots_[i];
    if (slot.res.buffer) {
        vmaDestroyBuffer(allocator, static_cast<VkBuffer>(slot.res.buffer),
                           slot.res.allocation);
    }
    slot = {};
}

const BufferResource *BufferPool::get(BufferHandle h) const {
    if (!is_valid(h)) {
        return nullptr;
    }
    const std::uint32_t i = h.id - 1;
    if (i >= slots_.size() || !slots_[i].live) {
        return nullptr;
    }
    return &slots_[i].res;
}

BufferResource *BufferPool::get(BufferHandle h) {
    return const_cast<BufferResource *>(
        static_cast<const BufferPool *>(this)->get(h));
}

void BufferPool::clear(VmaAllocator allocator) {
    if (allocator == nullptr) {
        slots_.clear();
        return;
    }
    for (auto &slot : slots_) {
        if (slot.live && slot.res.buffer) {
            vmaDestroyBuffer(allocator, static_cast<VkBuffer>(slot.res.buffer),
                             slot.res.allocation);
        }
        slot = {};
    }
    slots_.clear();
}

} // namespace rhi
