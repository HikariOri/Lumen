#pragma once

#include <vector>

#include <vulkan/vulkan_core.h>

#include "VKBase.h"

namespace vulkan {

    class graphicsBasePlus {
        VkFormatProperties formatProperties[std::size(formatInfos_v1_0)] = {};

        commandPool commandPool_graphics;
        commandPool commandPool_presentation;
        commandPool commandPool_compute;

        commandBuffer commandBuffer_transfer; // 从 commandPool_graphics 分配
        commandBuffer commandBuffer_presentation;

        // 静态变量
        static graphicsBasePlus singleton;

        //--------------------
        graphicsBasePlus() {
            // 在创建逻辑设备时执行Initialize()
            auto Initialize = [] {
                if (graphicsBase::Base().QueueFamilyIndex_Graphics() !=
                    VK_QUEUE_FAMILY_IGNORED) {
                    singleton.commandPool_graphics.Create(
                        graphicsBase::Base().QueueFamilyIndex_Graphics(),
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
                    singleton.commandPool_graphics.AllocateBuffers(
                        singleton.commandBuffer_transfer);
                }

                if (graphicsBase::Base().QueueFamilyIndex_Compute() !=
                    VK_QUEUE_FAMILY_IGNORED) {
                    singleton.commandPool_compute.Create(
                        graphicsBase::Base().QueueFamilyIndex_Compute(),
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
                }

                if (graphicsBase::Base().QueueFamilyIndex_Presentation() !=
                        VK_QUEUE_FAMILY_IGNORED &&
                    graphicsBase::Base().QueueFamilyIndex_Presentation() !=
                        graphicsBase::Base().QueueFamilyIndex_Graphics() &&
                    graphicsBase::Base()
                            .SwapchainCreateInfo()
                            .imageSharingMode == VK_SHARING_MODE_EXCLUSIVE) {
                    singleton.commandPool_presentation.Create(
                        graphicsBase::Base().QueueFamilyIndex_Presentation(),
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
                    singleton.commandPool_presentation.AllocateBuffers(
                        singleton.commandBuffer_presentation);
                }

                for (size_t i = 0; i < std::size(singleton.formatProperties);
                     i++) {
                    vkGetPhysicalDeviceFormatProperties(
                        graphicsBase::Base().PhysicalDevice(), VkFormat(i),
                        &singleton.formatProperties[i]);
                }
            };

            // 在销毁逻辑设备时执行CleanUp()
            // 如果你不需要更换物理设备或在运行中重启Vulkan（皆涉及重建逻辑设备），那么此CleanUp回调非必要
            // 程序运行结束时，无论是否有这个回调，graphicsBasePlus中的对象必会在析构graphicsBase前被析构掉
            auto CleanUp = [] {
                singleton.commandPool_graphics.~commandPool();
                singleton.commandPool_presentation.~commandPool();
                singleton.commandPool_compute.~commandPool();
            };

            graphicsBase::Plus(singleton);
            graphicsBase::Base().AddCallback_CreateDevice(Initialize);
            graphicsBase::Base().AddCallback_DestroyDevice(CleanUp);
        }

        graphicsBasePlus(graphicsBasePlus &&) = delete;

        ~graphicsBasePlus() = default;

    public:
        // Getter
        const VkFormatProperties &FormatProperties(VkFormat format) const {
#ifndef NDEBUG
            if (uint32_t(format) >= std::size(formatInfos_v1_0)) {
                std::println(
                    "[ FormatProperties ] ERROR\nThis function only supports "
                    "definite formats provided by VK_VERSION_1_0.\n");
                abort();
            }
#endif
            return formatProperties[format];
        }

        const commandPool &CommandPool_Graphics() const {
            return commandPool_graphics;
        }

        const commandPool &CommandPool_Compute() const {
            return commandPool_compute;
        }

        const commandBuffer &CommandBuffer_Transfer() const {
            return commandBuffer_transfer;
        }

        // 简化命令提交
        result_t
        ExecuteCommandBuffer_Graphics(VkCommandBuffer commandBuffer) const {
            fence fence;
            VkSubmitInfo submitInfo {};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            VkResult result = graphicsBase::Base().SubmitCommandBuffer_Graphics(
                submitInfo, fence);

            if (!result) {
                fence.Wait();
            }

            return result;
        }

        // 该函数专用于向呈现队列提交用于接收交换链图像的队列族所有权的命令缓冲区
        result_t AcquireImageOwnership_Presentation(
            VkSemaphore semaphore_renderingIsOver,
            VkSemaphore semaphore_ownershipIsTransfered,
            VkFence fence = VK_NULL_HANDLE) const {

            if (VkResult result = commandBuffer_presentation.Begin(
                    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) {
                return result;
            }

            graphicsBase::Base().CmdTransferImageOwnership(
                commandBuffer_presentation);

            if (VkResult result = commandBuffer_presentation.End()) {
                return result;
            }

            return graphicsBase::Base().SubmitCommandBuffer_Presentation(
                commandBuffer_presentation, semaphore_renderingIsOver,
                semaphore_ownershipIsTransfered, fence);
        }
    };

    constexpr formatInfo FormatInfo(VkFormat format) {
#ifndef NDEBUG
        if (uint32_t(format) >= std::size(formatInfos_v1_0)) {
            std::println("[ FormatInfo ] ERROR\nThis function only supports "
                         "definite formats provided by VK_VERSION_1_0.\n");
            abort();
        }
#endif
        return formatInfos_v1_0[format];
    }

    constexpr VkFormat
    Corresponding16BitFloatFormat(VkFormat format_32BitFloat) {
        switch (format_32BitFloat) {
        case VK_FORMAT_R32_SFLOAT: return VK_FORMAT_R16_SFLOAT;
        case VK_FORMAT_R32G32_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
        case VK_FORMAT_R32G32B32_SFLOAT: return VK_FORMAT_R16G16B16_SFLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        }
        return format_32BitFloat;
    }

    inline const VkFormatProperties &FormatProperties(VkFormat format) {
        return graphicsBase::Plus().FormatProperties(format);
    }

    inline graphicsBasePlus graphicsBasePlus::singleton;

    struct graphicsPipelineCreateInfoPack {
        VkGraphicsPipelineCreateInfo createInfo = {
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
        };

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

        // Vertex Input
        VkPipelineVertexInputStateCreateInfo vertexInputStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        std::vector<VkVertexInputBindingDescription> vertexInputBindings;
        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;

        // Input Assembly
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };

        // Tessellation
        VkPipelineTessellationStateCreateInfo tessellationStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO
        };

        // Viewport
        VkPipelineViewportStateCreateInfo viewportStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };

        std::vector<VkViewport> viewports;
        std::vector<VkRect2D> scissors;

        uint32_t dynamicViewportCount =
            1; // 动态视口/剪裁不会用到上述的
               // vector，因此动态视口和剪裁的个数向这俩变量手动指定
        uint32_t dynamicScissorCount = 1;

        // Rasterization
        VkPipelineRasterizationStateCreateInfo rasterizationStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };

