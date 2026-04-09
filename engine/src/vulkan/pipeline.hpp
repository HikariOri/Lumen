/**
 * @file pipeline.hpp
 * @brief Vulkan 图形管线链式构建器 `GraphicsPipelineBuilder` 的声明。
 *
 * @details
 * 将固定功能阶段与动态状态分步配置后，一次调用 build() 即可完成
 * `vkCreateGraphicsPipelines`。
 * 顶点/片段着色器模块由调用方提供并在 `build()` 完成前保持有效；本类不创建、不销毁
 * `VkShaderModule`（可与 `vulkan::Shader` 等 RAII 配合使用）。
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @class GraphicsPipelineBuilder
 * @brief 以链式 API 组装 `VkGraphicsPipelineCreateInfo` 并创建管线。
 *
 * @note 需要有效的 @c VkDevice 的生命周期不短于本对象。
 * @note 须在调用 build() 前设置顶点着色器、@c VkPipelineLayout 与 @c
 *       VkRenderPass；非动态视口模式下还需 set_viewport()。
 */
class GraphicsPipelineBuilder {
public:
    /**
     * @param device 用于创建图形管线的逻辑设备。
     */
    explicit GraphicsPipelineBuilder(VkDevice device);

    GraphicsPipelineBuilder(const GraphicsPipelineBuilder &) = delete;
    GraphicsPipelineBuilder &
    operator=(const GraphicsPipelineBuilder &) = delete;
    GraphicsPipelineBuilder(GraphicsPipelineBuilder &&) = delete;
    GraphicsPipelineBuilder &operator=(GraphicsPipelineBuilder &&) = delete;

    ~GraphicsPipelineBuilder();

    /** @{ @name 着色器模块 */

    /**
     * @brief 设置顶点着色器模块与入口名（替换已有配置）。
     * @param module 有效 `VkShaderModule`；须在 `build()` 返回前保持有效。
     * @param entry_name 入口点名，默认 @c "main"（内部以 `std::string` 保存至 `build()`）。
     */
    GraphicsPipelineBuilder &set_vertex_shader(VkShaderModule module,
                                               std::string entry_name = "main");

    /**
     * @brief 设置片段着色器模块与入口名（替换已有配置）。
     * @param module 有效 `VkShaderModule`；须在 `build()` 返回前保持有效。
     */
    GraphicsPipelineBuilder &set_fragment_shader(VkShaderModule module,
                                                 std::string entry_name = "main");

    /** @} */

    /** @{ @name 顶点输入 */

    /**
     * @brief 设置顶点属性与绑定描述（对应
     * `VkPipelineVertexInputStateCreateInfo`）。
     * @param vertex_attributes `pVertexAttributeDescriptions`
     * @param vertex_bindings `pVertexBindingDescriptions`
     */
    GraphicsPipelineBuilder &set_vertex_layout(
        const std::vector<VkVertexInputAttributeDescription> &vertex_attributes,
        const std::vector<VkVertexInputBindingDescription> &vertex_bindings);

    /** @} */

    /** @{ @name 图元装配、光栅化、多重采样 */

    /** @brief 图元拓扑，默认三角列表。 */
    GraphicsPipelineBuilder &
    set_topology(VkPrimitiveTopology primitiveTopology);

    /**
     * @brief `VkPipelineInputAssemblyStateCreateInfo::primitiveRestartEnable`。
     * @param enable 是否启用 primitive restart 索引。
     */
    GraphicsPipelineBuilder &set_primitive_restart(bool enable = false);

    /** @brief 多边形光栅化模式（填充 / 线框 / 点）。 */
    GraphicsPipelineBuilder &set_polygon_mode(VkPolygonMode polygonMode);

    /** @brief 面剔除掩码（`VkCullModeFlags`）。 */
    GraphicsPipelineBuilder &set_cull_mode(VkCullModeFlags cullMode);

    /** @brief 正面顶点绕序。 */
    GraphicsPipelineBuilder &set_front_face(VkFrontFace frontFace);

