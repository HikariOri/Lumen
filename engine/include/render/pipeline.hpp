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

#include "render/vulkan.hpp"

namespace lumen {
namespace render {

class Context;
class DescriptorSetLayout;
class RenderPass;

struct ShaderStage {
    vk::ShaderModule module {};
    vk::ShaderStageFlagBits stage { vk::ShaderStageFlagBits::eVertex };
    const char *entryPoint { "main" };
};

/// 顶点输入绑定
struct VertexInputBinding {
    uint32_t binding { 0 };
    uint32_t stride { 0 };
    vk::VertexInputRate inputRate { vk::VertexInputRate::eVertex };
};

/// 顶点属性（与 `VkVertexInputAttributeDescription` 一一对应；矩阵需按列拆成多条）
struct VertexInputAttribute {
    uint32_t location { 0 };
    uint32_t binding { 0 };
    vk::Format format { vk::Format::eR32G32B32Sfloat };
    uint32_t offset { 0 };
};

/// 图形管线配置
struct GraphicsPipelineConfig {
    std::vector<ShaderStage> shaderStages {};
    std::vector<VertexInputBinding> vertexBindings {};
    std::vector<VertexInputAttribute> vertexAttributes {};
    vk::PrimitiveTopology topology { vk::PrimitiveTopology::eTriangleList };
    vk::PolygonMode polygonMode { vk::PolygonMode::eFill };
    vk::CullModeFlags cullMode { vk::CullModeFlagBits::eBack };
    vk::FrontFace frontFace {
        vk::FrontFace::eClockwise
    }; // 与 proj[1][1]*=-1 配合，见 docs/reference/glm-vulkan.md
    bool depthTest { true };
    bool depthWrite { true };
    vk::CompareOp depthCompareOp { vk::CompareOp::eLess };
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
           const std::vector<vk::DescriptorSetLayout> &setLayouts,
           const std::vector<vk::PushConstantRange> &pushConstantRanges = {});

    [[nodiscard]] vk::PipelineLayout handle() const { return layout_; }
    [[nodiscard]] bool is_valid() const { return static_cast<bool>(layout_); }

private:
    void destroy_();

    vk::Device device_ {};
    vk::PipelineLayout layout_ {};
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

    [[nodiscard]] vk::PipelineCache handle() const { return cache_; }
    [[nodiscard]] bool is_valid() const { return static_cast<bool>(cache_); }

private:
    void destroy_();

    vk::Device device_ {};
    vk::PipelineCache cache_ {};
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
    bool create(const Context &ctx, vk::PipelineLayout pipelineLayout,
                vk::RenderPass renderPass, uint32_t subpassIndex,
                const GraphicsPipelineConfig &config,
                vk::PipelineCache cache = {});

    bool create(const Context &ctx, const PipelineLayout &pipelineLayout,
                const RenderPass &renderPass, uint32_t subpassIndex,
                const GraphicsPipelineConfig &config,
                vk::PipelineCache cache = {});

    [[nodiscard]] vk::Pipeline handle() const { return pipeline_; }
    [[nodiscard]] bool is_valid() const { return static_cast<bool>(pipeline_); }

private:
    void destroy_();

    vk::Device device_ {};
    vk::Pipeline pipeline_ {};
};

} // namespace render
} // namespace lumen