        // Multisample
        VkPipelineMultisampleStateCreateInfo multisampleStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };

        // Depth & Stencil
        VkPipelineDepthStencilStateCreateInfo depthStencilStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };

        // Color Blend
        VkPipelineColorBlendStateCreateInfo colorBlendStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };

        std::vector<VkPipelineColorBlendAttachmentState>
            colorBlendAttachmentStates;

        // Dynamic
        VkPipelineDynamicStateCreateInfo dynamicStateCi = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };

        std::vector<VkDynamicState> dynamicStates;

        //--------------------
        graphicsPipelineCreateInfoPack() {
            SetCreateInfos();
            // 若非派生管线，createInfo.basePipelineIndex不得为0，设置为-1
            createInfo.basePipelineIndex = -1;
        }

        // 移动构造器，所有指针都要重新赋值
        graphicsPipelineCreateInfoPack(
            const graphicsPipelineCreateInfoPack &other) noexcept {
            createInfo = other.createInfo;
            SetCreateInfos();

            vertexInputStateCi = other.vertexInputStateCi;
            inputAssemblyStateCi = other.inputAssemblyStateCi;
            tessellationStateCi = other.tessellationStateCi;
            viewportStateCi = other.viewportStateCi;
            rasterizationStateCi = other.rasterizationStateCi;
            multisampleStateCi = other.multisampleStateCi;
            depthStencilStateCi = other.depthStencilStateCi;
            colorBlendStateCi = other.colorBlendStateCi;
            dynamicStateCi = other.dynamicStateCi;

            shaderStages = other.shaderStages;
            vertexInputBindings = other.vertexInputBindings;
            vertexInputAttributes = other.vertexInputAttributes;
            viewports = other.viewports;
            scissors = other.scissors;
            colorBlendAttachmentStates = other.colorBlendAttachmentStates;
            dynamicStates = other.dynamicStates;
            UpdateAllArrayAddresses();
        }

        // Getter，这里我没用const修饰符
        operator VkGraphicsPipelineCreateInfo &() { return createInfo; }

        // Non-const Function
        // 该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，并相应改变各个count
        void UpdateAllArrays() {
            createInfo.stageCount = shaderStages.size();
            vertexInputStateCi.vertexBindingDescriptionCount =
                vertexInputBindings.size();
            vertexInputStateCi.vertexAttributeDescriptionCount =
                vertexInputAttributes.size();
            viewportStateCi.viewportCount = viewports.size()
                                                ? uint32_t(viewports.size())
                                                : dynamicViewportCount;
            viewportStateCi.scissorCount = scissors.size()
                                               ? uint32_t(scissors.size())
                                               : dynamicScissorCount;
            colorBlendStateCi.attachmentCount =
                colorBlendAttachmentStates.size();
            dynamicStateCi.dynamicStateCount = dynamicStates.size();
            UpdateAllArrayAddresses();
        }

    private:
        // 该函数用于将创建信息的地址赋值给basePipelineIndex中相应成员
        void SetCreateInfos() {
            createInfo.pVertexInputState = &vertexInputStateCi;
            createInfo.pInputAssemblyState = &inputAssemblyStateCi;
            createInfo.pTessellationState = &tessellationStateCi;
            createInfo.pViewportState = &viewportStateCi;
            createInfo.pRasterizationState = &rasterizationStateCi;
            createInfo.pMultisampleState = &multisampleStateCi;
            createInfo.pDepthStencilState = &depthStencilStateCi;
            createInfo.pColorBlendState = &colorBlendStateCi;
            createInfo.pDynamicState = &dynamicStateCi;
        }

        // 该函数用于将各个vector中数据的地址赋值给各个创建信息中相应成员，但不改变各个count
        void UpdateAllArrayAddresses() {
            createInfo.pStages = shaderStages.data();
            vertexInputStateCi.pVertexBindingDescriptions =
                vertexInputBindings.data();
            vertexInputStateCi.pVertexAttributeDescriptions =
                vertexInputAttributes.data();
            viewportStateCi.pViewports = viewports.data();
            viewportStateCi.pScissors = scissors.data();
            colorBlendStateCi.pAttachments = colorBlendAttachmentStates.data();
            dynamicStateCi.pDynamicStates = dynamicStates.data();
        }
    };

} // namespace vulkan