    /** @brief `VkPipelineRasterizationStateCreateInfo::lineWidth`。 */
    GraphicsPipelineBuilder &set_line_width(float lineWidth);

    /**
     * @brief `depthClampEnable`，默认关闭。
     * @param enable 需要设备支持 depth clamp 等特性方可实际生效。
     */
    GraphicsPipelineBuilder &set_depth_clamp(bool enable = false);

    /** @brief `rasterizerDiscardEnable`，默认关闭。 */
    GraphicsPipelineBuilder &set_rasterizer_discard(bool enable = false);

    /** @brief `depthBiasEnable`，默认关闭。 */
    GraphicsPipelineBuilder &set_depth_bias_enable(bool enable = false);

    /** @brief `depthBiasConstantFactor`，默认 0 。 */
    GraphicsPipelineBuilder &set_depth_bias_constant_factor(float value = 0.F);

    /** @brief `depthBiasClamp`，默认 0 。 */
    GraphicsPipelineBuilder &set_depth_bias_clamp(float value = 0.F);

    /** @brief `depthBiasSlopeFactor`，默认 0 。 */
    GraphicsPipelineBuilder &set_depth_bias_slope_factor(float value = 0.F);

    /** @brief 光栅化样本数（`rasterizationSamples`）。 */
    GraphicsPipelineBuilder &
    set_sample_count(VkSampleCountFlagBits rasterizationSamples);

    /**
     * @brief 逐样本着色（`sampleShadingEnable` / `minSampleShading`）。
     * @param enable 是否启用 sample shading（需设备支持）。
     * @param minSampleShading 最小着色覆盖率，默认 1.0 。
     */
    GraphicsPipelineBuilder &set_sample_shading(bool enable,
                                                float minSampleShading = 1.F);

    /** @brief 仅更新 `minSampleShading`。 */
    GraphicsPipelineBuilder &set_min_sample_shading(float value = 1.F);

    /**
     * @brief `pSampleMask`；空容器表示 @c nullptr （规范中等价全 1 掩码）。
     * @param masks 每个元素的位数与 `rasterizationSamples` 一致。
     */
    GraphicsPipelineBuilder &
    set_sample_mask(const std::vector<VkSampleMask> &masks);

    /** @brief `alphaToCoverageEnable`。 */
    GraphicsPipelineBuilder &set_alpha_to_coverage(bool enable = false);

    /** @brief `alphaToOneEnable`。 */
    GraphicsPipelineBuilder &set_alpha_to_one(bool enable = false);

    /** @} */

    /** @{ @name 视口与裁剪 */

    /**
     * @brief 静态视口与裁剪矩形（数量必须一致且非空，除非使用动态视口）。
     * @param viewports `pViewports`
     * @param scissors `pScissors`
     */
    GraphicsPipelineBuilder &
    set_viewport(const std::vector<VkViewport> &viewports,
                 const std::vector<VkRect2D> &scissors);

    /**
     * @brief 使用动态 `VK_DYNAMIC_STATE_VIEWPORT` / `SCISSOR`。
     * @param viewportCount 动态视口个数。
     * @param scissorCount 动态裁剪个数。
     */
    GraphicsPipelineBuilder &set_viewport_dynamic(uint32_t viewportCount = 1,
                                                  uint32_t scissorCount = 1);

    /** @} */

    /** @{ @name 深度 / 模板 */

    /**
     * @brief 深度测试、写入与比较函数。
     * @param depthTest `depthTestEnable`
     * @param depthWrite `depthWriteEnable`
     * @param depthCompare `depthCompareOp`，默认 `VK_COMPARE_OP_LESS` 。
     */
    GraphicsPipelineBuilder &
    set_depth_test(bool depthTest, bool depthWrite,
                   VkCompareOp depthCompare = VK_COMPARE_OP_LESS);

    /** @brief `depthBoundsTestEnable`。 */
    GraphicsPipelineBuilder &set_depth_bounds_test(bool enable);

    /** @brief `stencilTestEnable`。 */
    GraphicsPipelineBuilder &set_stencil_test(bool enable);

