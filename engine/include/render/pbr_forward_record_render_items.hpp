/**
 * @file pbr_forward_record_render_items.hpp
 * @brief 前向 PBR / Pick ID：从 `scene::RenderItem` 录制 `vkCmd*`（单向依赖 render → scene）
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include <glm/mat3x3.hpp>
#include <vulkan/vulkan.h>

#include "render/material/pbr_forward_ubo.hpp"
#include "render/resource/buffer.hpp"
#include "scene/pick.hpp"
#include "scene/render_item.hpp"

namespace lumen::render {

/**
 * @brief 由模型矩阵填充 `PbrObjectUbo`（与示例中手写逻辑一致）
 */
[[nodiscard]] inline PbrObjectUbo make_pbr_object_ubo(const glm::mat4 &model) noexcept {
    PbrObjectUbo ou {};
    ou.model = model;
    const glm::mat3 n3 = glm::mat3(model);
    ou.normalMatrix = glm::mat4(glm::transpose(glm::inverse(n3)));
    return ou;
}

/**
 * @brief `record_pbr_forward_render_items` 的录制上下文（假定 pipeline layout：set0 帧 +
 * IBL，set1 材质，set2 物体动态 UBO，set3 光源）
 */
struct PbrForwardRecordContext {
    VkCommandBuffer command_buffer {};
    VkPipelineLayout pipeline_layout {};
    VkDescriptorSet frame_descriptor_set {};
    VkDescriptorSet light_descriptor_set {};
    VkDescriptorSet object_descriptor_set {};
    /// `item.material == nullptr` 时用于解析材质指针（再交给 `get_material_descriptor_set`）
    const Material *default_material {};
    /// 单槽 `PbrObjectUbo` 动态对齐跨度
    std::uint32_t object_dynamic_stride {};
    /// 为 true 时对每条 item 绑定 VB/IB；为 false 时假定调用方已绑定共享几何
    bool bind_vertex_and_index_buffers_per_item { true };
};

/**
 * @brief Pick ID pass 录制上下文（set0 帧，set2 物体动态 UBO；片元 push constant 为 pick id）
 */
struct PickIdRecordContext {
    VkCommandBuffer command_buffer {};
    VkPipelineLayout pipeline_layout {};
    VkDescriptorSet frame_descriptor_set {};
    VkDescriptorSet object_descriptor_set {};
    std::uint32_t object_dynamic_stride {};
    VkShaderStageFlags pick_id_push_stages { VK_SHADER_STAGE_FRAGMENT_BIT };
    std::uint32_t pick_id_push_constant_offset { 0 };
    bool bind_vertex_and_index_buffers_per_item { true };
};

/// 无 per-draw 回调时的占位（更新材质 UBO 等）
struct PbrForwardNoopBeforeDraw {
    void operator()(const scene::RenderItem &, const Material *,
                    std::uint32_t) const noexcept {}
};

/**
 * @tparam ObjectUboBuffer 具备 `update(const PbrObjectUbo &, size_t byte_offset)`（如
 * `UniformBuffer`）
 * @tparam GetMaterialDescriptorSet 可调用 `(const Material *) -> VkDescriptorSet`
 * @tparam BeforeItemDraw 可调用 `(const RenderItem &, const Material *resolved, uint32_t
 * draw_slot)`；在绑定 descriptor 之前调用（用于每 draw 更新材质 UBO 等）
 * @return 使用的 draw 槽数量（每条 **有效** item 递增 1，与示例原逻辑一致）
 */
template <typename ObjectUboBuffer, typename GetMaterialDescriptorSet,
          typename BeforeItemDraw>
