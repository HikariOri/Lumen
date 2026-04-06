/**
 * @file pipeline.cpp
 * @brief Pipeline 实现
 */

#include "render/pipeline.hpp"
#include "core/log/logger.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"

#include <fstream>
#include <vector>

namespace lumen {
namespace render {

namespace {

void append_vertex_input_attributes(
    const VertexInputAttribute &a,
    std::vector<vk::VertexInputAttributeDescription> &out) {
    out.push_back(vk::VertexInputAttributeDescription {
        a.location, a.binding, a.format, a.offset });
}

} // namespace

// --- PipelineLayout ---

bool PipelineLayout::create(
    const Context &ctx, const std::vector<vk::DescriptorSetLayout> &setLayouts,
    const std::vector<vk::PushConstantRange> &pushConstantRanges) {
    device_ = ctx.device();

    vk::PipelineLayoutCreateInfo createInfo {};
    createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    createInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    createInfo.pushConstantRangeCount =
        static_cast<uint32_t>(pushConstantRanges.size());
    createInfo.pPushConstantRanges =
        pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

    const vk::Result result =
        device_.createPipelineLayout(&createInfo, nullptr, &layout_);
    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("PipelineLayout 创建成功, setLayouts={}",
                        setLayouts.size());
    }
    return result == vk::Result::eSuccess;
}

void PipelineLayout::destroy_() {
    if (layout_) {
        LUMEN_LOG_DEBUG("销毁 PipelineLayout");
        device_.destroyPipelineLayout(layout_, nullptr);
        layout_ = nullptr;
    }
}

PipelineLayout::~PipelineLayout() { destroy_(); }

PipelineLayout::PipelineLayout(PipelineLayout &&other) noexcept
    : device_ { other.device_ }, layout_ { other.layout_ } {
    other.device_ = nullptr;
    other.layout_ = nullptr;
}

PipelineLayout &PipelineLayout::operator=(PipelineLayout &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    layout_ = other.layout_;
    other.device_ = nullptr;
    other.layout_ = nullptr;
    return *this;
}

// --- PipelineCache ---

bool PipelineCache::create(const Context &ctx, const char *filePath) {
    device_ = ctx.device();

    std::vector<uint8_t> data;
    if (filePath) {
        std::ifstream file { filePath, std::ios::binary | std::ios::ate };
        if (file) {
            size_t size = file.tellg();
            file.seekg(0);
            data.resize(size);
            file.read(reinterpret_cast<char *>(data.data()), size);
        }
    }

    vk::PipelineCacheCreateInfo createInfo {};
    createInfo.initialDataSize = data.size();
    createInfo.pInitialData = data.empty() ? nullptr : data.data();

    const vk::Result result =
        device_.createPipelineCache(&createInfo, nullptr, &cache_);
    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("PipelineCache 创建成功, initData={} bytes",
                        data.size());
    }
    return result == vk::Result::eSuccess;
}

bool PipelineCache::save_to_file(const char *filePath) {
    if (!cache_ || !filePath)
        return false;

    size_t size { 0 };
    vk::Result result =
        device_.getPipelineCacheData(cache_, &size, nullptr);
    if (result != vk::Result::eSuccess || size == 0)
        return false;

    std::vector<uint8_t> data(size);
    result = device_.getPipelineCacheData(cache_, &size, data.data());
    if (result != vk::Result::eSuccess)
        return false;

    std::ofstream file { filePath, std::ios::binary };
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(data.data()), size);
    return true;
}

void PipelineCache::destroy_() {
    if (cache_) {
        device_.destroyPipelineCache(cache_, nullptr);
        cache_ = nullptr;
    }
}

PipelineCache::~PipelineCache() { destroy_(); }

PipelineCache::PipelineCache(PipelineCache &&other) noexcept
    : device_ { other.device_ }, cache_ { other.cache_ } {
    other.device_ = nullptr;
    other.cache_ = nullptr;
}

PipelineCache &PipelineCache::operator=(PipelineCache &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    cache_ = other.cache_;
    other.device_ = nullptr;
    other.cache_ = nullptr;
    return *this;
}

// --- GraphicsPipeline ---

