#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "core/log/logger.hpp"

namespace vulkan {

/**
 * @brief 多边形光栅化模式（与 `VkPolygonMode` 一一对应：填充 / 线框 / 点）
 */
enum class PolygonDrawMode : std::uint32_t {
    Fill = VK_POLYGON_MODE_FILL,
    Line = VK_POLYGON_MODE_LINE,
    Point = VK_POLYGON_MODE_POINT,
};

/**
 * @brief 链式构建
 * `VkGraphicsPipeline`，成员与私有方法按固定功能管线阶段分组便于阅读。
 *
 * @note 成功 `build()` 后顶点/片段着色器模块会在内部销毁（管线已内联 SPIR-V）；
 *       `build()` 失败则保留模块，便于调用方排查或再次尝试。
 */
class GraphicsPipelineBuilder {
public:
    explicit GraphicsPipelineBuilder(VkDevice device) : device_(device) {}

    GraphicsPipelineBuilder(const GraphicsPipelineBuilder &) = delete;
    GraphicsPipelineBuilder &
    operator=(const GraphicsPipelineBuilder &) = delete;
    GraphicsPipelineBuilder(GraphicsPipelineBuilder &&) = delete;
    GraphicsPipelineBuilder &operator=(GraphicsPipelineBuilder &&) = delete;

    ~GraphicsPipelineBuilder() { destroy_shader_modules_(); }

    // -------------------------------------------------------------------------
    // 阶段 A：着色器模块（SPIR-V）
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_vertex_shader(const std::vector<std::uint32_t> &spirv) {
        if (vertexShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertexShaderModule_, nullptr);
            vertexShaderModule_ = VK_NULL_HANDLE;
        }
        if (!create_shader_module_(spirv, vertexShaderModule_)) {
            LUMEN_LOG_ERROR("vertex shader module create failed");
        }
        return *this;
    }