    /** @brief 模板前向面状态（`VkStencilOpState::front`）。 */
    GraphicsPipelineBuilder &
    set_stencil_front(const VkStencilOpState &state = {});

    /** @brief 模板背向面状态（`back`）。 */
    GraphicsPipelineBuilder &
    set_stencil_back(const VkStencilOpState &state = {});

    /** @brief 将 front / back 设为同一套模板操作。 */
    GraphicsPipelineBuilder &set_stencil_ops(const VkStencilOpState &state);

    /**
     * @brief `minDepthBounds` / `maxDepthBounds`，默认 [0, 1] 。
     * @param min_bounds 深度下界。
     * @param max_bounds 深度上界。
     */
    GraphicsPipelineBuilder &set_depth_bounds(float min_bounds = 0.F,
                                              float max_bounds = 1.F);

    /** @} */

    /** @{ @name 颜色混合 */

    /** @brief 单 attachment：关闭混合，全通道写入。 */
    GraphicsPipelineBuilder &set_blend_off();

    /**
     * @brief 单 attachment：常用 SrcAlpha / InvSrcAlpha 颜色混合。
     * @note 是否与预乘 Alpha 匹配取决于 attachment 数据布局。
     */
    GraphicsPipelineBuilder &set_blend_alpha();

    /** @brief 直接替换整表 color blend attachment 状态。 */
    GraphicsPipelineBuilder &set_color_blend_attachments(
        std::vector<VkPipelineColorBlendAttachmentState> attachments);

    /** @brief `blendConstants`（四分量）。 */
    GraphicsPipelineBuilder &set_blend_constants(float r, float g, float b,
                                                 float a);

    /** @brief 与四 float 版等价，避免相邻标量参数误传。 */
    GraphicsPipelineBuilder &set_blend_constants(std::array<float, 4> rgba);

    /** @brief `logicOpEnable`；为真时对帧缓冲做逻辑运算。 */
    GraphicsPipelineBuilder &set_logic_op_enable(bool enable = false);

    /** @brief `logicOp`；仅在 logic 开启时有效。 */
    GraphicsPipelineBuilder &set_logic_op(VkLogicOp op = VK_LOGIC_OP_COPY);

    /** @} */

    /** @{ @name 动态状态 */

    /** @brief 替换整表动态状态枚举列表。 */
    GraphicsPipelineBuilder &
    set_dynamic_state(std::vector<VkDynamicState> states);

    /** @brief 追加一项动态状态（不会去重）。 */
    GraphicsPipelineBuilder &add_dynamic_state(VkDynamicState state);

    /** @brief 动态线宽（`vkCmdSetLineWidth`）。 */
    GraphicsPipelineBuilder &add_dynamic_line_width();

    /**
     * @brief 动态剔除模式（`vkCmdSetCullMode`）。
     * @note 需 Vulkan 1.3+ 或 `VK_EXT_extended_dynamic_state` 等。
     */
    GraphicsPipelineBuilder &add_dynamic_cull_mode();

    /** @brief 动态正面绕序（`vkCmdSetFrontFace`）。 */
    GraphicsPipelineBuilder &add_dynamic_front_face();

    /** @brief 动态图元拓扑（`vkCmdSetPrimitiveTopology`）。 */
    GraphicsPipelineBuilder &add_dynamic_primitive_topology();

    /**
     * @brief 动态多边形模式（`VK_DYNAMIC_STATE_POLYGON_MODE_EXT`）。
     * @note 需 `VK_EXT_extended_dynamic_state3`（或等价）。
     */
    GraphicsPipelineBuilder &add_dynamic_polygon_mode();

    /** @brief 依次加入上述五项常用动态状态（与已有列表去重合并）。 */
    GraphicsPipelineBuilder &add_dynamic_raster_common();

    /** @} */

    /** @{ @name 布局与渲染Pass */

    /** @brief `layout`（`VkPipelineLayout`）。 */
    GraphicsPipelineBuilder &
    set_pipeline_layout(VkPipelineLayout pipelineLayout);

