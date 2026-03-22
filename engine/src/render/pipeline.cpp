/**
 * @file pipeline.cpp
 * @brief Pipeline 实现
 */

#include "render/pipeline.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"


#include <fstream>

namespace lumen {
namespace render {

// --- PipelineLayout ---

bool PipelineLayout::create(
    const Context &ctx, const std::vector<VkDescriptorSetLayout> &setLayouts,
    const std::vector<VkPushConstantRange> &pushConstantRanges) {
    device_ = ctx.device();

    VkPipelineLayoutCreateInfo createInfo {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    createInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    createInfo.pushConstantRangeCount =
        static_cast<uint32_t>(pushConstantRanges.size());
    createInfo.pPushConstantRanges =
        pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

    VkResult result =
        vkCreatePipelineLayout(device_, &createInfo, nullptr, &layout_);
    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("PipelineLayout 创建成功, setLayouts={}",
                        setLayouts.size());
    }
    return result == VK_SUCCESS;
}

void PipelineLayout::destroy_() {
    if (layout_ != VK_NULL_HANDLE) {
        LUMEN_LOG_DEBUG("销毁 PipelineLayout");
        vkDestroyPipelineLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

PipelineLayout::~PipelineLayout() { destroy_(); }

PipelineLayout::PipelineLayout(PipelineLayout &&other) noexcept
    : device_ { other.device_ }, layout_ { other.layout_ } {
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
}

PipelineLayout &PipelineLayout::operator=(PipelineLayout &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    layout_ = other.layout_;
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
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

    VkPipelineCacheCreateInfo createInfo {
        VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
    };
    createInfo.initialDataSize = data.size();
    createInfo.pInitialData = data.empty() ? nullptr : data.data();

    VkResult result =
        vkCreatePipelineCache(device_, &createInfo, nullptr, &cache_);
    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("PipelineCache 创建成功, initData={} bytes",
                        data.size());
    }
    return result == VK_SUCCESS;
}

bool PipelineCache::save_to_file(const char *filePath) {
    if (!cache_ || !filePath)
        return false;

    size_t size { 0 };
    VkResult result = vkGetPipelineCacheData(device_, cache_, &size, nullptr);
    if (result != VK_SUCCESS || size == 0)
        return false;

    std::vector<uint8_t> data(size);
    result = vkGetPipelineCacheData(device_, cache_, &size, data.data());
    if (result != VK_SUCCESS)
        return false;

    std::ofstream file { filePath, std::ios::binary };
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(data.data()), size);
    return true;
}

void PipelineCache::destroy_() {
    if (cache_ != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(device_, cache_, nullptr);
        cache_ = VK_NULL_HANDLE;
    }
}

PipelineCache::~PipelineCache() { destroy_(); }

PipelineCache::PipelineCache(PipelineCache &&other) noexcept
    : device_ { other.device_ }, cache_ { other.cache_ } {
    other.device_ = VK_NULL_HANDLE;
    other.cache_ = VK_NULL_HANDLE;
}

PipelineCache &PipelineCache::operator=(PipelineCache &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    cache_ = other.cache_;
    other.device_ = VK_NULL_HANDLE;
    other.cache_ = VK_NULL_HANDLE;
    return *this;
}

// --- GraphicsPipeline ---

bool GraphicsPipeline::create(const Context &ctx,
                              VkPipelineLayout pipelineLayout,
                              VkRenderPass renderPass, uint32_t subpassIndex,
                              const GraphicsPipelineConfig &config,
                              VkPipelineCache cache) {
    device_ = ctx.device();

    std::vector<VkPipelineShaderStageCreateInfo> stageInfos(
        config.stages.size());
    for (size_t i { 0 }; i < config.stages.size(); ++i) {
        stageInfos[i].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[i].stage = config.stages[i].stage;
        stageInfos[i].module = config.stages[i].module;
        stageInfos[i].pName = config.stages[i].entryPoint;
    }

    std::vector<VkVertexInputBindingDescription> bindings(
        config.vertexBindings.size());
    for (size_t i { 0 }; i < config.vertexBindings.size(); ++i) {
        bindings[i].binding = config.vertexBindings[i].binding;
        bindings[i].stride = config.vertexBindings[i].stride;
        bindings[i].inputRate = config.vertexBindings[i].inputRate;
    }

    std::vector<VkVertexInputAttributeDescription> attrs(
        config.vertexAttributes.size());
    for (size_t i { 0 }; i < config.vertexAttributes.size(); ++i) {
        attrs[i].location = config.vertexAttributes[i].location;
        attrs[i].binding = config.vertexAttributes[i].binding;
        attrs[i].format = config.vertexAttributes[i].format;
        attrs[i].offset = config.vertexAttributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    vertexInput.vertexBindingDescriptionCount =
        static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions =
        bindings.empty() ? nullptr : bindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions =
        attrs.empty() ? nullptr : attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssembly.topology = config.topology;

    VkPipelineViewportStateCreateInfo viewportState {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
    };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
    };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
    };
    depthStencil.depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;

    VkPipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (config.alphaBlend) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        colorBlendAttachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
    };
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates { VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
    };
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
    };
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

    VkResult result = vkCreateGraphicsPipelines(
        device_, cache, 1, &pipelineInfo, nullptr, &pipeline_);
    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("GraphicsPipeline 创建成功");
    } else {
        LUMEN_LOG_ERROR("vkCreateGraphicsPipelines 失败: {}",
                        static_cast<int>(result));
    }
    return result == VK_SUCCESS;
}

void GraphicsPipeline::destroy_() {
    if (pipeline_ != VK_NULL_HANDLE) {
        LUMEN_LOG_DEBUG("销毁 GraphicsPipeline");
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
}

GraphicsPipeline::~GraphicsPipeline() { destroy_(); }

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline &&other) noexcept
    : device_ { other.device_ }, pipeline_ { other.pipeline_ } {
    other.device_ = VK_NULL_HANDLE;
    other.pipeline_ = VK_NULL_HANDLE;
}

GraphicsPipeline &
GraphicsPipeline::operator=(GraphicsPipeline &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    device_ = other.device_;
    pipeline_ = other.pipeline_;
    other.device_ = VK_NULL_HANDLE;
    other.pipeline_ = VK_NULL_HANDLE;
    return *this;
}

} // namespace render
} // namespace lumen
