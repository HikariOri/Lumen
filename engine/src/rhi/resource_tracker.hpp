#pragma once

#include "rhi/buffer.hpp"
#include "rhi/image.hpp"
#include "rhi/vulkan.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace rhi {

/// 供后续 RenderGraph / barrier 优化使用；缓冲区不使用 `layout` 字段。
struct ResourceState {
    vk::PipelineStageFlags stage_mask { vk::PipelineStageFlagBits::eTopOfPipe };
    vk::AccessFlags access_mask {};
    vk::ImageLayout layout { vk::ImageLayout::eUndefined };
};

class ResourceTracker {
public:
    void set_buffer_state(BufferHandle h, ResourceState s);
    void set_image_state(ImageHandle h, ResourceState s);

    [[nodiscard]] std::optional<ResourceState> buffer_state(BufferHandle h) const;
    [[nodiscard]] std::optional<ResourceState> image_state(ImageHandle h) const;

    void update_buffer(BufferHandle h, ResourceState s);
    void update_image(ImageHandle h, ResourceState s);

    void clear();

    void erase_buffer(BufferHandle h);
    void erase_image(ImageHandle h);

private:
    static std::uint64_t buffer_key(BufferHandle h);
    static std::uint64_t image_key(ImageHandle h);

    std::unordered_map<std::uint64_t, ResourceState> buffer_states_;
    std::unordered_map<std::uint64_t, ResourceState> image_states_;
};

} // namespace rhi
