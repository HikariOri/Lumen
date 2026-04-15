#pragma once

namespace vulkan {

VkShaderModule createShaderModule(VkDevice device, const uint32_t *code,
                                  size_t size);

struct PipelineBuilder {
    VkDevice device;

    // 直接用反射生成的完整结构体 ✅
    const VkPipelineVertexInputStateCreateInfo *vertexInputState = nullptr;

    // 着色器
    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;

    // 拓扑
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 混合
    bool blendEnable = false;
    VkColorComponentFlags colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // 深度
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthOp = VK_COMPARE_OP_LESS;

    // 渲染目标
    VkRenderPass renderPass;
    VkPipelineLayout layout;
    uint32_t subpass = 0;
    /// 与 VkRenderPass 中对应 Subpass 的 color attachment 数量一致（MRT 时 >1）。
    uint32_t colorAttachmentCount = 1;

    // 构建（最终版）
    VkPipeline build();
};

} // namespace vulkan
