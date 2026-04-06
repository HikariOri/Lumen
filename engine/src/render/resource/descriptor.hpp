/**
 * @file descriptor.hpp
 * @brief 描述符池、布局、描述符集管理（UBO / Texture / Sampler 绑定系统）
 *
 * Vulkan Descriptor 系统封装：
 * - DescriptorSetLayout：定义 shader 资源绑定结构（类似 root signature）
 * - DescriptorPool：负责 DescriptorSet 的分配与生命周期管理
 * - 写入函数：封装 UBO / Image 绑定更新逻辑
 *
 * 本模块是 CPU ↔ GPU 资源绑定的核心桥梁
 *
 * @todo 支持 buffer 和 image 写入同一个 pool
 */

#pragma once

#include <initializer_list>
#include <vector>
#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Context;

/**
 * @brief 单个 descriptor binding 描述
 *
 * 对应 shader 中的：
 * layout(set = X, binding = Y)
 *
 * Vulkan 中 binding 定义资源类型与 shader 可见性
 */
struct DescriptorBinding {

    /// shader 中的 binding index（layout(binding = X)）
    uint32_t binding { 0 };

    /// 资源类型（UBO / Texture / Sampler / StorageBuffer 等）
    vk::DescriptorType type { vk::DescriptorType::eUniformBuffer };

    /// array 数量（支持 descriptor array，例如 texture array）
    uint32_t count { 1 };

    /// shader 可见阶段（vertex / fragment / compute）
    vk::ShaderStageFlags stages { vk::ShaderStageFlagBits::eVertex };
};

/**
 * @brief DescriptorPool 中各类型资源的容量配置
 *
 * Vulkan DescriptorPool 本质是“固定容量内存池”
 * 必须提前声明各类型 descriptor 的最大数量
 */
struct DescriptorPoolSize {

    /// descriptor 类型
    vk::DescriptorType type { vk::DescriptorType::eUniformBuffer };

    /// 此类型最多可分配多少个 descriptor
    uint32_t count { 0 };
};

/**
 * @class DescriptorSetLayout
 * @brief 描述符布局（Shader Interface Definition）
 *
 * 等价于：
 * - OpenGL: uniform layout
 * - D3D12: root signature
 *
 * 决定 shader 能访问哪些资源，以及绑定结构
 */
class DescriptorSetLayout {
public:
    /// 可移动不可复制
    DescriptorSetLayout() = default;
    DescriptorSetLayout(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout(DescriptorSetLayout &&other) noexcept;
    DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout &operator=(DescriptorSetLayout &&other) noexcept;

    ~DescriptorSetLayout();

    /**
     * @brief 创建 descriptor set layout
     *
     * 内部会转换 DescriptorBinding → `vk::DescriptorSetLayoutBinding`
     *
     * Vulkan API：
     * vkCreateDescriptorSetLayout
     */
    bool create(const Context &ctx,
                const std::vector<DescriptorBinding> &bindings);

    [[nodiscard]] vk::DescriptorSetLayout handle() const { return layout_; }

    [[nodiscard]] bool is_valid() const { return static_cast<bool>(layout_); }

private:
    void destroy_();

    vk::Device device_ {};
    vk::DescriptorSetLayout layout_ {};
};

/**
 * @class DescriptorPool
 * @brief Descriptor 分配池（类似 allocator）
 *
 * Vulkan DescriptorPool 是预分配系统：
 * - 所有 DescriptorSet 必须从 pool 分配
 * - pool size 必须提前声明
 */
class DescriptorPool {
public:
    DescriptorPool() = default;

    DescriptorPool(const DescriptorPool &) = delete;
    DescriptorPool(DescriptorPool &&other) noexcept;

    DescriptorPool &operator=(const DescriptorPool &) = delete;
    DescriptorPool &operator=(DescriptorPool &&other) noexcept;

    ~DescriptorPool();

    /**
     * @brief 创建 descriptor pool
     *
     * maxSets：最多可分配多少 DescriptorSet
     * poolSizes：各类型 descriptor 容量
     *
     * Vulkan API：
     * vkCreateDescriptorPool
     */
    bool create(const Context &ctx,
                const std::vector<DescriptorPoolSize> &poolSizes,
                uint32_t maxSets);

    /**
     * @brief 分配 DescriptorSet
     *
     * Vulkan API：
     * vkAllocateDescriptorSets
     */
    bool allocate(vk::Device device, vk::DescriptorSetLayout layout,
                  vk::DescriptorSet &outSet);

    /**
     * @brief 重置 pool（释放所有 DescriptorSet）
     *
     * Vulkan API：
     * vkResetDescriptorPool
     *
     * ⚠️ 会使所有 descriptor set 失效
     */
    void reset();

    [[nodiscard]] vk::DescriptorPool handle() const { return pool_; }
    [[nodiscard]] bool is_valid() const { return static_cast<bool>(pool_); }

private:
    void destroy_();

    vk::Device device_ {};
    vk::DescriptorPool pool_ {};
};

/**
 * @brief 批量更新中的一项：UBO / SSBO 等 buffer 类 binding
 */
struct DescriptorWriteBuffer {
    uint32_t binding { 0 };
    vk::DescriptorType type { vk::DescriptorType::eUniformBuffer };
    vk::Buffer buffer {};
    size_t offset { 0 };
    size_t range { 0 };
};

/**
 * @brief 批量更新中的一项：`COMBINED_IMAGE_SAMPLER` binding
 */
struct DescriptorWriteImage {
    uint32_t binding { 0 };
    vk::ImageView imageView {};
    vk::Sampler sampler {};
    vk::ImageLayout imageLayout { vk::ImageLayout::eShaderReadOnlyOptimal };
};

/**
 * @brief 对同一 DescriptorSet 批量写入，单次 vkUpdateDescriptorSets
 *
 * 同一 set 上既有 buffer 又有 image 时应优先用本函数，避免多次驱动入口。
 */
void write_descriptor_set(vk::Device device, vk::DescriptorSet set,
                          std::initializer_list<DescriptorWriteBuffer> buffers,
                          std::initializer_list<DescriptorWriteImage> images);

/**
 * @brief 写入 Buffer（UBO / SSBO）到 DescriptorSet
 *
 * 单 binding 便捷封装；同 set 多 binding 请用 write_descriptor_set。
 */
void write_descriptor_buffer(vk::Device device, vk::DescriptorSet set,
                             uint32_t binding, vk::DescriptorType type,
                             vk::Buffer buffer, size_t offset, size_t range);

/**
 * @brief 写入 Image + Sampler 到 DescriptorSet
 *
 * 单 binding 便捷封装；同 set 多 binding 请用 write_descriptor_set。
 */
void write_descriptor_image(
    vk::Device device, vk::DescriptorSet set, uint32_t binding,
    vk::ImageView imageView, vk::Sampler sampler,

    /// 必须匹配 image 当前 layout，否则 shader 读取 undefined
    vk::ImageLayout imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal);

} // namespace render
} // namespace lumen
