/**
 * @file render_target.cpp
 * @brief `to_render_target` 实现。
 */

#include "vulkan/render_target.hpp"

#include "vulkan/image.hpp"

namespace vulkan {

RenderTarget to_render_target(const Image &image) {
    RenderTarget rt {};
    if (!image.is_valid() || image.view() == VK_NULL_HANDLE) {
        return rt;
    }
    const VkExtent3D ext = image.extent();
    if (ext.depth != 1U) {
        return rt;
    }
    rt.view = image.view();
    rt.format = image.format();
    rt.width = ext.width;
    rt.height = ext.height;
    return rt;
}

} // namespace vulkan