    GraphicsPipelineBuilder &
    set_fragment_shader(const std::vector<std::uint32_t> &spirv) {
        if (fragmentShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragmentShaderModule_, nullptr);
            fragmentShaderModule_ = VK_NULL_HANDLE;
        }
        if (!create_shader_module_(spirv, fragmentShaderModule_)) {
            LUMEN_LOG_ERROR("fragment shader module create failed");
        }
        return *this;
    }

    /** @brief 覆盖默认入口名 @c "main" */
    GraphicsPipelineBuilder &set_vertex_entry(std::string entryName = "main") {
        vertexEntry_ = std::move(entryName);
        return *this;
    }

    GraphicsPipelineBuilder &
    set_fragment_entry(std::string entryName = "main") {
        fragmentEntry_ = std::move(entryName);
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 B：顶点输入（binding / attribute）
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &set_vertex_layout(
        const std::vector<VkVertexInputAttributeDescription> &vertex_attributes,
        const std::vector<VkVertexInputBindingDescription> &vertex_bindings) {
        vertexAttributes_ = vertex_attributes;
        vertexBindings_ = vertex_bindings;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 C：图元装配、光栅、多重采样
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_topology(VkPrimitiveTopology primitiveTopology) {
        primitiveTopology_ = primitiveTopology;
        return *this;
    }

    /** @brief 对应
     * `VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable`，默认关
     */
    GraphicsPipelineBuilder &set_primitive_restart(bool enable = false) {
        primitiveRestartEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    GraphicsPipelineBuilder &set_polygon_mode(PolygonDrawMode polygonMode) {
        polygonMode_ = polygonMode;
        return *this;
    }

    GraphicsPipelineBuilder &set_cull_mode(VkCullModeFlags cullMode) {
        cullMode_ = cullMode;
        return *this;
    }

    GraphicsPipelineBuilder &set_front_face(VkFrontFace frontFace) {
        frontFace_ = frontFace;
        return *this;
    }

    GraphicsPipelineBuilder &set_line_width(float lineWidth) {
        lineWidth_ = lineWidth;
        return *this;
    }

    /** @brief 对应
     * `VkPipelineRasterizationStateCreateInfo::depthClampEnable`，默认关 */
    GraphicsPipelineBuilder &set_depth_clamp(bool enable = false) {
        depthClampEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `rasterizerDiscardEnable`，默认关 */
    GraphicsPipelineBuilder &set_rasterizer_discard(bool enable = false) {
        rasterizerDiscardEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `depthBiasEnable`，默认关 */
    GraphicsPipelineBuilder &set_depth_bias_enable(bool enable = false) {
        depthBiasEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `depthBiasConstantFactor`，默认 0 */
    GraphicsPipelineBuilder &set_depth_bias_constant_factor(float value = 0.F) {
        depthBiasConstantFactor_ = value;
        return *this;
    }

    /** @brief 对应 `depthBiasClamp`，默认 0 */
    GraphicsPipelineBuilder &set_depth_bias_clamp(float value = 0.F) {
        depthBiasClamp_ = value;
        return *this;
    }

    /** @brief 对应 `depthBiasSlopeFactor`，默认 0 */
    GraphicsPipelineBuilder &set_depth_bias_slope_factor(float value = 0.F) {
        depthBiasSlopeFactor_ = value;
        return *this;
    }

    GraphicsPipelineBuilder &
    set_sample_count(VkSampleCountFlagBits rasterizationSamples) {
        rasterizationSamples_ = rasterizationSamples;
        return *this;
    }

    GraphicsPipelineBuilder &set_sample_shading(bool enable,
                                                float minSampleShading = 1.F) {
        sampleShadingEnable_ = enable ? VK_TRUE : VK_FALSE;
        minSampleShading_ = minSampleShading;
        return *this;
    }

    /** @brief 仅调整 `minSampleShading`，默认 1（与 `set_sample_shading`
     * 中一致） */
    GraphicsPipelineBuilder &set_min_sample_shading(float value = 1.F) {
        minSampleShading_ = value;
        return *this;
    }

    /**
     * @brief 对应 `pSampleMask`；空 vector 表示 `nullptr`（规范中等价于全 1
     * 掩码）
     */
    GraphicsPipelineBuilder &
    set_sample_mask(const std::vector<VkSampleMask> &masks) {
        sampleMask_ = masks;
        return *this;
    }

    /** @brief 对应 `alphaToCoverageEnable`，默认关 */
    GraphicsPipelineBuilder &set_alpha_to_coverage(bool enable = false) {
        alphaToCoverageEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `alphaToOneEnable`，默认关 */
    GraphicsPipelineBuilder &set_alpha_to_one(bool enable = false) {
        alphaToOneEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 D：视口与裁剪（静态；若启用动态 viewport/scissor 可只设 count=1 与
    // nullptr）
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_viewport(const std::vector<VkViewport> &viewports,
                 const std::vector<VkRect2D> &scissors) {
        viewports_ = viewports;
        scissors_ = scissors;
        return *this;
    }

    /** @brief 使用动态 VK_DYNAMIC_STATE_VIEWPORT / SCISSOR 时调用 */
    GraphicsPipelineBuilder &set_viewport_dynamic(uint32_t viewportCount = 1,
                                                  uint32_t scissorCount = 1) {
        viewports_.clear();
        scissors_.clear();
        dynamicViewport_ = true;
        dynamicViewportCount_ = viewportCount;
        dynamicScissorCount_ = scissorCount;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 E：深度 / 模板
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_depth_test(bool depthTest, bool depthWrite,
                   VkCompareOp depthCompare = VK_COMPARE_OP_LESS) {
        depthTestEnable_ = depthTest ? VK_TRUE : VK_FALSE;
        depthWriteEnable_ = depthWrite ? VK_TRUE : VK_FALSE;
        depthCompareOp_ = depthCompare;
        return *this;
    }

    GraphicsPipelineBuilder &set_depth_bounds_test(bool enable) {
        depthBoundsTestEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    GraphicsPipelineBuilder &set_stencil_test(bool enable) {
        stencilTestEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `front`（`VkStencilOpState`），默认零初始化 */
    GraphicsPipelineBuilder &
    set_stencil_front(const VkStencilOpState &state = {}) {
        stencilFront_ = state;
        return *this;
    }

    /** @brief 对应 `back`，默认零初始化 */
    GraphicsPipelineBuilder &
    set_stencil_back(const VkStencilOpState &state = {}) {
        stencilBack_ = state;
        return *this;
    }

    /** @brief `front` / `back` 设为同一套模板操作 */
    GraphicsPipelineBuilder &set_stencil_ops(const VkStencilOpState &state) {
        stencilFront_ = state;
        stencilBack_ = state;
        return *this;
    }

    /** @brief 对应 `minDepthBounds` / `maxDepthBounds`，默认 [0, 1] */
    GraphicsPipelineBuilder &set_depth_bounds(float min_bounds = 0.F,
                                              float max_bounds = 1.F) {
        minDepthBounds_ = min_bounds;
        maxDepthBounds_ = max_bounds;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 F：颜色混合
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &set_blend_off() {
        colorBlendAttachments_.clear();
        colorBlendAttachments_.push_back(make_blend_attachment_disabled_());
        return *this;
    }

    /** @brief 单 color attachment 的常用 alpha 混合（预乘或非预乘由 attachment
     * 决定） */
    GraphicsPipelineBuilder &set_blend_alpha() {
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

    GraphicsPipelineBuilder &set_color_blend_attachments(
        std::vector<VkPipelineColorBlendAttachmentState> attachments) {
        colorBlendAttachments_ = std::move(attachments);
        return *this;
    }

    GraphicsPipelineBuilder &set_blend_constants(float r, float g, float b,
                                                 float a) {
        blendConstants_[0] = r;
        blendConstants_[1] = g;
        blendConstants_[2] = b;
        blendConstants_[3] = a;
        return *this;
    }

    // 与 set_blend_constants(r,g,b,a) 等价，避免相邻 float 参数误传
    GraphicsPipelineBuilder &set_blend_constants(std::array<float, 4> rgba) {
        blendConstants_ = rgba;
        return *this;
    }

    /** @brief 对应 `logicOpEnable`；为真时使用帧缓冲逻辑运算，默认关 */
    GraphicsPipelineBuilder &set_logic_op_enable(bool enable = false) {
        logicOpEnable_ = enable ? VK_TRUE : VK_FALSE;
        return *this;
    }

    /** @brief 对应 `logicOp`；仅在 `logicOpEnable` 为真时生效，默认 `COPY` */
    GraphicsPipelineBuilder &set_logic_op(VkLogicOp op = VK_LOGIC_OP_COPY) {
        logicOp_ = op;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 G：动态状态
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_dynamic_state(std::vector<VkDynamicState> states) {
        dynamicStates_ = std::move(states);
        return *this;
    }

    GraphicsPipelineBuilder &add_dynamic_state(VkDynamicState state) {
        dynamicStates_.push_back(state);
        return *this;
    }

    /** @brief 动态线宽（`vkCmdSetLineWidth`） */
    GraphicsPipelineBuilder &add_dynamic_line_width() {
        add_dynamic_state_unique_(VK_DYNAMIC_STATE_LINE_WIDTH);
        return *this;
    }

    /** @brief 动态面剔除（`vkCmdSetCullMode`），需 extended dynamic state */
    GraphicsPipelineBuilder &add_dynamic_cull_mode() {
        add_dynamic_state_unique_(VK_DYNAMIC_STATE_CULL_MODE);
        return *this;
    }

    /** @brief 动态正面顺序（`vkCmdSetFrontFace`） */
    GraphicsPipelineBuilder &add_dynamic_front_face() {
        add_dynamic_state_unique_(VK_DYNAMIC_STATE_FRONT_FACE);
        return *this;
    }

    /** @brief 动态图元拓扑（`vkCmdSetPrimitiveTopology`） */
    GraphicsPipelineBuilder &add_dynamic_primitive_topology() {
        add_dynamic_state_unique_(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
        return *this;
    }

    /**
     * @brief 动态多边形模式（实心 / 线框 / 点，`vkCmdSetPolygonModeEXT`）
     * @note 需 `VK_EXT_extended_dynamic_state3`（或等价特性）
     */
    GraphicsPipelineBuilder &add_dynamic_polygon_mode() {
        add_dynamic_state_unique_(VK_DYNAMIC_STATE_POLYGON_MODE_EXT);
        return *this;
    }

    /**
     * @brief 依次加入上述五项常用光栅 / 装配动态状态（与已有列表去重合并）
     */
    GraphicsPipelineBuilder &add_dynamic_raster_common() {
        add_dynamic_line_width();
        add_dynamic_cull_mode();
        add_dynamic_front_face();
        add_dynamic_primitive_topology();
        add_dynamic_polygon_mode();
        return *this;
    }

    // -------------------------------------------------------------------------
    // 阶段 H：布局与 RenderPass
    // -------------------------------------------------------------------------

    GraphicsPipelineBuilder &
    set_pipeline_layout(VkPipelineLayout pipelineLayout) {
        pipelineLayout_ = pipelineLayout;
        return *this;
    }

    GraphicsPipelineBuilder &set_render_pass(VkRenderPass renderPass,
                                             uint32_t subpassIndex = 0) {
        renderPass_ = renderPass;
        subpass_ = subpassIndex;
        return *this;
    }

    GraphicsPipelineBuilder &
    set_pipeline_create_flags(VkPipelineCreateFlags pipelineCreateFlags) {
        pipelineCreateFlags_ = pipelineCreateFlags;
        return *this;
    }

    // -------------------------------------------------------------------------
    // 构建
    // -------------------------------------------------------------------------

    [[nodiscard]] VkPipeline build() {
        if (vertexShaderModule_ == VK_NULL_HANDLE) {
            LUMEN_LOG_ERROR("GraphicsPipelineBuilder: vertex shader not set");
            return VK_NULL_HANDLE;
        }
        if (pipelineLayout_ == VK_NULL_HANDLE ||
            renderPass_ == VK_NULL_HANDLE) {
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

        // ---- 1. Shader stages ----
        const std::vector<VkPipelineShaderStageCreateInfo> shaderStages =
            create_shader_stages_();
        if (shaderStages.empty()) {
            LUMEN_LOG_ERROR("GraphicsPipelineBuilder: no shader stages");
            return VK_NULL_HANDLE;
        }

        // ---- 2. Vertex input ----
        const VkPipelineVertexInputStateCreateInfo vertexInput =
            create_vertex_input_state_();

        // ---- 3. Input assembly ----
        const VkPipelineInputAssemblyStateCreateInfo inputAssembly =
            create_input_assembly_state_();

        // ---- 4. Viewport / scissor ----
        const VkPipelineViewportStateCreateInfo viewportState =
            create_viewport_state_();

        // ---- 5. Rasterization ----
        const VkPipelineRasterizationStateCreateInfo rasterization =
            create_rasterization_state_();

        // ---- 6. Multisample ----
        const VkPipelineMultisampleStateCreateInfo multisampleState =
            create_multisample_state_();

        // ---- 7. Depth / stencil ----
        const VkPipelineDepthStencilStateCreateInfo depthStencilState =
            create_depth_stencil_state_();

        // ---- 8. Color blend ----
        ensure_color_blend_attachments_();
        const VkPipelineColorBlendStateCreateInfo colorBlendState =
            create_color_blend_state_();

        // ---- 9. Dynamic（动态视口时自动补全 VIEWPORT / SCISSOR） ----
        std::vector<VkDynamicState> dynamicMerged = dynamicStates_;
        if (dynamicViewport_) {
            auto appendIfMissing =
                [&dynamicMerged](VkDynamicState dynamicState) {
                    if (std::find(dynamicMerged.begin(), dynamicMerged.end(),
                                  dynamicState) == dynamicMerged.end()) {
                        dynamicMerged.push_back(dynamicState);
                    }
                };
            appendIfMissing(VK_DYNAMIC_STATE_VIEWPORT);
            appendIfMissing(VK_DYNAMIC_STATE_SCISSOR);
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
        pipelineInfo.pDynamicState = dynamicStatePtr; // nullptr 表示无动态状态
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

private:
    // =========================================================================
    // 资源：按阶段分组（私有成员：camelCase_）
    // =========================================================================

    VkDevice device_ { VK_NULL_HANDLE };

    // --- 阶段 A：着色器 ---
    VkShaderModule vertexShaderModule_ { VK_NULL_HANDLE };
    VkShaderModule fragmentShaderModule_ { VK_NULL_HANDLE };
    std::string vertexEntry_ { "main" };
    std::string fragmentEntry_ { "main" };

    // --- 阶段 B：顶点输入 ---
    std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
    std::vector<VkVertexInputBindingDescription> vertexBindings_;

    // --- 阶段 C：装配 / 光栅 / 采样 ---
    VkPrimitiveTopology primitiveTopology_ {
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkBool32 primitiveRestartEnable_ { VK_FALSE };
    PolygonDrawMode polygonMode_ { PolygonDrawMode::Fill };
    VkCullModeFlags cullMode_ { VK_CULL_MODE_NONE };
    VkFrontFace frontFace_ { VK_FRONT_FACE_COUNTER_CLOCKWISE };
    VkBool32 depthClampEnable_ { VK_FALSE };
    VkBool32 rasterizerDiscardEnable_ { VK_FALSE };
    VkBool32 depthBiasEnable_ { VK_FALSE };
    float depthBiasConstantFactor_ { 0.F };
    float depthBiasClamp_ { 0.F };
    float depthBiasSlopeFactor_ { 0.F };
    float lineWidth_ { 1.F };
    VkSampleCountFlagBits rasterizationSamples_ { VK_SAMPLE_COUNT_1_BIT };
    VkBool32 sampleShadingEnable_ { VK_FALSE };
    float minSampleShading_ { 1.F };
    std::vector<VkSampleMask> sampleMask_;
    VkBool32 alphaToCoverageEnable_ { VK_FALSE };
    VkBool32 alphaToOneEnable_ { VK_FALSE };

    // --- 阶段 D：视口 ---
    std::vector<VkViewport> viewports_;
    std::vector<VkRect2D> scissors_;
    bool dynamicViewport_ { false };
    uint32_t dynamicViewportCount_ { 1 };
    uint32_t dynamicScissorCount_ { 1 };

    // --- 阶段 E：深度 / 模板 ---
    VkBool32 depthTestEnable_ { VK_TRUE };
    VkBool32 depthWriteEnable_ { VK_TRUE };
    VkCompareOp depthCompareOp_ { VK_COMPARE_OP_LESS };
    VkBool32 depthBoundsTestEnable_ { VK_FALSE };
    VkBool32 stencilTestEnable_ { VK_FALSE };
    VkStencilOpState stencilFront_ {};
    VkStencilOpState stencilBack_ {};
    float minDepthBounds_ { 0.F };
    float maxDepthBounds_ { 1.F };

    // --- 阶段 F：混合 ---
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;
    std::array<float, 4> blendConstants_ {};
    VkBool32 logicOpEnable_ { VK_FALSE };
    VkLogicOp logicOp_ { VK_LOGIC_OP_COPY };

    // --- 阶段 G：动态状态 ---
    std::vector<VkDynamicState> dynamicStates_;

    // --- 阶段 H：布局 / Pass ---
    VkPipelineLayout pipelineLayout_ { VK_NULL_HANDLE };
    VkRenderPass renderPass_ { VK_NULL_HANDLE };
    uint32_t subpass_ { 0 };
    VkPipelineCreateFlags pipelineCreateFlags_ { 0 };

    // =========================================================================
    // 私有方法：snake_case + 尾部 _（与引擎私有辅助一致）
    // =========================================================================

    void add_dynamic_state_unique_(VkDynamicState state) {
        if (std::find(dynamicStates_.begin(), dynamicStates_.end(), state) ==
            dynamicStates_.end()) {
            dynamicStates_.push_back(state);
        }
    }

    // --- 阶段 A ---

    void destroy_shader_modules_() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        if (vertexShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vertexShaderModule_, nullptr);
            vertexShaderModule_ = VK_NULL_HANDLE;
        }
        if (fragmentShaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, fragmentShaderModule_, nullptr);
            fragmentShaderModule_ = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] bool
    create_shader_module_(const std::vector<std::uint32_t> &spirv,
                          VkShaderModule &outModule) {
        if (spirv.empty()) {
            return false;
        }
        VkShaderModuleCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(std::uint32_t);
        createInfo.pCode = spirv.data();
        return vkCreateShaderModule(device_, &createInfo, nullptr,
                                    &outModule) == VK_SUCCESS;
    }

    [[nodiscard]] std::vector<VkPipelineShaderStageCreateInfo>
    create_shader_stages_() const {
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(2);
        if (vertexShaderModule_ != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo stageInfo {};
            stageInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            stageInfo.module = vertexShaderModule_;
            stageInfo.pName = vertexEntry_.c_str();
            stages.push_back(stageInfo);
        }
        if (fragmentShaderModule_ != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo stageInfo {};
            stageInfo.sType =
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stageInfo.module = fragmentShaderModule_;
            stageInfo.pName = fragmentEntry_.c_str();
            stages.push_back(stageInfo);
        }
        return stages;
    }

    // --- 阶段 B ---

    [[nodiscard]] VkPipelineVertexInputStateCreateInfo
    create_vertex_input_state_() const {
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

    // --- 阶段 C ---

    [[nodiscard]] VkPipelineInputAssemblyStateCreateInfo
    create_input_assembly_state_() const {
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo {};
        inputAssemblyInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = primitiveTopology_;
        inputAssemblyInfo.primitiveRestartEnable = primitiveRestartEnable_;
        return inputAssemblyInfo;
    }

    [[nodiscard]] VkPipelineRasterizationStateCreateInfo
    create_rasterization_state_() const {
        VkPipelineRasterizationStateCreateInfo rasterizationInfo {};
        rasterizationInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizationInfo.depthClampEnable = depthClampEnable_;
        rasterizationInfo.rasterizerDiscardEnable = rasterizerDiscardEnable_;
        rasterizationInfo.polygonMode =
            static_cast<VkPolygonMode>(polygonMode_);
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
    create_multisample_state_() const {
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

    // --- 阶段 D ---

    [[nodiscard]] VkPipelineViewportStateCreateInfo
    create_viewport_state_() const {
        VkPipelineViewportStateCreateInfo viewportInfo {};
        viewportInfo.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        if (dynamicViewport_) {
            viewportInfo.viewportCount = dynamicViewportCount_;
            viewportInfo.pViewports = nullptr;
            viewportInfo.scissorCount = dynamicScissorCount_;
            viewportInfo.pScissors = nullptr;
        } else {
            viewportInfo.viewportCount =
                static_cast<uint32_t>(viewports_.size());
            viewportInfo.pViewports =
                viewports_.empty() ? nullptr : viewports_.data();
            viewportInfo.scissorCount = static_cast<uint32_t>(scissors_.size());
            viewportInfo.pScissors =
                scissors_.empty() ? nullptr : scissors_.data();
        }
        return viewportInfo;
    }

    // --- 阶段 E ---

    [[nodiscard]] VkPipelineDepthStencilStateCreateInfo
    create_depth_stencil_state_() const {
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

    // --- 阶段 F ---

    [[nodiscard]] static VkPipelineColorBlendAttachmentState
    make_blend_attachment_disabled_() {
        VkPipelineColorBlendAttachmentState attachment {};
        attachment.blendEnable = VK_FALSE;
        attachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        return attachment;
    }

    void ensure_color_blend_attachments_() {
        if (colorBlendAttachments_.empty()) {
            colorBlendAttachments_.push_back(make_blend_attachment_disabled_());
        }
    }

    [[nodiscard]] VkPipelineColorBlendStateCreateInfo
    create_color_blend_state_() const {
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
        for (size_t i { 0 }; i < 4; ++i) {
            colorBlendInfo.blendConstants[i] = blendConstants_[i];
        }
        return colorBlendInfo;
    }
};

} // namespace vulkan