    /**
     * @brief `renderPass` 与子通道索引。
     * @param renderPass 兼容的 render pass 句柄。
     * @param subpassIndex `subpass` 下标。
     */
    GraphicsPipelineBuilder &set_render_pass(VkRenderPass renderPass,
                                             uint32_t subpassIndex = 0);

    /** @brief `VkGraphicsPipelineCreateInfo::flags`。 */
    GraphicsPipelineBuilder &
    set_pipeline_create_flags(VkPipelineCreateFlags pipelineCreateFlags);

    /** @} */

    /**
     * @brief 校验配置并调用 `vkCreateGraphicsPipelines`。
     * @return 成功返回管线句柄；失败返回 `VK_NULL_HANDLE` 并写引擎日志。
     * @note 不销毁传入的着色器模块；管线创建成功后调用方可按需销毁模块。
     */
    [[nodiscard]] VkPipeline build();

private:
    VkDevice device_ { VK_NULL_HANDLE };

    VkShaderModule vertexShaderModule_ { VK_NULL_HANDLE };
    VkShaderModule fragmentShaderModule_ { VK_NULL_HANDLE };
    std::string vertexEntry_ { "main" };
    std::string fragmentEntry_ { "main" };

    std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
    std::vector<VkVertexInputBindingDescription> vertexBindings_;

    VkPrimitiveTopology primitiveTopology_ {
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkBool32 primitiveRestartEnable_ { VK_FALSE };
    VkPolygonMode polygonMode_ { VK_POLYGON_MODE_FILL };
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

    std::vector<VkViewport> viewports_;
    std::vector<VkRect2D> scissors_;
    bool dynamicViewport_ { false };
    uint32_t dynamicViewportCount_ { 1 };
    uint32_t dynamicScissorCount_ { 1 };

    VkBool32 depthTestEnable_ { VK_TRUE };
    VkBool32 depthWriteEnable_ { VK_TRUE };
    VkCompareOp depthCompareOp_ { VK_COMPARE_OP_LESS };
    VkBool32 depthBoundsTestEnable_ { VK_FALSE };
    VkBool32 stencilTestEnable_ { VK_FALSE };
    VkStencilOpState stencilFront_ {};
    VkStencilOpState stencilBack_ {};
    float minDepthBounds_ { 0.F };
    float maxDepthBounds_ { 1.F };

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments_;
    std::array<float, 4> blendConstants_ {};
    VkBool32 logicOpEnable_ { VK_FALSE };
    VkLogicOp logicOp_ { VK_LOGIC_OP_COPY };

    std::vector<VkDynamicState> dynamicStates_;

    VkPipelineLayout pipelineLayout_ { VK_NULL_HANDLE };
    VkRenderPass renderPass_ { VK_NULL_HANDLE };
    uint32_t subpass_ { 0 };
    VkPipelineCreateFlags pipelineCreateFlags_ { 0 };

    void append_unique_dynamic_state_(std::vector<VkDynamicState> &into,
                                      VkDynamicState state);

    void add_dynamic_state_unique_(VkDynamicState state);

    [[nodiscard]] std::vector<VkPipelineShaderStageCreateInfo>
    create_shader_stages_() const;

    [[nodiscard]] VkPipelineVertexInputStateCreateInfo
    create_vertex_input_state_() const;

    [[nodiscard]] VkPipelineInputAssemblyStateCreateInfo
    create_input_assembly_state_() const;

    [[nodiscard]] VkPipelineRasterizationStateCreateInfo
    create_rasterization_state_() const;

    [[nodiscard]] VkPipelineMultisampleStateCreateInfo
    create_multisample_state_() const;

    [[nodiscard]] VkPipelineViewportStateCreateInfo
    create_viewport_state_() const;

    [[nodiscard]] VkPipelineDepthStencilStateCreateInfo
    create_depth_stencil_state_() const;

    void ensure_color_blend_attachments_();

    [[nodiscard]] VkPipelineColorBlendStateCreateInfo
    create_color_blend_state_() const;
};

} // namespace vulkan
