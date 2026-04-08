/**
 * @file pipeline.cpp
 * @brief `GraphicsPipelineBuilder` 实现：图形管线各阶段状态组装与
 * `vkCreateGraphicsPipelines`。
 */

#include "vulkan/pipeline.hpp"

#include "core/log/logger.hpp"

#include <algorithm>

namespace vulkan {

namespace {

[[nodiscard]] VkPipelineColorBlendAttachmentState
make_blend_attachment_disabled() {
    VkPipelineColorBlendAttachmentState attachment {};
    attachment.blendEnable = VK_FALSE;
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return attachment;
}

} // namespace

GraphicsPipelineBuilder::GraphicsPipelineBuilder(VkDevice device)
    : device_(device) {}

GraphicsPipelineBuilder::~GraphicsPipelineBuilder() {
    destroy_shader_modules_();
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_vertex_shader(
    const std::vector<std::uint32_t> &spirv, std::string entry_name) {
    vertexEntry_ = std::move(entry_name);
    destroy_shader_module_(vertexShaderModule_);
    if (!create_shader_module_(spirv, vertexShaderModule_)) {
        LUMEN_LOG_ERROR("vertex shader module create failed");
    }
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_fragment_shader(
    const std::vector<std::uint32_t> &spirv, std::string entry_name) {
    fragmentEntry_ = std::move(entry_name);
    destroy_shader_module_(fragmentShaderModule_);
    if (!create_shader_module_(spirv, fragmentShaderModule_)) {
        LUMEN_LOG_ERROR("fragment shader module create failed");
    }
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_vertex_layout(
    const std::vector<VkVertexInputAttributeDescription> &vertex_attributes,
    const std::vector<VkVertexInputBindingDescription> &vertex_bindings) {
    vertexAttributes_ = vertex_attributes;
    vertexBindings_ = vertex_bindings;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_topology(VkPrimitiveTopology primitiveTopology) {
    primitiveTopology_ = primitiveTopology;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_primitive_restart(bool enable) {
    primitiveRestartEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_polygon_mode(VkPolygonMode polygonMode) {
    polygonMode_ = polygonMode;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_cull_mode(VkCullModeFlags cullMode) {
    cullMode_ = cullMode;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_front_face(VkFrontFace frontFace) {
    frontFace_ = frontFace;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_line_width(float lineWidth) {
    lineWidth_ = lineWidth;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_depth_clamp(bool enable) {
    depthClampEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_rasterizer_discard(bool enable) {
    rasterizerDiscardEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bias_enable(bool enable) {
    depthBiasEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bias_constant_factor(float value) {
    depthBiasConstantFactor_ = value;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bias_clamp(float value) {
    depthBiasClamp_ = value;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bias_slope_factor(float value) {
    depthBiasSlopeFactor_ = value;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_sample_count(
    VkSampleCountFlagBits rasterizationSamples) {
    rasterizationSamples_ = rasterizationSamples;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_sample_shading(bool enable,
                                            float minSampleShading) {
    sampleShadingEnable_ = enable ? VK_TRUE : VK_FALSE;
    minSampleShading_ = minSampleShading;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_min_sample_shading(float value) {
    minSampleShading_ = value;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_sample_mask(
    const std::vector<VkSampleMask> &masks) {
    sampleMask_ = masks;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_alpha_to_coverage(bool enable) {
    alphaToCoverageEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_alpha_to_one(bool enable) {
    alphaToOneEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_viewport(const std::vector<VkViewport> &viewports,
                                      const std::vector<VkRect2D> &scissors) {
    viewports_ = viewports;
    scissors_ = scissors;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_viewport_dynamic(uint32_t viewportCount,
                                              uint32_t scissorCount) {
    viewports_.clear();
    scissors_.clear();
    dynamicViewport_ = true;
    dynamicViewportCount_ = viewportCount;
    dynamicScissorCount_ = scissorCount;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_test(bool depthTest, bool depthWrite,
                                        VkCompareOp depthCompare) {
    depthTestEnable_ = depthTest ? VK_TRUE : VK_FALSE;
    depthWriteEnable_ = depthWrite ? VK_TRUE : VK_FALSE;
    depthCompareOp_ = depthCompare;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bounds_test(bool enable) {
    depthBoundsTestEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_stencil_test(bool enable) {
    stencilTestEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_stencil_front(const VkStencilOpState &state) {
    stencilFront_ = state;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_stencil_back(const VkStencilOpState &state) {
    stencilBack_ = state;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_stencil_ops(const VkStencilOpState &state) {
    stencilFront_ = state;
    stencilBack_ = state;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_depth_bounds(float min_bounds, float max_bounds) {
    minDepthBounds_ = min_bounds;
    maxDepthBounds_ = max_bounds;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_blend_off() {
    colorBlendAttachments_.clear();
    colorBlendAttachments_.push_back(make_blend_attachment_disabled());
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_blend_alpha() {
    VkPipelineColorBlendAttachmentState attachment {};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments_.clear();
    colorBlendAttachments_.push_back(attachment);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_color_blend_attachments(
    std::vector<VkPipelineColorBlendAttachmentState> attachments) {
    colorBlendAttachments_ = std::move(attachments);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_blend_constants(float r,
                                                                      float g,
                                                                      float b,
                                                                      float a) {
    blendConstants_[0] = r;
    blendConstants_[1] = g;
    blendConstants_[2] = b;
    blendConstants_[3] = a;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_blend_constants(std::array<float, 4> rgba) {
    blendConstants_ = rgba;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_logic_op_enable(bool enable) {
    logicOpEnable_ = enable ? VK_TRUE : VK_FALSE;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_logic_op(VkLogicOp op) {
    logicOp_ = op;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_dynamic_state(std::vector<VkDynamicState> states) {
    dynamicStates_ = std::move(states);
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::add_dynamic_state(VkDynamicState state) {
    dynamicStates_.push_back(state);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_line_width() {
    add_dynamic_state_unique_(VK_DYNAMIC_STATE_LINE_WIDTH);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_cull_mode() {
    add_dynamic_state_unique_(VK_DYNAMIC_STATE_CULL_MODE);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_front_face() {
    add_dynamic_state_unique_(VK_DYNAMIC_STATE_FRONT_FACE);
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::add_dynamic_primitive_topology() {
    add_dynamic_state_unique_(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_polygon_mode() {
    add_dynamic_state_unique_(VK_DYNAMIC_STATE_POLYGON_MODE_EXT);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::add_dynamic_raster_common() {
    add_dynamic_line_width();
    add_dynamic_cull_mode();
    add_dynamic_front_face();
    add_dynamic_primitive_topology();
    add_dynamic_polygon_mode();
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_pipeline_layout(VkPipelineLayout pipelineLayout) {
    pipelineLayout_ = pipelineLayout;
    return *this;
}

GraphicsPipelineBuilder &
GraphicsPipelineBuilder::set_render_pass(VkRenderPass renderPass,
                                         uint32_t subpassIndex) {
    renderPass_ = renderPass;
    subpass_ = subpassIndex;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::set_pipeline_create_flags(
    VkPipelineCreateFlags pipelineCreateFlags) {
    pipelineCreateFlags_ = pipelineCreateFlags;
    return *this;
}

[[nodiscard]] VkPipeline GraphicsPipelineBuilder::build() {
    if (vertexShaderModule_ == VK_NULL_HANDLE) {
        LUMEN_LOG_ERROR("GraphicsPipelineBuilder: vertex shader not set");
        return VK_NULL_HANDLE;
    }
    if (pipelineLayout_ == VK_NULL_HANDLE || renderPass_ == VK_NULL_HANDLE) {
        LUMEN_LOG_ERROR("GraphicsPipelineBuilder: pipeline layout or "
                        "render pass not set");
        return VK_NULL_HANDLE;
    }
    if (!dynamicViewport_) {
        if (viewports_.empty() || scissors_.empty() ||
            viewports_.size() != scissors_.size()) {
            LUMEN_LOG_ERROR("GraphicsPipelineBuilder: static "
                            "viewport/scissor mismatch or "
                            "empty; use set_viewport_dynamic()");
            return VK_NULL_HANDLE;
        }
    }

    const std::vector<VkPipelineShaderStageCreateInfo> shaderStages =
        create_shader_stages_();
    if (shaderStages.empty()) {
        LUMEN_LOG_ERROR("GraphicsPipelineBuilder: no shader stages");
        return VK_NULL_HANDLE;
    }

    const VkPipelineVertexInputStateCreateInfo vertexInput =
        create_vertex_input_state_();
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly =
        create_input_assembly_state_();
    const VkPipelineViewportStateCreateInfo viewportState =
        create_viewport_state_();
    const VkPipelineRasterizationStateCreateInfo rasterization =
        create_rasterization_state_();
    const VkPipelineMultisampleStateCreateInfo multisampleState =
        create_multisample_state_();
    const VkPipelineDepthStencilStateCreateInfo depthStencilState =
        create_depth_stencil_state_();

    ensure_color_blend_attachments_();
    const VkPipelineColorBlendStateCreateInfo colorBlendState =
        create_color_blend_state_();

    std::vector<VkDynamicState> dynamicMerged = dynamicStates_;
    if (dynamicViewport_) {
        dynamicMerged.reserve(dynamicMerged.size() + 2);
        append_unique_dynamic_state_(dynamicMerged, VK_DYNAMIC_STATE_VIEWPORT);
        append_unique_dynamic_state_(dynamicMerged, VK_DYNAMIC_STATE_SCISSOR);
    }
    VkPipelineDynamicStateCreateInfo dynamicStateInfo {};
    dynamicStateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(dynamicMerged.size());
    dynamicStateInfo.pDynamicStates =
        dynamicMerged.empty() ? nullptr : dynamicMerged.data();
    const VkPipelineDynamicStateCreateInfo *dynamicStatePtr =
        dynamicMerged.empty() ? nullptr : &dynamicStateInfo;

    VkGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.flags = pipelineCreateFlags_;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisampleState;
    pipelineInfo.pDepthStencilState = &depthStencilState;
    pipelineInfo.pColorBlendState = &colorBlendState;
    pipelineInfo.pDynamicState = dynamicStatePtr;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = subpass_;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipeline pipeline { VK_NULL_HANDLE };
    const VkResult vkResult = vkCreateGraphicsPipelines(
        device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (vkResult != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vkCreateGraphicsPipelines failed");
        return VK_NULL_HANDLE;
    }

    destroy_shader_modules_();
    return pipeline;
}

void GraphicsPipelineBuilder::append_unique_dynamic_state_(
    std::vector<VkDynamicState> &into, VkDynamicState state) {
    if (std::find(into.begin(), into.end(), state) == into.end()) {
        into.push_back(state);
    }
}

void GraphicsPipelineBuilder::add_dynamic_state_unique_(VkDynamicState state) {
    append_unique_dynamic_state_(dynamicStates_, state);
}

void GraphicsPipelineBuilder::destroy_shader_module_(VkShaderModule &module) {
    if (module == VK_NULL_HANDLE || device_ == VK_NULL_HANDLE) {
        return;
    }
    vkDestroyShaderModule(device_, module, nullptr);
    module = VK_NULL_HANDLE;
}

void GraphicsPipelineBuilder::destroy_shader_modules_() {
    destroy_shader_module_(vertexShaderModule_);
    destroy_shader_module_(fragmentShaderModule_);
}

[[nodiscard]] bool GraphicsPipelineBuilder::create_shader_module_(
    const std::vector<std::uint32_t> &spirv, VkShaderModule &outModule) {
    if (spirv.empty()) {
        return false;
    }
    VkShaderModuleCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
    createInfo.pCode = spirv.data();
    return vkCreateShaderModule(device_, &createInfo, nullptr, &outModule) ==
           VK_SUCCESS;
}

[[nodiscard]] std::vector<VkPipelineShaderStageCreateInfo>
GraphicsPipelineBuilder::create_shader_stages_() const {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(2);
    if (vertexShaderModule_ != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stageInfo {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stageInfo.module = vertexShaderModule_;
        stageInfo.pName = vertexEntry_.c_str();
        stages.push_back(stageInfo);
    }
    if (fragmentShaderModule_ != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo stageInfo {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stageInfo.module = fragmentShaderModule_;
        stageInfo.pName = fragmentEntry_.c_str();
        stages.push_back(stageInfo);
    }
    return stages;
}

[[nodiscard]] VkPipelineVertexInputStateCreateInfo
GraphicsPipelineBuilder::create_vertex_input_state_() const {
    VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount =
        static_cast<uint32_t>(vertexBindings_.size());
    vertexInputInfo.pVertexBindingDescriptions =
        vertexBindings_.empty() ? nullptr : vertexBindings_.data();
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertexAttributes_.size());
    vertexInputInfo.pVertexAttributeDescriptions =
        vertexAttributes_.empty() ? nullptr : vertexAttributes_.data();
    return vertexInputInfo;
}

[[nodiscard]] VkPipelineInputAssemblyStateCreateInfo
GraphicsPipelineBuilder::create_input_assembly_state_() const {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo {};
    inputAssemblyInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = primitiveTopology_;
    inputAssemblyInfo.primitiveRestartEnable = primitiveRestartEnable_;
    return inputAssemblyInfo;
}

[[nodiscard]] VkPipelineRasterizationStateCreateInfo
GraphicsPipelineBuilder::create_rasterization_state_() const {
    VkPipelineRasterizationStateCreateInfo rasterizationInfo {};
    rasterizationInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationInfo.depthClampEnable = depthClampEnable_;
    rasterizationInfo.rasterizerDiscardEnable = rasterizerDiscardEnable_;
    rasterizationInfo.polygonMode = polygonMode_;
    rasterizationInfo.cullMode = cullMode_;
    rasterizationInfo.frontFace = frontFace_;
    rasterizationInfo.depthBiasEnable = depthBiasEnable_;
    rasterizationInfo.depthBiasConstantFactor = depthBiasConstantFactor_;
    rasterizationInfo.depthBiasClamp = depthBiasClamp_;
    rasterizationInfo.depthBiasSlopeFactor = depthBiasSlopeFactor_;
    rasterizationInfo.lineWidth = lineWidth_;
    return rasterizationInfo;
}

[[nodiscard]] VkPipelineMultisampleStateCreateInfo
GraphicsPipelineBuilder::create_multisample_state_() const {
    VkPipelineMultisampleStateCreateInfo multisampleInfo {};
    multisampleInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.rasterizationSamples = rasterizationSamples_;
    multisampleInfo.sampleShadingEnable = sampleShadingEnable_;
    multisampleInfo.minSampleShading = minSampleShading_;
    multisampleInfo.pSampleMask =
        sampleMask_.empty() ? nullptr : sampleMask_.data();
    multisampleInfo.alphaToCoverageEnable = alphaToCoverageEnable_;
    multisampleInfo.alphaToOneEnable = alphaToOneEnable_;
    return multisampleInfo;
}

[[nodiscard]] VkPipelineViewportStateCreateInfo
GraphicsPipelineBuilder::create_viewport_state_() const {
    VkPipelineViewportStateCreateInfo viewportInfo {};
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    if (dynamicViewport_) {
        viewportInfo.viewportCount = dynamicViewportCount_;
        viewportInfo.pViewports = nullptr;
        viewportInfo.scissorCount = dynamicScissorCount_;
        viewportInfo.pScissors = nullptr;
    } else {
        viewportInfo.viewportCount = static_cast<uint32_t>(viewports_.size());
        viewportInfo.pViewports =
            viewports_.empty() ? nullptr : viewports_.data();
        viewportInfo.scissorCount = static_cast<uint32_t>(scissors_.size());
        viewportInfo.pScissors = scissors_.empty() ? nullptr : scissors_.data();
    }
    return viewportInfo;
}

[[nodiscard]] VkPipelineDepthStencilStateCreateInfo
GraphicsPipelineBuilder::create_depth_stencil_state_() const {
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo {};
    depthStencilInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilInfo.depthTestEnable = depthTestEnable_;
    depthStencilInfo.depthWriteEnable = depthWriteEnable_;
    depthStencilInfo.depthCompareOp = depthCompareOp_;
    depthStencilInfo.depthBoundsTestEnable = depthBoundsTestEnable_;
    depthStencilInfo.stencilTestEnable = stencilTestEnable_;
    depthStencilInfo.front = stencilFront_;
    depthStencilInfo.back = stencilBack_;
    depthStencilInfo.minDepthBounds = minDepthBounds_;
    depthStencilInfo.maxDepthBounds = maxDepthBounds_;
    return depthStencilInfo;
}

void GraphicsPipelineBuilder::ensure_color_blend_attachments_() {
    if (colorBlendAttachments_.empty()) {
        colorBlendAttachments_.push_back(make_blend_attachment_disabled());
    }
}

[[nodiscard]] VkPipelineColorBlendStateCreateInfo
GraphicsPipelineBuilder::create_color_blend_state_() const {
    VkPipelineColorBlendStateCreateInfo colorBlendInfo {};
    colorBlendInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendInfo.logicOpEnable = logicOpEnable_;
    colorBlendInfo.logicOp = logicOp_;
    colorBlendInfo.attachmentCount =
        static_cast<uint32_t>(colorBlendAttachments_.size());
    colorBlendInfo.pAttachments = colorBlendAttachments_.empty()
                                      ? nullptr
                                      : colorBlendAttachments_.data();
    for (std::size_t i { 0 }; i < blendConstants_.size(); ++i) {
        colorBlendInfo.blendConstants[i] = blendConstants_[i];
    }
    return colorBlendInfo;
}

} // namespace vulkan
