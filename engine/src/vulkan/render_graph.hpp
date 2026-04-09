/**
 * @file render_graph.hpp
 * @brief 轻量 RenderGraph：委托 `RgResources` 管理图像槽位；按 Pass 编译
 *        `RenderPass` + `RenderTargetBundle`；`execute` 插入布局屏障并录制子 pass。
 *
 * @note 含交换链写入的 pass 须在每帧 `acquire` 后 `set_resource_target` +
 *       `prepare_frame`，再 `execute`。
 */

#pragma once

#include "vulkan/rg_node.hpp"
#include "vulkan/rg_resources.hpp"
#include "vulkan/rg_types.hpp"
#include "vulkan/render_pass.hpp"
#include "vulkan/render_target.hpp"
#include "vulkan/render_target_bundle.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class Context;

using RgExecuteFunc = RgPassExecuteFunc;

class RenderGraph final {
public:
    explicit RenderGraph(Context &ctx) noexcept;
    ~RenderGraph();

    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;
    RenderGraph(RenderGraph &&) = delete;
    RenderGraph &operator=(RenderGraph &&) = delete;

    [[nodiscard]] RgResourceHandle create_texture(std::uint32_t width,
                                                  std::uint32_t height,
                                                  VkFormat format);
    [[nodiscard]] RgResourceHandle create_depth(std::uint32_t width,
                                                std::uint32_t height);

    [[nodiscard]] RgResourceHandle import_swapchain(
        const RenderTarget &rt,
        VkImage swapchain_image = VK_NULL_HANDLE);

    void set_resource_target(RgResourceHandle handle, const RenderTarget &rt,
                             VkImage swapchain_image = VK_NULL_HANDLE);

    void add_pass(RgPassNode pass);

    void add_pass(
        std::string name,
        std::vector<std::pair<RgResourceHandle, RgAccess>> writes,
        std::vector<std::pair<RgResourceHandle, RgAccess>> reads,
        RgExecuteFunc execute);

    /// 将已登记的 pass 名称与读写数量打到引擎 Info 日志（调试用）。
    void dump_graph() const;

    [[nodiscard]] std::expected<void, std::string>
    prepare_frame(VkDevice device);

    [[nodiscard]] std::expected<void, std::string> compile();

    [[nodiscard]] std::expected<void, std::string>
    execute(VkCommandBuffer cmd, VkDevice device);

    void clear_compiled();
    /// 与 `clear_compiled()` 等价，便于与示例代码对齐。
    void destroy(VkDevice device);

    [[nodiscard]] RgResources &resources() noexcept { return resources_; }
    [[nodiscard]] const RgResources &resources() const noexcept {
        return resources_;
    }

    [[nodiscard]] RgResourceType resource_type(RgResourceHandle h) const;
    [[nodiscard]] RenderTarget resource_render_target(RgResourceHandle h) const;

    [[nodiscard]] bool valid_handle(RgResourceHandle h) const noexcept;

    /// @brief 编译后取第 @p pass_index 个子 pass 的 `VkRenderPass`（用于
    ///        `GraphicsPipelineBuilder::set_render_pass`）。
    [[nodiscard]] VkRenderPass render_pass_for_pass(std::size_t pass_index) const noexcept;

private:
    struct CompiledPass {
        std::string name;
        std::vector<std::pair<RgResourceHandle, RgAccess>> writes;
        std::vector<std::pair<RgResourceHandle, RgAccess>> reads;
        RgExecuteFunc execute;
        RenderTargetBundle bundle {};
        std::optional<RenderPass> render_pass {};
        bool has_swapchain_write { false };
    };

    [[nodiscard]] std::expected<void, std::string> build_bundle_from_writes_(
        const std::vector<std::pair<RgResourceHandle, RgAccess>> &writes,
        RenderTargetBundle &bundle) const;

    [[nodiscard]] bool pass_writes_swapchain_(const RgPassNode &node) const;

    void destroy_compiled_passes() noexcept;

    Context &ctx_;
    RgResources resources_;
    std::vector<RgPassNode> pass_nodes_;
    std::vector<CompiledPass> compiled_;
};

} // namespace vulkan
