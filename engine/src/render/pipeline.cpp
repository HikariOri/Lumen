/**
 * @file pipeline.cpp
 * @brief Pipeline 实现
 */

#include "render/pipeline.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"

#include <fstream>
#include <vector>

namespace lumen {
namespace render {

namespace {

[[nodiscard]] VkVertexInputRate
to_vk_vertex_input_rate(VertexInputRate r) noexcept {
    switch (r) {
    case VertexInputRate::PerVertex: return VK_VERTEX_INPUT_RATE_VERTEX;
    case VertexInputRate::PerInstance: return VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    return VK_VERTEX_INPUT_RATE_VERTEX;
}

void push_attr(std::vector<VkVertexInputAttributeDescription> &out,
               uint32_t location, uint32_t binding, VkFormat format,
               uint32_t offset) {
    out.push_back(VkVertexInputAttributeDescription { location, binding, format,
                                                      offset });
}

void push_matrix_cols(std::vector<VkVertexInputAttributeDescription> &out,
                      uint32_t baseLocation, uint32_t binding,
                      uint32_t baseOffset, uint32_t columnCount,
                      uint32_t columnStrideBytes, VkFormat columnFormat) {
    for (uint32_t c { 0 }; c < columnCount; ++c) {
        push_attr(out, baseLocation + c, binding, columnFormat,
                  baseOffset + c * columnStrideBytes);
    }
}

void append_vertex_input_attributes(
    const VertexInputAttribute &a,
    std::vector<VkVertexInputAttributeDescription> &out) {
    const uint32_t L { a.location };
    const uint32_t B { a.binding };
    const uint32_t O { a.offset };
    using F = VertexAttributeFormat;

    switch (a.format) {
    case F::F64: push_attr(out, L, B, VK_FORMAT_R64_SFLOAT, O); return;
    case F::F64Vec2: push_attr(out, L, B, VK_FORMAT_R64G64_SFLOAT, O); return;
    case F::F64Vec3:
        push_attr(out, L, B, VK_FORMAT_R64G64B64_SFLOAT, O);
        return;
    case F::F64Vec4:
        push_attr(out, L, B, VK_FORMAT_R64G64B64A64_SFLOAT, O);
        return;
    case F::F64Mat2:
        push_matrix_cols(out, L, B, O, 2, 16, VK_FORMAT_R64G64_SFLOAT);
        return;
    case F::F64Mat3:
        push_matrix_cols(out, L, B, O, 3, 24, VK_FORMAT_R64G64B64_SFLOAT);
        return;
    case F::F64Mat4:
        push_matrix_cols(out, L, B, O, 4, 32, VK_FORMAT_R64G64B64A64_SFLOAT);
        return;

    case F::F32: push_attr(out, L, B, VK_FORMAT_R32_SFLOAT, O); return;
    case F::F32Vec2: push_attr(out, L, B, VK_FORMAT_R32G32_SFLOAT, O); return;
    case F::F32Vec3:
        push_attr(out, L, B, VK_FORMAT_R32G32B32_SFLOAT, O);
        return;
    case F::F32Vec4:
        push_attr(out, L, B, VK_FORMAT_R32G32B32A32_SFLOAT, O);
        return;
    case F::F32Mat2:
        push_matrix_cols(out, L, B, O, 2, 8, VK_FORMAT_R32G32_SFLOAT);
        return;
    case F::F32Mat3:
        push_matrix_cols(out, L, B, O, 3, 12, VK_FORMAT_R32G32B32_SFLOAT);
        return;
    case F::F32Mat4:
        push_matrix_cols(out, L, B, O, 4, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
        return;

    case F::F16: push_attr(out, L, B, VK_FORMAT_R16_SFLOAT, O); return;
    case F::F16Vec2: push_attr(out, L, B, VK_FORMAT_R16G16_SFLOAT, O); return;
    case F::F16Vec3:
        push_attr(out, L, B, VK_FORMAT_R16G16B16_SFLOAT, O);
        return;
    case F::F16Vec4:
        push_attr(out, L, B, VK_FORMAT_R16G16B16A16_SFLOAT, O);
        return;
    case F::F16Mat2:
        push_matrix_cols(out, L, B, O, 2, 4, VK_FORMAT_R16G16_SFLOAT);
        return;
    case F::F16Mat3:
        push_matrix_cols(out, L, B, O, 3, 6, VK_FORMAT_R16G16B16_SFLOAT);
        return;
    case F::F16Mat4:
        push_matrix_cols(out, L, B, O, 4, 8, VK_FORMAT_R16G16B16A16_SFLOAT);
        return;

    case F::I32: push_attr(out, L, B, VK_FORMAT_R32_SINT, O); return;
    case F::I32Vec2: push_attr(out, L, B, VK_FORMAT_R32G32_SINT, O); return;
    case F::I32Vec3: push_attr(out, L, B, VK_FORMAT_R32G32B32_SINT, O); return;
    case F::I32Vec4:
        push_attr(out, L, B, VK_FORMAT_R32G32B32A32_SINT, O);
        return;
    case F::I32Mat2:
        push_matrix_cols(out, L, B, O, 2, 8, VK_FORMAT_R32G32_SINT);
        return;
    case F::I32Mat3:
        push_matrix_cols(out, L, B, O, 3, 12, VK_FORMAT_R32G32B32_SINT);
        return;
    case F::I32Mat4:
        push_matrix_cols(out, L, B, O, 4, 16, VK_FORMAT_R32G32B32A32_SINT);
        return;

    case F::U32: push_attr(out, L, B, VK_FORMAT_R32_UINT, O); return;
    case F::U32Vec2: push_attr(out, L, B, VK_FORMAT_R32G32_UINT, O); return;
    case F::U32Vec3: push_attr(out, L, B, VK_FORMAT_R32G32B32_UINT, O); return;
    case F::U32Vec4:
        push_attr(out, L, B, VK_FORMAT_R32G32B32A32_UINT, O);
        return;
    case F::U32Mat2:
        push_matrix_cols(out, L, B, O, 2, 8, VK_FORMAT_R32G32_UINT);
        return;
    case F::U32Mat3:
        push_matrix_cols(out, L, B, O, 3, 12, VK_FORMAT_R32G32B32_UINT);
        return;
    case F::U32Mat4:
        push_matrix_cols(out, L, B, O, 4, 16, VK_FORMAT_R32G32B32A32_UINT);
        return;

    case F::I16: push_attr(out, L, B, VK_FORMAT_R16_SINT, O); return;
    case F::I16Vec2: push_attr(out, L, B, VK_FORMAT_R16G16_SINT, O); return;
    case F::I16Vec3: push_attr(out, L, B, VK_FORMAT_R16G16B16_SINT, O); return;
    case F::I16Vec4:
        push_attr(out, L, B, VK_FORMAT_R16G16B16A16_SINT, O);
        return;
    case F::I16Mat2:
        push_matrix_cols(out, L, B, O, 2, 4, VK_FORMAT_R16G16_SINT);
        return;
    case F::I16Mat3:
        push_matrix_cols(out, L, B, O, 3, 6, VK_FORMAT_R16G16B16_SINT);
        return;
    case F::I16Mat4:
        push_matrix_cols(out, L, B, O, 4, 8, VK_FORMAT_R16G16B16A16_SINT);
        return;

    case F::U16: push_attr(out, L, B, VK_FORMAT_R16_UINT, O); return;
    case F::U16Vec2: push_attr(out, L, B, VK_FORMAT_R16G16_UINT, O); return;
    case F::U16Vec3: push_attr(out, L, B, VK_FORMAT_R16G16B16_UINT, O); return;
    case F::U16Vec4:
        push_attr(out, L, B, VK_FORMAT_R16G16B16A16_UINT, O);
        return;
    case F::U16Mat2:
        push_matrix_cols(out, L, B, O, 2, 4, VK_FORMAT_R16G16_UINT);
        return;
    case F::U16Mat3:
        push_matrix_cols(out, L, B, O, 3, 6, VK_FORMAT_R16G16B16_UINT);
        return;
    case F::U16Mat4:
        push_matrix_cols(out, L, B, O, 4, 8, VK_FORMAT_R16G16B16A16_UINT);
        return;

    case F::I8: push_attr(out, L, B, VK_FORMAT_R8_SINT, O); return;
    case F::I8Vec2: push_attr(out, L, B, VK_FORMAT_R8G8_SINT, O); return;
    case F::I8Vec3: push_attr(out, L, B, VK_FORMAT_R8G8B8_SINT, O); return;
    case F::I8Vec4: push_attr(out, L, B, VK_FORMAT_R8G8B8A8_SINT, O); return;
    case F::I8Mat2:
        push_matrix_cols(out, L, B, O, 2, 2, VK_FORMAT_R8G8_SINT);
        return;
    case F::I8Mat3:
        push_matrix_cols(out, L, B, O, 3, 3, VK_FORMAT_R8G8B8_SINT);
        return;
    case F::I8Mat4:
        push_matrix_cols(out, L, B, O, 4, 4, VK_FORMAT_R8G8B8A8_SINT);
        return;

    case F::U8: push_attr(out, L, B, VK_FORMAT_R8_UINT, O); return;
    case F::U8Vec2: push_attr(out, L, B, VK_FORMAT_R8G8_UINT, O); return;
    case F::U8Vec3: push_attr(out, L, B, VK_FORMAT_R8G8B8_UINT, O); return;
    case F::U8Vec4: push_attr(out, L, B, VK_FORMAT_R8G8B8A8_UINT, O); return;
    case F::U8Mat2:
        push_matrix_cols(out, L, B, O, 2, 2, VK_FORMAT_R8G8_UINT);
        return;
    case F::U8Mat3:
        push_matrix_cols(out, L, B, O, 3, 3, VK_FORMAT_R8G8B8_UINT);
        return;
    case F::U8Mat4:
        push_matrix_cols(out, L, B, O, 4, 4, VK_FORMAT_R8G8B8A8_UINT);
        return;

    case F::Unorm8: push_attr(out, L, B, VK_FORMAT_R8_UNORM, O); return;
    case F::Unorm8Vec2: push_attr(out, L, B, VK_FORMAT_R8G8_UNORM, O); return;
    case F::Unorm8Vec3: push_attr(out, L, B, VK_FORMAT_R8G8B8_UNORM, O); return;
    case F::Unorm8Vec4:
        push_attr(out, L, B, VK_FORMAT_R8G8B8A8_UNORM, O);
        return;

    case F::Snorm8: push_attr(out, L, B, VK_FORMAT_R8_SNORM, O); return;
    case F::Snorm8Vec2: push_attr(out, L, B, VK_FORMAT_R8G8_SNORM, O); return;
    case F::Snorm8Vec3: push_attr(out, L, B, VK_FORMAT_R8G8B8_SNORM, O); return;
    case F::Snorm8Vec4:
        push_attr(out, L, B, VK_FORMAT_R8G8B8A8_SNORM, O);
        return;

    case F::Unorm16: push_attr(out, L, B, VK_FORMAT_R16_UNORM, O); return;
    case F::Unorm16Vec2:
        push_attr(out, L, B, VK_FORMAT_R16G16_UNORM, O);
        return;
    case F::Unorm16Vec3:
        push_attr(out, L, B, VK_FORMAT_R16G16B16_UNORM, O);
        return;
    case F::Unorm16Vec4:
        push_attr(out, L, B, VK_FORMAT_R16G16B16A16_UNORM, O);
        return;

    case F::Snorm16: push_attr(out, L, B, VK_FORMAT_R16_SNORM, O); return;
    case F::Snorm16Vec2:
        push_attr(out, L, B, VK_FORMAT_R16G16_SNORM, O);
        return;
    case F::Snorm16Vec3:
        push_attr(out, L, B, VK_FORMAT_R16G16B16_SNORM, O);
        return;
    case F::Snorm16Vec4:
        push_attr(out, L, B, VK_FORMAT_R16G16B16A16_SNORM, O);
        return;

    case F::Srgb8: push_attr(out, L, B, VK_FORMAT_R8_SRGB, O); return;
    case F::Srgb8Vec2: push_attr(out, L, B, VK_FORMAT_R8G8_SRGB, O); return;
    case F::Srgb8Vec3: push_attr(out, L, B, VK_FORMAT_R8G8B8_SRGB, O); return;
    case F::Srgb8Vec4: push_attr(out, L, B, VK_FORMAT_R8G8B8A8_SRGB, O); return;
    default: return;
    }
}

} // namespace

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
        config.shaderStages.size());
    for (size_t i { 0 }; i < config.shaderStages.size(); ++i) {
        stageInfos[i].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[i].stage = config.shaderStages[i].stage;
        stageInfos[i].module = config.shaderStages[i].module;
        stageInfos[i].pName = config.shaderStages[i].entryPoint;
    }

    std::vector<VkVertexInputBindingDescription> bindings(
        config.vertexBindings.size());
    for (size_t i { 0 }; i < config.vertexBindings.size(); ++i) {
        bindings[i].binding = config.vertexBindings[i].binding;
        bindings[i].stride = config.vertexBindings[i].stride;
        bindings[i].inputRate =
            to_vk_vertex_input_rate(config.vertexBindings[i].inputRate);
    }

    std::vector<VkVertexInputAttributeDescription> attrs;
    attrs.reserve(config.vertexAttributes.size() * 4);
    for (const VertexInputAttribute &va : config.vertexAttributes) {
        append_vertex_input_attributes(va, attrs);
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
    rasterizer.lineWidth = 1.0F;

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

bool GraphicsPipeline::create(const Context &ctx,
                              const PipelineLayout &pipelineLayout,
                              const RenderPass &renderPass,
                              uint32_t subpassIndex,
                              const GraphicsPipelineConfig &config,
                              VkPipelineCache cache) {
    return create(ctx, pipelineLayout.handle(), renderPass.handle(),
                  subpassIndex, config, cache);
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
