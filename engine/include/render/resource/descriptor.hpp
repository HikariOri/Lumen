/**
 * @file descriptor.hpp
 * @brief 描述符池、布局、集：UBO、纹理、Sampler 绑定
 *
 * 管理 DescriptorSetLayout、DescriptorPool、DescriptorSet 的创建与绑定。
 */

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;

/// 描述符类型绑定
struct DescriptorBinding {
    uint32_t binding { 0 };
    VkDescriptorType type { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
    uint32_t count { 1 };
    VkShaderStageFlags stages { VK_SHADER_STAGE_VERTEX_BIT };
};

/// 描述符池大小（按类型）
struct DescriptorPoolSize {
    VkDescriptorType type { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
    uint32_t count { 0 };
};

/**
 * @class DescriptorSetLayout
 * @brief 描述符集布局
 */
class DescriptorSetLayout {
public:
    DescriptorSetLayout() = default;
    DescriptorSetLayout(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout(DescriptorSetLayout &&other) noexcept;
    DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;
    DescriptorSetLayout &operator=(DescriptorSetLayout &&other) noexcept;
    ~DescriptorSetLayout();

    /**
     * @brief 创建布局
     * @param ctx Context
     * @param bindings 绑定列表
     * @return 成功返回 true
     */
    bool create(const Context &ctx,
                const std::vector<DescriptorBinding> &bindings);

    [[nodiscard]] VkDescriptorSetLayout handle() const { return layout_; }
    [[nodiscard]] bool is_valid() const { return layout_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkDescriptorSetLayout layout_ { VK_NULL_HANDLE };
};

/**
 * @class DescriptorPool
 * @brief 描述符池
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
     * @brief 创建池
     * @param ctx Context
     * @param poolSizes 各类型数量
     * @param maxSets 最大 DescriptorSet 数量
     * @return 成功返回 true
     */
    bool create(const Context &ctx,
                const std::vector<DescriptorPoolSize> &poolSizes,
                uint32_t maxSets);

    /**
     * @brief 分配 DescriptorSet
     * @param layout 布局
     * @param outSet 输出句柄
     * @return 成功返回 true
     */
    bool allocate(VkDevice device, VkDescriptorSetLayout layout,
                  VkDescriptorSet &outSet);

    /**
     * @brief 重置池（释放所有已分配的 Set）
     */
    void reset();

    [[nodiscard]] VkDescriptorPool handle() const { return pool_; }
    [[nodiscard]] bool is_valid() const { return pool_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkDescriptorPool pool_ { VK_NULL_HANDLE };
};

/**
 * @brief 写入 UBO 到 DescriptorSet
 */
void write_descriptor_buffer(VkDevice device, VkDescriptorSet set,
                             uint32_t binding, VkDescriptorType type,
                             VkBuffer buffer, size_t offset, size_t range);

/**
 * @brief 写入 Image+Sampler 到 DescriptorSet
 */
void write_descriptor_image(
    VkDevice device, VkDescriptorSet set, uint32_t binding,
    VkImageView imageView, VkSampler sampler,
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

} // namespace render
} // namespace lumen
