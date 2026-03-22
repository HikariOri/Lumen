/**
 * @file pipeline.hpp
 * @brief 管线布局、Graphics 管线、PipelineCache
 *
 * 封装 PipelineLayout、GraphicsPipeline、PipelineCache 的创建与管理。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

class Context;
class DescriptorSetLayout;
class RenderPass;

/// 着色器阶段
struct ShaderStage {
    VkShaderModule module { VK_NULL_HANDLE };
    VkShaderStageFlagBits stage { VK_SHADER_STAGE_VERTEX_BIT };
    const char *entryPoint { "main" };
};

/// 顶点输入绑定
struct VertexInputBinding {
    uint32_t binding { 0 };
    uint32_t stride { 0 };
    VkVertexInputRate inputRate { VK_VERTEX_INPUT_RATE_VERTEX };
};

/// 顶点属性
struct VertexInputAttribute {
    uint32_t location { 0 };
    uint32_t binding { 0 };
    VkFormat format { VK_FORMAT_R32G32B32_SFLOAT };
    uint32_t offset { 0 };
};

/// 图形管线配置
struct GraphicsPipelineConfig {
    std::vector<ShaderStage> stages {};
    std::vector<VertexInputBinding> vertexBindings {};
    std::vector<VertexInputAttribute> vertexAttributes {};
    VkPrimitiveTopology topology { VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPolygonMode polygonMode { VK_POLYGON_MODE_FILL };
    VkCullModeFlags cullMode { VK_CULL_MODE_BACK_BIT };
    VkFrontFace frontFace {
        VK_FRONT_FACE_CLOCKWISE
    }; // 与 proj[1][1]*=-1 配合，见 docs/reference/glm-vulkan.md
    bool depthTest { true };
    bool depthWrite { true };
    VkCompareOp depthCompareOp { VK_COMPARE_OP_LESS };
    /// 预乘前开启典型 SrcAlpha / OneMinusSrcAlpha 颜色混合（图标、粒子等）
    bool alphaBlend { false };
};

/**
 * @class PipelineLayout
 * @brief 管线布局（Push Constant、Descriptor 布局）
 */
class PipelineLayout {
public:
    PipelineLayout() = default;
    PipelineLayout(const PipelineLayout &) = delete;
    PipelineLayout(PipelineLayout &&other) noexcept;
    PipelineLayout &operator=(const PipelineLayout &) = delete;
    PipelineLayout &operator=(PipelineLayout &&other) noexcept;
    ~PipelineLayout();

    /**
     * @brief 创建管线布局
     * @param ctx Context
     * @param setLayouts DescriptorSetLayout 列表
     * @param pushConstantRanges Push Constant 范围（可为空）
     */
    bool
    create(const Context &ctx,
           const std::vector<VkDescriptorSetLayout> &setLayouts,
           const std::vector<VkPushConstantRange> &pushConstantRanges = {});

    [[nodiscard]] VkPipelineLayout handle() const { return layout_; }
    [[nodiscard]] bool is_valid() const { return layout_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkPipelineLayout layout_ { VK_NULL_HANDLE };
};

/**
 * @class PipelineCache
 * @brief 管线缓存，用于加速管线创建
 */
class PipelineCache {
public:
    PipelineCache() = default;
    PipelineCache(const PipelineCache &) = delete;
    PipelineCache(PipelineCache &&other) noexcept;
    PipelineCache &operator=(const PipelineCache &) = delete;
    PipelineCache &operator=(PipelineCache &&other) noexcept;
    ~PipelineCache();

    /**
     * @brief 创建缓存（可从文件加载已有数据）
     * @param ctx Context
     * @param filePath 持久化文件路径，空则仅内存缓存
     */
    bool create(const Context &ctx, const char *filePath = nullptr);

    /**
     * @brief 将缓存写入文件
     */
    bool save_to_file(const char *filePath);

    [[nodiscard]] VkPipelineCache handle() const { return cache_; }
    [[nodiscard]] bool is_valid() const { return cache_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkPipelineCache cache_ { VK_NULL_HANDLE };
};

/**
 * @class GraphicsPipeline
 * @brief 图形管线
 */
class GraphicsPipeline {
public:
    GraphicsPipeline() = default;
    GraphicsPipeline(const GraphicsPipeline &) = delete;
    GraphicsPipeline(GraphicsPipeline &&other) noexcept;
    GraphicsPipeline &operator=(const GraphicsPipeline &) = delete;
    GraphicsPipeline &operator=(GraphicsPipeline &&other) noexcept;
    ~GraphicsPipeline();

    /**
     * @brief 创建图形管线
     * @param ctx Context
     * @param pipelineLayout 管线布局
     * @param renderPass 渲染通道
     * @param subpassIndex 子通道索引
     * @param config 管线配置
     * @param cache 可选管线缓存
     */
    bool create(const Context &ctx, VkPipelineLayout pipelineLayout,
                VkRenderPass renderPass, uint32_t subpassIndex,
                const GraphicsPipelineConfig &config,
                VkPipelineCache cache = VK_NULL_HANDLE);

    [[nodiscard]] VkPipeline handle() const { return pipeline_; }
    [[nodiscard]] bool is_valid() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    void destroy_();

    VkDevice device_ { VK_NULL_HANDLE };
    VkPipeline pipeline_ { VK_NULL_HANDLE };
};

} // namespace render
} // namespace lumen
