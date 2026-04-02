/**
 * @file pick_id_render_target.hpp
 * @brief ID Map 离屏目标：`VK_FORMAT_R32_UINT` 颜色 + 深度（无 MSAA），见 `note/pick.md`
 */

#pragma once

#include <cstdint>

#include "render/vulkan.hpp"

#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/resource/image.hpp"

namespace lumen {
namespace render {

/**
 * @brief R32_UINT 颜色附件 + 可选深度；RenderPass 结束后颜色布局为 TRANSFER_SRC
 */
class PickIdRenderTarget {
public:
    PickIdRenderTarget() = default;
    PickIdRenderTarget(const PickIdRenderTarget &) = delete;
    PickIdRenderTarget &operator=(const PickIdRenderTarget &) = delete;
    PickIdRenderTarget(PickIdRenderTarget &&other) noexcept;
    PickIdRenderTarget &operator=(PickIdRenderTarget &&other) noexcept;
    ~PickIdRenderTarget();

    bool create(const Context &ctx, uint32_t width, uint32_t height);
    bool resize(uint32_t width, uint32_t height);

    [[nodiscard]] bool is_valid() const {
        return renderPass_.is_valid() && framebuffer_.count() > 0 &&
               framebuffer_.get(0) != VK_NULL_HANDLE;
    }

    [[nodiscard]] VkRenderPass render_pass() const {
        return renderPass_.handle();
    }
    [[nodiscard]] const RenderPass &render_pass_ref() const {
        return renderPass_;
    }
    [[nodiscard]] VkFramebuffer framebuffer() const {
        return framebuffer_.get(0);
    }
    [[nodiscard]] VkExtent2D extent() const { return { width_, height_ }; }
    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }

    [[nodiscard]] VkImage color_image_vk() const { return colorImage_.handle(); }
    [[nodiscard]] const Image &color_image() const { return colorImage_; }

    /// 在 copy 之后调用，将颜色图从 TRANSFER_SRC 置回 UNDEFINED，供下一帧 loadOp CLEAR
    static void record_color_barrier_to_undefined(VkCommandBuffer cmd,
                                                    VkImage color_image);

    /// 将 R32_UINT 颜色图从 `TRANSFER_SRC` 迁到 `SHADER_READ_ONLY`（供 `usampler2D`）
    static void record_color_barrier_transfer_src_to_shader_read(
        VkCommandBuffer cmd, VkImage color_image);

    /// 可视化采样结束后迁回 `UNDEFINED`
    static void record_color_barrier_shader_read_to_undefined(
        VkCommandBuffer cmd, VkImage color_image);

private:
    void destroy_();
    bool create_internal_(const Context &ctx);

    const Context *ctx_ { nullptr };
    uint32_t width_ { 0 };
    uint32_t height_ { 0 };

    Image colorImage_ {};
    Image depthImage_ {};
    RenderPass renderPass_ {};
    Framebuffer framebuffer_ {};
};

} // namespace render
} // namespace lumen