std::uint32_t record_pbr_forward_render_items(
    const PbrForwardRecordContext &ctx, std::span<const scene::RenderItem> items,
    ObjectUboBuffer &object_ubo_buffer,
    GetMaterialDescriptorSet &&get_material_descriptor_set,
    BeforeItemDraw &&before_item_draw) {
    VkCommandBuffer cb = ctx.command_buffer;
    std::uint32_t draw_slot = 0;
    for (const scene::RenderItem &item : items) {
        if (!item.is_valid_for_draw()) {
            continue;
        }
        const Material *resolved = item.material != nullptr ? item.material
                                                            : ctx.default_material;
        before_item_draw(item, resolved, draw_slot);
        const VkDescriptorSet material_ds =
            get_material_descriptor_set(resolved);
        const PbrObjectUbo ou = make_pbr_object_ubo(item.model);
        object_ubo_buffer.update(ou,
                                 static_cast<size_t>(draw_slot) *
                                     static_cast<size_t>(ctx.object_dynamic_stride));
        std::array<VkDescriptorSet, 4> sets { ctx.frame_descriptor_set, material_ds,
                                               ctx.object_descriptor_set,
                                               ctx.light_descriptor_set };
        const uint32_t dynamic_offset =
            draw_slot * ctx.object_dynamic_stride;
        vkCmdBindDescriptorSets(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline_layout, 0,
            static_cast<uint32_t>(sets.size()), sets.data(), 1, &dynamic_offset);
        ++draw_slot;
        if (ctx.bind_vertex_and_index_buffers_per_item) {
            const auto *prim = item.primitive;
            const VkDeviceSize voff =
                static_cast<VkDeviceSize>(prim->vertexByteOffset);
            VkBuffer vb = item.vertexBuffer->handle();
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &voff);
            vkCmdBindIndexBuffer(cb, item.indexBuffer->handle(), 0,
                                 item.indexBuffer->vk_index_type());
        }
        vkCmdDrawIndexed(cb, item.primitive->indexCount, 1,
                         item.primitive->firstIndex, item.primitive->baseVertex, 0);
    }
    return draw_slot;
}

template <typename ObjectUboBuffer, typename GetMaterialDescriptorSet>
std::uint32_t record_pbr_forward_render_items(
    const PbrForwardRecordContext &ctx, std::span<const scene::RenderItem> items,
    ObjectUboBuffer &object_ubo_buffer,
    GetMaterialDescriptorSet &&get_material_descriptor_set) {
    return record_pbr_forward_render_items(
        ctx, items, object_ubo_buffer,
        std::forward<GetMaterialDescriptorSet>(get_material_descriptor_set),
        PbrForwardNoopBeforeDraw {});
}

/**
 * @tparam BeforeItemPick `(const RenderItem &, uint32_t slot)`，在写入 object UBO 与 bind 之前调用
 * @return 与示例一致：每条 **有效** item 末尾槽计数 +1（含 `pick id == 0` 未绘制项）
 */
template <typename ObjectUboBuffer, typename BeforeItemPick>
std::uint32_t record_pick_id_render_items(const PickIdRecordContext &ctx,
                                         std::span<const scene::RenderItem> items,
                                         ObjectUboBuffer &object_ubo_buffer,
                                         BeforeItemPick &&before_item_pick) {
    VkCommandBuffer cb = ctx.command_buffer;
    std::uint32_t pick_draw_slot = 0;
    for (const scene::RenderItem &item : items) {
        if (!item.is_valid_for_draw()) {
            continue;
        }
        before_item_pick(item, pick_draw_slot);
        const std::uint32_t pid =
            scene::encode_pick_entity_id(item.pick_entity);
        if (pid != 0U) {
            const PbrObjectUbo ou = make_pbr_object_ubo(item.model);
            object_ubo_buffer.update(
                ou, static_cast<size_t>(pick_draw_slot) *
                        static_cast<size_t>(ctx.object_dynamic_stride));
            std::array<VkDescriptorSet, 2> sets { ctx.frame_descriptor_set,
                                                  ctx.object_descriptor_set };
            const uint32_t dynamic_offset =
                pick_draw_slot * ctx.object_dynamic_stride;
            vkCmdBindDescriptorSets(
                cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline_layout, 0,
                static_cast<uint32_t>(sets.size()), sets.data(), 1,
                &dynamic_offset);
            vkCmdPushConstants(cb, ctx.pipeline_layout, ctx.pick_id_push_stages,
                               static_cast<int32_t>(ctx.pick_id_push_constant_offset),
                               static_cast<uint32_t>(sizeof(std::uint32_t)), &pid);
            const auto *prim = item.primitive;
            if (ctx.bind_vertex_and_index_buffers_per_item) {
                const VkDeviceSize voff =
                    static_cast<VkDeviceSize>(prim->vertexByteOffset);
                VkBuffer vb = item.vertexBuffer->handle();
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &voff);
                vkCmdBindIndexBuffer(cb, item.indexBuffer->handle(), 0,
                                     item.indexBuffer->vk_index_type());
            }
            vkCmdDrawIndexed(cb, prim->indexCount, 1, prim->firstIndex,
                             prim->baseVertex, 0);
        }
        ++pick_draw_slot;
    }
    return pick_draw_slot;
}

template <typename ObjectUboBuffer>
std::uint32_t record_pick_id_render_items(
    const PickIdRecordContext &ctx, std::span<const scene::RenderItem> items,
    ObjectUboBuffer &object_ubo_buffer) {
    return record_pick_id_render_items(
        ctx, items, object_ubo_buffer,
        [](const scene::RenderItem &, std::uint32_t) noexcept {});
}

} // namespace lumen::render
