/**
 * @file rg_resources.hpp
 * @brief RenderGraph 资源池：`Image` / 交换链 `RenderTarget` 槽位与每槽布局跟踪。
 */

#pragma once

#include "vulkan/image.hpp"
#include "vulkan/rg_types.hpp"
#include "vulkan/render_target.hpp"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class Context;

/**
 * @brief 由 `RenderGraph` 持有；创建离屏纹理/深度、登记交换链，并维护与槽位一一对应的
 *        `VkImageLayout`（供调度器插入屏障）。
 */
class RgResources final {
public:
    explicit RgResources(Context &ctx) noexcept;

    [[nodiscard]] RgResourceHandle create_texture(std::uint32_t width,
                                                  std::uint32_t height,
                                                  VkFormat format);
    [[nodiscard]] RgResourceHandle create_depth(std::uint32_t width,
                                                std::uint32_t height);

    /**
     * @param swapchain_image 可为 `VK_NULL_HANDLE`（仅 framebuffer / 呈现由 render pass
     *        处理；跨 pass 图像屏障将跳过该槽）。
     */
    [[nodiscard]] RgResourceHandle import_swapchain(const RenderTarget &rt,
                                                  VkImage swapchain_image =
                                                      VK_NULL_HANDLE);

    void set_resource_target(RgResourceHandle handle, const RenderTarget &rt,
                             VkImage swapchain_image = VK_NULL_HANDLE);

    [[nodiscard]] bool valid_handle(RgResourceHandle h) const noexcept;
    [[nodiscard]] RgResourceType resource_type(RgResourceHandle h) const;
    [[nodiscard]] RenderTarget render_target(RgResourceHandle h) const;

    /// 屏障用 `VkImage`：自有 `Image` 或交换链外部句柄。
    [[nodiscard]] VkImage barrier_vk_image(RgResourceHandle h) const noexcept;

    /// 非空表示可调用 `Image::record_layout_barrier`。
    [[nodiscard]] const Image *try_owned_image(RgResourceHandle h) const noexcept;

    /// @pre `valid_handle(h)`
    [[nodiscard]] VkImageLayout layout(RgResourceHandle h) const;
    void set_layout(RgResourceHandle h, VkImageLayout layout);
    void reset_all_layouts(VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);

    /// @pre `valid_handle(h)`；与 `layout` / `set_layout` 读写同一字段。
    [[nodiscard]] RgResourceState &mutable_state(RgResourceHandle h) noexcept;
    [[nodiscard]] const RgResourceState &resource_state(
        RgResourceHandle h) const noexcept;

    /// @pre `valid_handle(h)`（交换链槽可改 view，须与 `set_resource_target` 一致使用）。
    [[nodiscard]] RenderTarget &render_target_ref(RgResourceHandle h) noexcept;
    [[nodiscard]] const RenderTarget &render_target_ref(
        RgResourceHandle h) const noexcept;

    /// 非空表示引擎持有 `Image`，可配合 `Image::record_layout_barrier`。
    [[nodiscard]] Image *try_owned_image_mut(RgResourceHandle h) noexcept;

    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }

private:
    struct Slot {
        RgResourceType type { RgResourceType::Texture };
        RenderTarget target {};
        Image owned_image {};
        VkImage external_image { VK_NULL_HANDLE };
    };

    Context &ctx_;
    std::vector<Slot> slots_;
    std::vector<RgResourceState> states_;
};

} // namespace vulkan
