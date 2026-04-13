#include "vulkan/pipeline_builder.hpp"
#include "core/log/logger.hpp"

#include <vector>

namespace vulkan {

VkShaderModule createShaderModule(VkDevice device, const uint32_t *code,
                                  size_t size) {
    VkShaderModuleCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };

    VkShaderModule m;
    if (vkCreateShaderModule(device, &info, nullptr, &m) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return m;
}

VkPipeline PipelineBuilder::build() {
    // 输入装配
    VkPipelineInputAssemblyStateCreateInfo ia {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };

    // 视口 + 裁剪（动态）
    VkPipelineViewportStateCreateInfo vp {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    // 光栅化
    VkPipelineRasterizationStateCreateInfo rs {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    // 多重采样
    VkPipelineMultisampleStateCreateInfo ms {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    // 深度
    VkPipelineDepthStencilStateCreateInfo ds {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depthTest,
        .depthWriteEnable = depthWrite,
        .depthCompareOp = depthOp,
    };

    // 混合
    VkPipelineColorBlendAttachmentState att {};
    att.blendEnable = blendEnable;
    att.colorWriteMask = colorWriteMask;

    if (blendEnable) {
        att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.colorBlendOp = VK_BLEND_OP_ADD;
    }

    const uint32_t color_count =
        color_attachment_count == 0 ? 1U : color_attachment_count;
    std::vector<VkPipelineColorBlendAttachmentState> blend_atts(
        color_count, att);

    VkPipelineColorBlendStateCreateInfo cb {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = color_count,
        .pAttachments = blend_atts.data(),
    };

    // 动态状态（必须，用于resize）
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynStates,
    };

    // 着色器阶段
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    if (vertShader) {
        stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShader,
            .pName = "main",
        });
    }

    if (fragShader) {
        stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShader,
            .pName = "main",
        });
    }

    // 最终创建
    VkGraphicsPipelineCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = (uint32_t)stages.size(),
        .pStages = stages.data(),

        // ✅ 直接使用你反射生成的顶点输入
        .pVertexInputState = vertexInputState,

        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pDepthStencilState = &ds,
        .pColorBlendState = &cb,
        .pDynamicState = &dynState,
        .layout = layout,
        .renderPass = renderPass,
        .subpass = subpass,
    };

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                  &pipeline) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Failed to create pipeline");
        return VK_NULL_HANDLE;
    }

    return pipeline;
}
} // namespace vulkan
