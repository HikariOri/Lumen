#pragma once

#include "rhi/image.hpp"
#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <vector>

namespace rhi {

/// 图像 + ImageView 槽位池：复用槽位，按句柄销毁 view 与 VMA 图像。
class ImagePool {
public:
    ImagePool() = default;
    ImagePool(const ImagePool &) = delete;
    ImagePool &operator=(const ImagePool &) = delete;
    ImagePool(ImagePool &&) = default;
    ImagePool &operator=(ImagePool &&) = default;
    ~ImagePool() = default;

    [[nodiscard]] ImageHandle insert(ImageResource resource);

    void destroy(ImageHandle h, vk::Device device, VmaAllocator allocator);

    [[nodiscard]] const ImageResource *get(ImageHandle h) const;
    [[nodiscard]] ImageResource *get(ImageHandle h);

    void clear(vk::Device device, VmaAllocator allocator);

    [[nodiscard]] std::size_t slot_count() const { return slots_.size(); }

private:
    struct Slot {
        ImageResource res {};
        bool live { false };
    };

    std::vector<Slot> slots_;
};

} // namespace rhi
