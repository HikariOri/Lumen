/**
 * @file vertex_layout.hpp
 * @brief 顶点输入布局：与 Vulkan vertex input state / glTF 多 attribute 对齐
 *
 * @details
 * 描述交错顶点在单个 vertex buffer binding 内的 stride 与各 `location` 的
 * `VkFormat`、字节偏移。用于
 * `Primitive::layout`，使「画什么」与管线顶点输入声明一致。
 *
 * 使用定长 `std::array` 存储 attribute，避免每个 `Primitive` 附带 `std::vector`
 * 堆分配； 有效个数由 `attributeCount` 表示，且不超过 `MAX_ATTRIBUTES`。
 *
 * @note
 * 与具体 `GraphicsPipeline` 的 `vertexAttributes` 配置需保持一致，否则校验层或
 * GPU 行为可能异常。
 *
 * @see `asset/geometry/mesh_asset.hpp`（`geometry::Primitive::layout`）
 *
 * @ingroup Render
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace lumen::render {

/**
 * @brief 单个顶点属性在缓冲内的布局片段
 */
struct VertexAttribute {
    std::uint32_t location {}; ///< 着色器中 `layout(location = …)` 对应槽位
    VkFormat format {};        ///< 分量类型与维数（如 `vec3` →
                               ///< `VK_FORMAT_R32G32B32_SFLOAT`）
    std::uint32_t offset {};   ///< 相对当前 binding 起始的字节偏移
};

/**
 * @brief 完整顶点输入布局（单 binding、交错顶点）
 */
struct VertexLayout {
    static constexpr std::uint32_t MAX_ATTRIBUTES {
        16
    }; ///< 最大 attribute 条数

    std::uint32_t stride {}; ///< 单个顶点跨距（字节），对应
                             ///< `VkVertexInputBindingDescription::stride`
    std::uint32_t attributeCount {}; ///< 当前有效的 `attributes` 前缀长度
    std::array<VertexAttribute, MAX_ATTRIBUTES>
        attributes {}; ///< 各 location 描述

    /**
     * @brief 追加一条属性（已满时静默忽略）
     * @param attr location / format / offset
     */
    void add_attribute(VertexAttribute attr) {
        if (attributeCount >= MAX_ATTRIBUTES) {
            return;
        }
        const auto ix = static_cast<std::size_t>(attributeCount);
        attributes.at(ix) = attr;
        ++attributeCount;
    }

    /**
     * @return 未配置 stride 或无任何 attribute 时为 true
     */
    [[nodiscard]] bool empty() const {
        return attributeCount == 0U || stride == 0U;
    }
};

/**
 * @brief 构建与 `shaders/glsl/pbr_forward.vert` location 0…3 一致的布局
 *
 * @details
 * 顺序为：position (`vec3`)、normal (`vec3`)、uv (`vec2`)、tangent (`vec4`)。
 * 内存布局与示例中交错 `HelmVertex` 及常见 glTF 上传路径一致。
 *
 * @return 已填充 `stride` 与四条 `VertexAttribute` 的 `VertexLayout`
 */
[[nodiscard]] inline VertexLayout make_vertex_layout_pbr_forward_tangent() {
    struct V {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 tangent;
    };

    VertexLayout L {};
    L.stride = static_cast<std::uint32_t>(sizeof(V));
    L.add_attribute(VertexAttribute {
        .location = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(V, position)),
    });
    L.add_attribute(VertexAttribute {
        .location = 1,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(V, normal)),
    });
    L.add_attribute(VertexAttribute {
        .location = 2,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(V, uv)),
    });
    L.add_attribute(VertexAttribute {
        .location = 3,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(V, tangent)),
    });
    return L;
}

} // namespace lumen::render