bool GraphicsPipeline::create(const Context &ctx,
                              vk::PipelineLayout pipelineLayout,
                              vk::RenderPass renderPass, uint32_t subpassIndex,
                              const GraphicsPipelineConfig &config,
                              vk::PipelineCache cache) {
    device_ = ctx.device();

    std::vector<vk::PipelineShaderStageCreateInfo> stageInfos(
        config.shaderStages.size());
    for (size_t i { 0 }; i < config.shaderStages.size(); ++i) {
        stageInfos[i].stage = config.shaderStages[i].stage;
        stageInfos[i].module = config.shaderStages[i].module;
        stageInfos[i].pName = config.shaderStages[i].entryPoint;
    }

    std::vector<vk::VertexInputBindingDescription> bindings(
        config.vertexBindings.size());
    for (size_t i { 0 }; i < config.vertexBindings.size(); ++i) {
        bindings[i].binding = config.vertexBindings[i].binding;
        bindings[i].stride = config.vertexBindings[i].stride;
        bindings[i].inputRate = config.vertexBindings[i].inputRate;
    }

    std::vector<vk::VertexInputAttributeDescription> attrs;
    attrs.reserve(config.vertexAttributes.size());
    for (const VertexInputAttribute &va : config.vertexAttributes) {
        append_vertex_input_attributes(va, attrs);
    }

    vk::PipelineVertexInputStateCreateInfo vertexInput {};
    vertexInput.vertexBindingDescriptionCount =
        static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions =
        bindings.empty() ? nullptr : bindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions =
        attrs.empty() ? nullptr : attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly {};
    inputAssembly.topology = config.topology;

    vk::PipelineViewportStateCreateInfo viewportState {};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer {};
    rasterizer.depthClampEnable = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.lineWidth = 1.0F;

    vk::PipelineMultisampleStateCreateInfo multisample {};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo depthStencil {};
    depthStencil.depthTestEnable = config.depthTest ? vk::True : vk::False;
    depthStencil.depthWriteEnable = config.depthWrite ? vk::True : vk::False;
    depthStencil.depthCompareOp = config.depthCompareOp;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlags { vk::ColorComponentFlagBits::eR |
                                  vk::ColorComponentFlagBits::eG |
                                  vk::ColorComponentFlagBits::eB |
                                  vk::ColorComponentFlagBits::eA };
    if (config.alphaBlend) {
        colorBlendAttachment.blendEnable = vk::True;
        colorBlendAttachment.srcColorBlendFactor =
            vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor =
            vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor =
            vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    } else {
        colorBlendAttachment.blendEnable = vk::False;
    }

    vk::PipelineColorBlendStateCreateInfo colorBlend {};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    std::vector<vk::DynamicState> dynamicStates { vk::DynamicState::eViewport,
                                                  vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState {};
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::GraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.stageCount = static_cast<uint32_t>(stageInfos.size());
    pipelineInfo.pStages = stageInfos.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassIndex;

    const vk::Result result = device_.createGraphicsPipelines(
        cache, 1, &pipelineInfo, nullptr, &pipeline_);
    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("GraphicsPipeline 创建成功");
    } else {
        LUMEN_LOG_ERROR("createGraphicsPipelines 失败: {}",
                        static_cast<int>(result));
    }
    return result == vk::Result::eSuccess;
}

bool GraphicsPipeline::create(const Context &ctx,
                              const PipelineLayout &pipelineLayout,
                              const RenderPass &renderPass,
                              uint32_t subpassIndex,
                              const GraphicsPipelineConfig &config,
                              vk::PipelineCache cache) {
    return create(ctx, pipelineLayout.handle(), renderPass.handle(),
                  subpassIndex, config, cache);
}

void GraphicsPipeline::destroy_() {
    if (pipeline_) {
        LUMEN_LOG_DEBUG("销毁 GraphicsPipeline");
        device_.destroyPipeline(pipeline_, nullptr);
        pipeline_ = nullptr;
    }
}

GraphicsPipeline::~GraphicsPipeline() { destroy_(); }

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline &&other) noexcept
    : device_ { other.device_ }, pipeline_ { other.pipeline_ } {
    other.device_ = nullptr;
    other.pipeline_ = nullptr;
}

GraphicsPipeline &
GraphicsPipeline::operator=(GraphicsPipeline &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    pipeline_ = other.pipeline_;
    other.device_ = nullptr;
    other.pipeline_ = nullptr;
    return *this;
}

} // namespace render
} // namespace lumen
