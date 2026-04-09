/**
 * @file rg_node.hpp
 * @brief RenderGraph 的 Pass 节点描述（读写资源 + 录制回调）。
 */

#pragma once

#include "vulkan/rg_types.hpp"
#include "vulkan/render_target_bundle.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

using RgPassExecuteFunc =
    std::function<void(VkCommandBuffer, const RenderTargetBundle &)>;

/**
 * @brief 与 `RenderGraph::add_pass` 登记的数据一致；可整体 `std::move` 传入。
 */
struct RgPassNode {
    std::string name;
    std::vector<std::pair<RgResourceHandle, RgAccess>> writes;
    std::vector<std::pair<RgResourceHandle, RgAccess>> reads;
    RgPassExecuteFunc execute;
};

} // namespace vulkan
