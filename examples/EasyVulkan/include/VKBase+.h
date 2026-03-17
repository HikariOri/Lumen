/**
 * @file VKBase+.h
 * @brief Vulkan 图形基础扩展模块，提供命令池、暂存缓冲区和图形管线创建信息封装
 */

#pragma once

#include <vector>

#include <vulkan/vulkan_core.h>

#include "VKBase.h"
#include "VKFormat.h"

namespace vulkan {

    /**
     * @class graphicsBasePlus
     * @brief 图形基础扩展单例类，管理命令池、命令缓冲区和格式属性
     */
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
        /**
         * @brief 获取指定 Vulkan 格式的属性
         * @param format 要查询的 VkFormat 枚举值
         * @return 对应格式的 VkFormatProperties 引用
         */
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

        /** @brief 获取图形命令池 */
        const commandPool &CommandPool_Graphics() const {
            return commandPool_graphics;
        }

        /** @brief 获取计算命令池 */
        const commandPool &CommandPool_Compute() const {
            return commandPool_compute;
        }

        /** @brief 获取传输用命令缓冲区 */
        const commandBuffer &CommandBuffer_Transfer() const {
            return commandBuffer_transfer;
        }

        /**
         * @brief 向图形队列提交命令缓冲区并等待完成
         * @param commandBuffer 要提交的 VkCommandBuffer
         * @return Vulkan 操作结果
         */
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

        /**
         * @brief 向呈现队列提交获取交换链图像队列族所有权的命令缓冲区
         * @param semaphore_renderingIsOver 渲染完成信号量
         * @param semaphore_ownershipIsTransfered 所有权转移完成信号量
         * @param fence 可选的栅栏对象
         * @return Vulkan 操作结果
         */
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

    /**
     * @brief 获取指定 Vulkan 格式的 formatInfo 信息
     * @param format 要查询的 VkFormat 枚举值
     * @return 对应格式的 formatInfo 结构
     */
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

    /**
     * @brief 获取 32 位浮点格式对应的 16 位浮点格式
     * @param format_32BitFloat 32 位浮点 VkFormat
     * @return 对应的 16 位浮点格式，若无对应则返回原格式
     */
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

    /** @brief 便捷函数，获取格式属性 */
    inline const VkFormatProperties &FormatProperties(VkFormat format) {
        return graphicsBase::Plus().FormatProperties(format);
    }

    inline graphicsBasePlus graphicsBasePlus::singleton;

    /**
     * @struct graphicsPipelineCreateInfoPack
     * @brief 图形管线创建信息封装，集中管理各阶段状态和数组地址
     */
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

        /** @brief 默认构造，初始化各 CreateInfo 并设置 basePipelineIndex */
        graphicsPipelineCreateInfoPack() {
            SetCreateInfos();
            // 若非派生管线，createInfo.basePipelineIndex不得为0，设置为-1
            createInfo.basePipelineIndex = -1;
        }

        /**
         * @brief 复制构造函数，拷贝后需重新设置各 CreateInfo 指针
         * @param other 源对象
         */
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

        /** @brief 转换为 VkGraphicsPipelineCreateInfo 引用 */
        operator VkGraphicsPipelineCreateInfo &() { return createInfo; }

        /**
         * @brief 更新各 vector 的地址到创建信息并同步 count
         */
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
        /** @brief 将各 CreateInfo 地址赋值给 createInfo 对应成员 */
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

        /** @brief 更新各 vector 数据地址到 CreateInfo，不修改 count */
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
    /**
     * @class stagingBuffer
     * @brief 暂存缓冲区，用于 CPU 与 GPU 之间的数据传输
     *
     * 支持主机可见内存映射、设备数据读写，可创建与缓冲区共享内存的混叠 2D
     * 图像。 提供主线程单例的静态方法，方便在任意处访问全局暂存缓冲。
     */
    class stagingBuffer {

        /** @brief 主线程单例访问器 */
        static inline class stagingBuffer_mainThread_t {
            stagingBuffer *pointer;
            stagingBuffer *Create() {
                static stagingBuffer stagingBuffer;
                graphicsBase::Base().AddCallback_DestroyDevice(
                    [] { stagingBuffer.~stagingBuffer(); });
                return &stagingBuffer;
            }

        public:
            stagingBuffer_mainThread_t() : pointer(Create()) {}
            /** @brief 获取主线程单例暂存缓冲引用 */
            stagingBuffer &Get() const { return *pointer; }
        } stagingBuffer_mainThread;

    protected:
        bufferMemory bufferMemory;    /**< 底层缓冲区与设备内存 */
        VkDeviceSize memoryUsage = 0; /**< 当前映射的字节数 */
        image aliasedImage;           /**< 与缓冲区共享内存的混叠图像（可选） */

    public:
        /** @brief 默认构造 */
        stagingBuffer() = default;

        /**
         * @brief 构造并分配指定大小的缓冲区
         * @param size 初始容量（字节）
         */
        stagingBuffer(VkDeviceSize size) { Expand(size); }

        /** @brief 隐式转换为 VkBuffer */
        operator VkBuffer() const { return bufferMemory.Buffer(); }

        /** @brief 获取 VkBuffer 指针地址 */
        const VkBuffer *Address() const {
            return bufferMemory.AddressOfBuffer();
        }

        /** @brief 获取已分配内存大小 */
        VkDeviceSize AllocationSize() const {
            return bufferMemory.AllocationSize();
        }

        /** @brief 获取混叠图像（若存在） */
        VkImage AliasedImage() const { return aliasedImage; }

        /**
         * @brief 从缓冲区取回数据到 CPU
         * @param pData_src 目标主机内存指针
         * @param size 要读取的字节数
         */
        void RetrieveData(void *pData_src, VkDeviceSize size) const {
            bufferMemory.RetrieveData(pData_src, size);
        }

        /**
         * @brief 扩展缓冲区容量，不足时重新分配
         * @param size 期望的最小字节数
         */
        void Expand(VkDeviceSize size) {
            if (size <= AllocationSize())
                return;
            Release();
            VkBufferCreateInfo bufferCreateInfo = {
                .size = size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT
            };
            bufferMemory.Create(bufferCreateInfo,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        }

        /** @brief 手动释放所有内存并销毁缓冲区和设备内存 */
        void Release() { bufferMemory.~bufferMemory(); }

        /**
         * @brief 映射缓冲区内存到主机地址空间
         * @param size 要映射的字节数
         * @return 映射后的主机指针
         */
        void *MapMemory(VkDeviceSize size) {
            Expand(size);
            void *pData_dst = nullptr;
            bufferMemory.MapMemory(pData_dst, size);
            memoryUsage = size;
            return pData_dst;
        }

        /** @brief 取消内存映射 */
        void UnmapMemory() {
            bufferMemory.UnmapMemory(memoryUsage);
            memoryUsage = 0;
        }

        /**
         * @brief 向缓冲区写入数据
         * @param pData_src 源数据指针
         * @param size 要写入的字节数
         */
        void BufferData(const void *pData_src, VkDeviceSize size) {
            Expand(size);
            bufferMemory.BufferData(pData_src, size);
        }

        /**
         * @brief 创建线性布局的混叠 2D 图像（与缓冲区共享内存）
         * @param format 图像格式
         * @param extent 图像尺寸
         * @return 创建的 VkImage 句柄，不支持时返回 VK_NULL_HANDLE
         */
        [[nodiscard]]
        VkImage AliasedImage2d(VkFormat format, VkExtent2D extent) {
            if (!(FormatProperties(format).linearTilingFeatures &
                  VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
                return VK_NULL_HANDLE;
            }
            VkDeviceSize imageDataSize =
                VkDeviceSize(FormatInfo(format).sizePerPixel) * extent.width *
                extent.height;
            if (imageDataSize > AllocationSize()) {
                return VK_NULL_HANDLE;
            }
            VkImageFormatProperties imageFormatProperties = {};
            vkGetPhysicalDeviceImageFormatProperties(
                graphicsBase::Base().PhysicalDevice(), format, VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0,
                &imageFormatProperties);
            if (extent.width > imageFormatProperties.maxExtent.width ||
                extent.height > imageFormatProperties.maxExtent.height ||
                imageDataSize > imageFormatProperties.maxResourceSize) {
                return VK_NULL_HANDLE;
            }
            VkImageCreateInfo imageCreateInfo = {
                .imageType = VK_IMAGE_TYPE_2D,
                .format = format,
                .extent = { extent.width, extent.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_LINEAR,
                .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED
            };
            aliasedImage.~image();
            aliasedImage.Create(imageCreateInfo);
            VkImageSubresource subResource = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                               0 };
            VkSubresourceLayout subresourceLayout = {};
            vkGetImageSubresourceLayout(graphicsBase::Base().Device(),
                                        aliasedImage, &subResource,
                                        &subresourceLayout);
            if (subresourceLayout.size != imageDataSize) {
                return VK_NULL_HANDLE;
            }
            aliasedImage.BindMemory(bufferMemory.Memory());
            return aliasedImage;
        }

        /** @brief 获取主线程单例暂存缓冲的 VkBuffer */
        static VkBuffer Buffer_MainThread() {
            return stagingBuffer_mainThread.Get();
        }

        /** @brief 主线程单例：扩展缓冲区容量 */
        static void Expand_MainThread(VkDeviceSize size) {
            stagingBuffer_mainThread.Get().Expand(size);
        }

        /** @brief 主线程单例：释放缓冲区 */
        static void Release_MainThread() {
            stagingBuffer_mainThread.Get().Release();
        }

        /** @brief 主线程单例：映射内存 */
        static void *MapMemory_MainThread(VkDeviceSize size) {
            return stagingBuffer_mainThread.Get().MapMemory(size);
        }

        /** @brief 主线程单例：取消映射 */
        static void UnmapMemory_MainThread() {
            stagingBuffer_mainThread.Get().UnmapMemory();
        }

        /** @brief 主线程单例：写入数据 */
        static void BufferData_MainThread(const void *pData_src,
                                          VkDeviceSize size) {
            stagingBuffer_mainThread.Get().BufferData(pData_src, size);
        }

        /** @brief 主线程单例：取回数据 */
        static void RetrieveData_MainThread(void *pData_src,
                                            VkDeviceSize size) {
            stagingBuffer_mainThread.Get().RetrieveData(pData_src, size);
        }

        /** @brief 主线程单例：创建混叠 2D 图像 */
        [[nodiscard]]
        static VkImage AliasedImage2d_MainThread(VkFormat format,
                                                 VkExtent2D extent) {
            return stagingBuffer_mainThread.Get().AliasedImage2d(format,
                                                                 extent);
        }
    };

    /**
     * @class deviceLocalBuffer
     * @brief 设备本地缓冲区，优先使用 device-local 内存
     *
     * 自动尝试 device-local + host-visible（集成显卡）或纯
     * device-local（独显）。 提供从 CPU
     * 上传数据的多种方式：TransferData、CmdUpdateBuffer。
     */
    class deviceLocalBuffer {
    protected:
        bufferMemory bufferMemory; /**< 底层缓冲区与设备内存 */

    public:
        /** @brief 默认构造 */
        deviceLocalBuffer() = default;

        /**
         * @brief 构造并创建指定大小的设备本地缓冲区
         * @param size 缓冲区大小（字节）
         * @param desiredUsages_Without_transfer_dst 期望的 usage 标志（不含
         * TRANSFER_DST，会自动添加）
         */
        deviceLocalBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags desiredUsages_Without_transfer_dst) {
            Create(size, desiredUsages_Without_transfer_dst);
        }

        /** @brief 隐式转换为 VkBuffer */
        operator VkBuffer() const { return bufferMemory.Buffer(); }

        /** @brief 获取 VkBuffer 指针地址 */
        const VkBuffer *Address() const {
            return bufferMemory.AddressOfBuffer();
        }

        /** @brief 获取已分配内存大小 */
        VkDeviceSize AllocationSize() const {
            return bufferMemory.AllocationSize();
        }

        /**
         * @brief 将连续数据从 CPU 上传到缓冲区
         * @param pData_src 源数据指针
         * @param size 要传输的字节数
         * @param offset 目标缓冲区偏移（字节）
         */
        void TransferData(const void *pData_src, VkDeviceSize size,
                          VkDeviceSize offset = 0) const {
            if (bufferMemory.MemoryProperties() &
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                bufferMemory.BufferData(pData_src, size, offset);
                return;
            }
            stagingBuffer::BufferData_MainThread(pData_src, size);
            const auto &commandBuffer =
                graphicsBase::Plus().CommandBuffer_Transfer();
            commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            VkBufferCopy region { 0, offset, size };
            vkCmdCopyBuffer(commandBuffer, stagingBuffer::Buffer_MainThread(),
                            bufferMemory.Buffer(), 1, &region);
            commandBuffer.End();
            graphicsBase::Plus().ExecuteCommandBuffer_Graphics(commandBuffer);
        }

        /**
         * @brief 按元素步长将数据从 CPU 上传到缓冲区
         * @param pData_src 源数据指针
         * @param elementCount 元素个数
         * @param elementSize 每个元素的有效字节数
         * @param stride_src 源数据步长（字节）
         * @param stride_dst 目标缓冲区步长（字节）
         * @param offset 目标缓冲区起始偏移（字节）
         */
        void TransferData(const void *pData_src, uint32_t elementCount,
                          VkDeviceSize elementSize, VkDeviceSize stride_src,
                          VkDeviceSize stride_dst,
                          VkDeviceSize offset = 0) const {
            if (bufferMemory.MemoryProperties() &
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                void *pData_dst = nullptr;
                bufferMemory.MapMemory(pData_dst, stride_dst * elementCount,
                                       offset);
                for (size_t i = 0; i < elementCount; i++) {
                    memcpy(stride_dst * i + static_cast<uint8_t *>(pData_dst),
                           stride_src * i +
                               static_cast<const uint8_t *>(pData_src),
                           size_t(elementSize));
                }
                bufferMemory.UnmapMemory(elementCount * stride_dst, offset);
                return;
            }
            stagingBuffer::BufferData_MainThread(pData_src,
                                                 stride_src * elementCount);
            auto &commandBuffer = graphicsBase::Plus().CommandBuffer_Transfer();
            commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            std::unique_ptr<VkBufferCopy[]> regions =
                std::make_unique<VkBufferCopy[]>(elementCount);
            for (size_t i = 0; i < elementCount; i++) {
                regions[i] = { stride_src * i, stride_dst * i + offset,
                               elementSize };
            }
            vkCmdCopyBuffer(commandBuffer, stagingBuffer::Buffer_MainThread(),
                            bufferMemory.Buffer(), elementCount, regions.get());
            commandBuffer.End();
            graphicsBase::Plus().ExecuteCommandBuffer_Graphics(commandBuffer);
        }

        /**
         * @brief 上传单个对象到缓冲区
         * @param data_src 源对象引用
         */
        void TransferData(const auto &data_src) const {
            TransferData(&data_src, sizeof data_src);
        }

        /**
         * @brief 在命令缓冲区中记录直接更新缓冲区内容（限于 65536 字节）
         * @param commandBuffer 录制中的命令缓冲区
         * @param pData_src 源数据指针
         * @param size_Limited_to_65536 要更新的字节数（不超过 65536）
         * @param offset 缓冲区偏移（字节）
         */
        void CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                             const void *pData_src,
                             VkDeviceSize size_Limited_to_65536,
                             VkDeviceSize offset = 0) const {
            vkCmdUpdateBuffer(commandBuffer, bufferMemory.Buffer(), offset,
                              size_Limited_to_65536, pData_src);
        }

        /**
         * @brief 在命令缓冲区中更新单个对象到缓冲区
         * @param commandBuffer 录制中的命令缓冲区
         * @param data_src 源对象引用
         */
        void CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                             const auto &data_src) const {
            vkCmdUpdateBuffer(commandBuffer, bufferMemory.Buffer(), 0,
                              sizeof data_src, &data_src);
        }

        /**
         * @brief 创建设备本地缓冲区
         * @param size 缓冲区大小（字节）
         * @param desiredUsages_Without_transfer_dst 期望的 usage（不含
         * TRANSFER_DST，自动添加）
         */
        void Create(VkDeviceSize size,
                    VkBufferUsageFlags desiredUsages_Without_transfer_dst) {
            VkBufferCreateInfo bufferCreateInfo = {
                .size = size,
                .usage = desiredUsages_Without_transfer_dst |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT
            };
            false || bufferMemory.CreateBuffer(bufferCreateInfo) ||
                bufferMemory.AllocateMemory(
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                    bufferMemory.AllocateMemory(
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
                bufferMemory.BindMemory();
        }

        /**
         * @brief 等待设备空闲后销毁并重新创建缓冲区
         * @param size 新的缓冲区大小（字节）
         * @param desiredUsages_Without_transfer_dst 期望的 usage（不含
         * TRANSFER_DST）
         */
        void Recreate(VkDeviceSize size,
                      VkBufferUsageFlags desiredUsages_Without_transfer_dst) {
            graphicsBase::Base().WaitIdle();
            bufferMemory.~bufferMemory();
            Create(size, desiredUsages_Without_transfer_dst);
        }
    };

    /**
     * @class vertexBuffer
     * @brief 顶点缓冲区，继承 deviceLocalBuffer，固定 VERTEX_BUFFER usage
     */
    class vertexBuffer : public deviceLocalBuffer {
    public:
        /** @brief 默认构造 */
        vertexBuffer() = default;

        /**
         * @brief 构造并创建顶点缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        vertexBuffer(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0)
            : deviceLocalBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                          otherUsages) {}

        /**
         * @brief 创建顶点缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Create(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Create(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                otherUsages);
        }

        /**
         * @brief 重建顶点缓冲区
         * @param size 新的缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Recreate(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Recreate(
                size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | otherUsages);
        }
    };

    /**
     * @class indexBuffer
     * @brief 索引缓冲区，继承 deviceLocalBuffer，固定 INDEX_BUFFER usage
     */
    class indexBuffer : public deviceLocalBuffer {
    public:
        /** @brief 默认构造 */
        indexBuffer() = default;

        /**
         * @brief 构造并创建索引缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        indexBuffer(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0)
            : deviceLocalBuffer(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                          otherUsages) {}

        /**
         * @brief 创建索引缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Create(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Create(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                otherUsages);
        }

        /**
         * @brief 重建索引缓冲区
         * @param size 新的缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Recreate(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Recreate(size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                  otherUsages);
        }
    };

    /**
     * @class uniformBuffer
     * @brief 统一缓冲区，继承 deviceLocalBuffer，固定 UNIFORM_BUFFER usage
     *
     * 提供 CalculateAlignedSize 以符合 minUniformBufferOffsetAlignment。
     */
    class uniformBuffer : public deviceLocalBuffer {
    public:
        /** @brief 默认构造 */
        uniformBuffer() = default;

        /**
         * @brief 构造并创建统一缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        uniformBuffer(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0)
            : deviceLocalBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                          otherUsages) {}

        /**
         * @brief 创建统一缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Create(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Create(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                otherUsages);
        }

        /**
         * @brief 重建统一缓冲区
         * @param size 新的缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Recreate(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Recreate(
                size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | otherUsages);
        }

        /**
         * @brief 计算满足 minUniformBufferOffsetAlignment 的对齐后大小
         * @param dataSize 原始数据大小（字节）
         * @return 对齐后的字节数
         */
        static VkDeviceSize CalculateAlignedSize(VkDeviceSize dataSize) {
            const VkDeviceSize &alignment =
                graphicsBase::Base()
                    .PhysicalDeviceProperties()
                    .limits.minUniformBufferOffsetAlignment;
            return dataSize + alignment - 1 & ~(alignment - 1);
        }
    };

    /**
     * @class storageBuffer
     * @brief 存储缓冲区，继承 deviceLocalBuffer，固定 STORAGE_BUFFER usage
     *
     * 提供 CalculateAlignedSize 以符合 minStorageBufferOffsetAlignment。
     */
    class storageBuffer : public deviceLocalBuffer {
    public:
        /** @brief 默认构造 */
        storageBuffer() = default;

        /**
         * @brief 构造并创建存储缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        storageBuffer(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0)
            : deviceLocalBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          otherUsages) {}

        /**
         * @brief 创建存储缓冲区
         * @param size 缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Create(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Create(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                otherUsages);
        }

        /**
         * @brief 重建存储缓冲区
         * @param size 新的缓冲区大小（字节）
         * @param otherUsages 附加 usage 标志（可选）
         */
        void Recreate(VkDeviceSize size, VkBufferUsageFlags otherUsages = 0) {
            deviceLocalBuffer::Recreate(
                size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | otherUsages);
        }

        /**
         * @brief 计算满足 minStorageBufferOffsetAlignment 的对齐后大小
         * @param dataSize 原始数据大小（字节）
         * @return 对齐后的字节数
         */
        static VkDeviceSize CalculateAlignedSize(VkDeviceSize dataSize) {
            const VkDeviceSize &alignment =
                graphicsBase::Base()
                    .PhysicalDeviceProperties()
                    .limits.minStorageBufferOffsetAlignment;
            return dataSize + alignment - 1 & ~(alignment - 1);
        }
    };

    /**
     * @struct imageOperation
     * @brief 图像操作工具类，封装 buffer→image 拷贝、图像 blit 和 mipmap 生成
     *
     * 提供带自动内存屏障的 vkCmdCopyBufferToImage、vkCmdBlitImage 封装，
     * 以及 2D 图像 mipmap 链生成。所有操作需在已开始的命令缓冲区内调用。
     */
    struct imageOperation {

        /**
         * @struct imageMemoryBarrierParameterPack
         * @brief 图像内存屏障参数包，用于指定操作前后布局/访问/阶段
         *
         * 默认构造表示不需要对应屏障；带参数构造表示需要该方向的屏障。
         */
        struct imageMemoryBarrierParameterPack {
            const bool isNeeded = false;          ///< 是否需要执行该屏障
            const VkPipelineStageFlags stage = 0; ///< 管道阶段
            const VkAccessFlags access = 0;       ///< 访问掩码
            const VkImageLayout layout =
                VK_IMAGE_LAYOUT_UNDEFINED; ///< 目标布局
            constexpr imageMemoryBarrierParameterPack() = default;

            /**
             * @brief 构造需要执行的屏障参数
             * @param stage 管道阶段
             * @param access 访问掩码
             * @param layout 目标布局
             */
            constexpr imageMemoryBarrierParameterPack(
                VkPipelineStageFlags stage, VkAccessFlags access,
                VkImageLayout layout)
                : isNeeded(true), stage(stage), access(access), layout(layout) {
            }
        };

        /**
         * @brief 将缓冲区数据拷贝到图像（带可选的转换前后屏障）
         * @param commandBuffer 命令缓冲区
         * @param buffer 源缓冲区
         * @param image 目标图像
         * @param region 拷贝区域
         * @param imb_from 拷贝前屏障参数（源布局→TRANSFER_DST）
         * @param imb_to 拷贝后屏障参数（TRANSFER_DST→目标布局）
         */
        static void
        CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer,
                             VkImage image, const VkBufferImageCopy &region,
                             imageMemoryBarrierParameterPack imb_from,
                             imageMemoryBarrierParameterPack imb_to) {
            // Pre-copy barrier
            VkImageMemoryBarrier imageMemoryBarrier = {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                nullptr,
                imb_from.access,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                imb_from.layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED, // No ownership transfer
                VK_QUEUE_FAMILY_IGNORED,
                image,
                { region.imageSubresource.aspectMask,
                  region.imageSubresource.mipLevel, 1,
                  region.imageSubresource.baseArrayLayer,
                  region.imageSubresource.layerCount }
            };

            if (imb_from.isNeeded) {
                vkCmdPipelineBarrier(commandBuffer, imb_from.stage,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1,
                                     &imageMemoryBarrier);
            }

            // Copy
            vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &region);

            // Post-copy barrier
            if (imb_to.isNeeded) {
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrier.oldLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrier.dstAccessMask = imb_to.access;
                imageMemoryBarrier.newLayout = imb_to.layout;
                vkCmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, imb_to.stage,
                    0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            }
        }

        /**
         * @brief 执行图像 blit（带可选的转换前后屏障）
         * @param commandBuffer 命令缓冲区
         * @param image_src 源图像（需已为 TRANSFER_SRC_OPTIMAL）
         * @param image_dst 目标图像
         * @param region blit 区域
         * @param imb_dst_from 拷贝前目标图像屏障（源布局→TRANSFER_DST）
         * @param imb_dst_to 拷贝后目标图像屏障（TRANSFER_DST→目标布局）
         * @param filter 缩放滤波（默认 LINEAR）
         */
        static void CmdBlitImage(VkCommandBuffer commandBuffer,
                                 VkImage image_src, VkImage image_dst,
                                 const VkImageBlit &region,
                                 imageMemoryBarrierParameterPack imb_dst_from,
                                 imageMemoryBarrierParameterPack imb_dst_to,
                                 VkFilter filter = VK_FILTER_LINEAR) {
            // Pre-blit barrier
            VkImageMemoryBarrier imageMemoryBarrier = {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                nullptr,
                imb_dst_from.access,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                imb_dst_from.layout,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                image_dst,
                { region.dstSubresource.aspectMask,
                  region.dstSubresource.mipLevel, 1,
                  region.dstSubresource.baseArrayLayer,
                  region.dstSubresource.layerCount }
            };

            if (imb_dst_from.isNeeded) {
                vkCmdPipelineBarrier(commandBuffer, imb_dst_from.stage,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                     nullptr, 0, nullptr, 1,
                                     &imageMemoryBarrier);
            }

            // Blit
            vkCmdBlitImage(commandBuffer, image_src,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_dst,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                           filter);

            // Post-blit barrier
            if (imb_dst_to.isNeeded) {
                imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                imageMemoryBarrier.oldLayout =
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                imageMemoryBarrier.dstAccessMask = imb_dst_to.access;
                imageMemoryBarrier.newLayout = imb_dst_to.layout;
                vkCmdPipelineBarrier(commandBuffer,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     imb_dst_to.stage, 0, 0, nullptr, 0,
                                     nullptr, 1, &imageMemoryBarrier);
            }
        }

        /**
         * @brief 为 2D 图像生成 mipmap 链（从 mip0 blit 到各级）
         * @param commandBuffer 命令缓冲区
         * @param image 目标图像（mip0 需已为 TRANSFER_SRC_OPTIMAL）
         * @param imageExtent 图像尺寸
         * @param mipLevelCount mip 级数
         * @param layerCount 数组层数
         * @param imb_to 完成后整幅图像的屏障（TRANSFER_SRC→目标布局）
         * @param minFilter blit 使用的滤波（默认 LINEAR）
         */
        static void CmdGenerateMipmap2d(VkCommandBuffer commandBuffer,
                                        VkImage image, VkExtent2D imageExtent,
                                        uint32_t mipLevelCount,
                                        uint32_t layerCount,
                                        imageMemoryBarrierParameterPack imb_to,
                                        VkFilter minFilter = VK_FILTER_LINEAR) {

            auto MipmapExtent = [](VkExtent2D imageExtent, uint32_t mipLevel) {
                VkOffset3D extent = { int32_t(imageExtent.width >> mipLevel),
                                      int32_t(imageExtent.height >> mipLevel),
                                      1 };
                extent.x += !extent.x;
                extent.y += !extent.y;
                return extent;
            };

            // Blit
            if (layerCount > 1) {
                std::unique_ptr<VkImageBlit[]> regions =
                    std::make_unique<VkImageBlit[]>(layerCount);
                for (uint32_t i = 1; i < mipLevelCount; i++) {
                    VkOffset3D mipmapExtent_src =
                        MipmapExtent(imageExtent, i - 1);
                    VkOffset3D mipmapExtent_dst = MipmapExtent(imageExtent, i);
                    for (uint32_t j = 0; j < layerCount; j++) {
                        regions[j] = {
                            { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, j,
                              1 },                    // srcSubresource
                            { {}, mipmapExtent_src }, // srcOffsets
                            { VK_IMAGE_ASPECT_COLOR_BIT, i, j,
                              1 },                   // dstSubresource
                            { {}, mipmapExtent_dst } // dstOffsets
                        };
                    }

                    // Pre-blit barrier
                    VkImageMemoryBarrier imageMemoryBarrier {
                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        nullptr,
                        0,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED,
                        VK_QUEUE_FAMILY_IGNORED,
                        image,
                        { VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, layerCount }
                    };

                    vkCmdPipelineBarrier(
                        commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                        nullptr, 1, &imageMemoryBarrier);

                    // Blit
                    vkCmdBlitImage(commandBuffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   layerCount, regions.get(), minFilter);

                    // Post-blit barrier
                    imageMemoryBarrier.srcAccessMask =
                        VK_ACCESS_TRANSFER_WRITE_BIT;
                    imageMemoryBarrier.oldLayout =
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    imageMemoryBarrier.dstAccessMask =
                        VK_ACCESS_TRANSFER_READ_BIT;
                    imageMemoryBarrier.newLayout =
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    vkCmdPipelineBarrier(
                        commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                        nullptr, 1, &imageMemoryBarrier);
                }
            } else {
                for (uint32_t i = 1; i < mipLevelCount; i++) {
                    VkImageBlit region {
                        { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0,
                          layerCount }, // srcSubresource
                        { {}, MipmapExtent(imageExtent, i - 1) }, // srcOffsets
                        { VK_IMAGE_ASPECT_COLOR_BIT, i, 0,
                          layerCount },                      // dstSubresource
                        { {}, MipmapExtent(imageExtent, i) } // dstOffsets
                    };

                    CmdBlitImage(commandBuffer, image, image, region,
                                 { VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                   VK_IMAGE_LAYOUT_UNDEFINED },
                                 { VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
                                 minFilter);
                }
            }

            // Post-blit barrier
            if (imb_to.isNeeded) {
                VkImageMemoryBarrier imageMemoryBarrier = {
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    nullptr,
                    0,
                    imb_to.access,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    imb_to.layout,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    image,
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevelCount, 0,
                      layerCount }
                };

                vkCmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, imb_to.stage,
                    0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
            }
        }
    };

    /**
     * @class texture
     * @brief Vulkan
     * 纹理基类，封装图像内存和图像视图，支持从文件或内存加载图像数据
     *
     * 提供 transfer/sampled
     * 用途的图像创建、图像加载（stb_image）及描述符信息生成。
     * 派生类需实现具体纹理类型（如 2D、立方体贴图等）的创建逻辑。
     */
    class texture {
    protected:
        imageView imageView;     ///< 图像视图
        imageMemory imageMemory; ///< 图像内存
        //--------------------
        texture() = default;

        /**
         * @brief 创建图像内存（含 transfer/sampled 用途）
         * @param imageType 图像类型
         * @param format 像素格式
         * @param extent 图像尺寸
         * @param mipLevelCount  mip 级数
         * @param arrayLayerCount 数组层数
         * @param flags 图像创建标志
         */
        void CreateImageMemory(VkImageType imageType, VkFormat format,
                               VkExtent3D extent, uint32_t mipLevelCount,
                               uint32_t arrayLayerCount,
                               VkImageCreateFlags flags = 0) {

            VkImageCreateInfo imageCreateInfo {};

            imageCreateInfo.flags = flags;
            imageCreateInfo.imageType = imageType;
            imageCreateInfo.format = format;
            imageCreateInfo.extent = extent;
            imageCreateInfo.mipLevels = mipLevelCount;
            imageCreateInfo.arrayLayers = arrayLayerCount;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT;

            imageMemory.Create(imageCreateInfo,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        /**
         * @brief 创建图像视图
         * @param viewType 视图类型
         * @param format 像素格式
         * @param mipLevelCount mip 级数
         * @param arrayLayer数组层数Count
         * @param flags 视图创建标志
         */
        void CreateImageView(VkImageViewType viewType, VkFormat format,
                             uint32_t mipLevelCount, uint32_t arrayLayerCount,
                             VkImageViewCreateFlags flags = 0) {

            imageView.Create(imageMemory.Image(), viewType, format,
                             { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevelCount, 0,
                               arrayLayerCount },
                             flags);
        }

        /**
         * @brief 从文件路径或内存加载图像数据（内部实现）
         * @param address 文件路径字符串或指向图像二进制的指针
         * @param fileSize 图像数据大小（从内存加载时有效，从文件加载时忽略）
         * @param extent 输出图像宽高
         * @param requiredFormatInfo 所需格式信息（浮点 32 位或整型 8/16 位）
         * @return 加载的图像数据，失败返回空 unique_ptr
         */
        static std::unique_ptr<uint8_t[]>
        LoadFile_Internal(const auto *address, size_t fileSize,
                          VkExtent2D &extent, formatInfo requiredFormatInfo) {
#ifndef NDEBUG
            if (!(requiredFormatInfo.rawDataType == formatInfo::floatingPoint &&
                  requiredFormatInfo.sizePerComponent == 4) &&
                !(requiredFormatInfo.rawDataType == formatInfo::integer &&
                  Between_Closed<int32_t>(
                      1, requiredFormatInfo.sizePerComponent, 2))) {
                std::println("[ texture ] ERROR\nRequired format is not "
                             "available for source image data!\n");
                abort();
            }
#endif

            int &width = reinterpret_cast<int &>(extent.width);
            int &height = reinterpret_cast<int &>(extent.height);
            int channelCount = 0;
            void *pImageData = nullptr;

            if constexpr (std::same_as<decltype(address), const uint8_t *>) {
                if (fileSize > INT32_MAX) {
                    std::println(
                        "[ texture ] ERROR\nFailed to load image data from the "
                        "given address! Data size must be less than 2G!\n");
                    return {};
                }

                constexpr int desired_channels = 4;
                if (requiredFormatInfo.rawDataType == formatInfo::integer) {
                    if (requiredFormatInfo.sizePerComponent == 1) {
                        pImageData = stbi_load_from_memory(
                            address, static_cast<int>(fileSize), &width,
                            &height, &channelCount, desired_channels);
                    } else {
                        pImageData = stbi_load_16_from_memory(
                            address, static_cast<int>(fileSize), &width,
                            &height, &channelCount, desired_channels);
                    }
                } else {
                    pImageData = stbi_loadf_from_memory(
                        address, static_cast<int>(fileSize), &width, &height,
                        &channelCount, desired_channels);
                }
                if (!pImageData) {
                    std::println("[ texture ] ERROR\nFailed to load image data "
                                 "from the given address!\n");
                }
            } else {
                if (requiredFormatInfo.rawDataType == formatInfo::integer) {
                    if (requiredFormatInfo.sizePerComponent == 1) {
                        pImageData =
                            stbi_load(address, &width, &height, &channelCount,
                                      requiredFormatInfo.componentCount);
                    } else {
                        pImageData = stbi_load_16(
                            address, &width, &height, &channelCount,
                            requiredFormatInfo.componentCount);
                    }
                } else {
                    pImageData =
                        stbi_loadf(address, &width, &height, &channelCount,
                                   requiredFormatInfo.componentCount);
                }
                if (!pImageData) {
                    std::println("[ texture ] ERROR\nFailed to load the file: "
                                 "{}\n",
                                 address);
                }
            }
            return std::unique_ptr<uint8_t[]>(
                static_cast<uint8_t *>(pImageData));
        }

    public:
        /** @brief 获取图像视图句柄 */
        VkImageView ImageView() const { return imageView; }

        /** @brief 获取图像句柄 */
        VkImage Image() const { return imageMemory.Image(); }

        /** @brief 获取图像视图指针（用于 VkWriteDescriptorSet 等） */
        const VkImageView *AddressOfImageView() const {
            return imageView.Address();
        }

        /** @brief 获取图像指针 */
        const VkImage *AddressOfImage() const {
            return imageMemory.AddressOfImage();
        }

        /**
         * @brief 获取描述符图像信息（用于写入描述符集）
         * @param sampler 采样器句柄
         * @return VkDescriptorImageInfo 结构体
         */
        VkDescriptorImageInfo DescriptorImageInfo(VkSampler sampler) const {
            return { sampler, imageView,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }

        /**
         * @brief 从文件路径加载图像
         * @param filepath 图像文件路径
         * @param extent 输出图像宽高
         * @param requiredFormatInfo 所需格式信息
         * @return 图像像素数据，失败返回空
         */
        [[nodiscard]]
        static std::unique_ptr<uint8_t[]>
        LoadFile(const char *filepath, VkExtent2D &extent,
                 formatInfo requiredFormatInfo) {
            return LoadFile_Internal(filepath, 0, extent, requiredFormatInfo);
        }

        /**
         * @brief 从内存加载图像
         * @param fileBinaries 图像二进制数据指针
         * @param fileSize 数据大小（需小于 2GB）
         * @param extent 输出图像宽高
         * @param requiredFormatInfo 所需格式信息
         * @return 图像像素数据，失败返回空
         */
        [[nodiscard]]
        static std::unique_ptr<uint8_t[]>
        LoadFile(const uint8_t *fileBinaries, size_t fileSize,
                 VkExtent2D &extent, formatInfo requiredFormatInfo) {
            return LoadFile_Internal(fileBinaries, fileSize, extent,
                                     requiredFormatInfo);
        }

        /**
         * @brief 根据 2D 尺寸计算 mip 级数
         * @param extent 图像尺寸
         * @return mip 级数，公式为 floor(log2(max(w,h))) + 1
         */
        static uint32_t CalculateMipLevelCount(VkExtent2D extent) {
            return uint32_t(std::floor(
                       std::log2(std::max(extent.width, extent.height)))) +
                   1;
        }
        /**
         * @brief 从缓冲区拷贝到图像并执行 blit 生成 mipmap
         * @param buffer_copyFrom 源缓冲区
         * @param image_copyTo 拷贝目标图像
         * @param image_blitTo blit 目标图像
         * @param imageExtent 图像尺寸
         * @param mipLevelCount mip 级数
         * @param layerCount 数组层数
         * @param minFilter blit 滤波器（待实现）
         */
        static void CopyBlitAndGenerateMipmap2d(
            VkBuffer buffer_copyFrom, VkImage image_copyTo,
            VkImage image_blitTo, VkExtent2D imageExtent,
            uint32_t mipLevelCount = 1, uint32_t layerCount = 1,
            VkFilter minFilter = VK_FILTER_LINEAR) {
            static constexpr imageOperation::imageMemoryBarrierParameterPack
                imbs[2] = { { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                            { VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL } };
            bool generateMipmap = mipLevelCount > 1;
            bool blitMipLevel0 = image_copyTo != image_blitTo;
            auto &commandBuffer = graphicsBase::Plus().CommandBuffer_Transfer();
            commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

            VkBufferImageCopy region = {
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                                      layerCount },
                .imageExtent = { imageExtent.width, imageExtent.height, 1 }
            };
            imageOperation::CmdCopyBufferToImage(
                commandBuffer, buffer_copyFrom, image_copyTo, region,
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                  VK_IMAGE_LAYOUT_UNDEFINED },
                imbs[generateMipmap || blitMipLevel0]);

            if (blitMipLevel0) {
                VkImageBlit region = {
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layerCount },
                    { {},
                      { int32_t(imageExtent.width), int32_t(imageExtent.height),
                        1 } },
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layerCount },
                    { {},
                      { int32_t(imageExtent.width), int32_t(imageExtent.height),
                        1 } }
                };
                imageOperation::CmdBlitImage(
                    commandBuffer, image_copyTo, image_blitTo, region,
                    { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                      VK_IMAGE_LAYOUT_UNDEFINED },
                    imbs[generateMipmap], minFilter);
            }

            if (generateMipmap)
                imageOperation::CmdGenerateMipmap2d(
                    commandBuffer, image_blitTo, imageExtent, mipLevelCount,
                    layerCount,
                    { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                    minFilter);

            commandBuffer.End();
            graphicsBase::Plus().ExecuteCommandBuffer_Graphics(commandBuffer);
        }

        /**
         * @brief 对已初始化的图像执行 blit 生成 mipmap
         * @param image_preinitialized 已包含 base level 数据的图像
         * @param image_final 最终目标图像
         * @param imageExtent 图像尺寸
         * @param mipLevelCount mip 级数
         * @param layerCount 数组层数
         * @param minFilter blit 滤波器（待实现）
         */
        static void BlitAndGenerateMipmap2d(
            VkImage image_preinitialized, VkImage image_final,
            VkExtent2D imageExtent, uint32_t mipLevelCount = 1,
            uint32_t layerCount = 1, VkFilter minFilter = VK_FILTER_LINEAR) {
            static constexpr imageOperation::imageMemoryBarrierParameterPack
                imbs[2] = { { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                            { VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL } };
            bool generateMipmap = mipLevelCount > 1;
            bool blitMipLevel0 = image_preinitialized != image_final;
            if (generateMipmap || blitMipLevel0) {
                auto &commandBuffer =
                    graphicsBase::Plus().CommandBuffer_Transfer();
                commandBuffer.Begin(
                    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

                if (blitMipLevel0) {
                    VkImageMemoryBarrier imageMemoryBarrier = {
                        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                        nullptr,
                        0,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_PREINITIALIZED,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED,
                        VK_QUEUE_FAMILY_IGNORED,
                        image_preinitialized,
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount }
                    };
                    vkCmdPipelineBarrier(
                        commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                        nullptr, 1, &imageMemoryBarrier);
                    VkImageBlit region = {
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layerCount },
                        { {},
                          { int32_t(imageExtent.width),
                            int32_t(imageExtent.height), 1 } },
                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layerCount },
                        { {},
                          { int32_t(imageExtent.width),
                            int32_t(imageExtent.height), 1 } }
                    };
                    imageOperation::CmdBlitImage(
                        commandBuffer, image_preinitialized, image_final,
                        region,
                        { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                          VK_IMAGE_LAYOUT_UNDEFINED },
                        imbs[generateMipmap], minFilter);
                }

                if (generateMipmap)
                    imageOperation::CmdGenerateMipmap2d(
                        commandBuffer, image_final, imageExtent, mipLevelCount,
                        layerCount,
                        { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_SHADER_READ_BIT,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                        minFilter);

                commandBuffer.End();
                graphicsBase::Plus().ExecuteCommandBuffer_Graphics(
                    commandBuffer);
            }
        }

        static VkSamplerCreateInfo SamplerCreateInfo() {
            return { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                     .magFilter = VK_FILTER_LINEAR,
                     .minFilter = VK_FILTER_LINEAR,
                     .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                     .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                     .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                     .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                     .mipLodBias = 0.F,
                     .anisotropyEnable = VK_TRUE,
                     .maxAnisotropy = graphicsBase::Base()
                                          .PhysicalDeviceProperties()
                                          .limits.maxSamplerAnisotropy,
                     .compareEnable = VK_FALSE,
                     .compareOp = VK_COMPARE_OP_ALWAYS,
                     .minLod = 0.F,
                     .maxLod = VK_LOD_CLAMP_NONE,
                     .borderColor = {},
                     .unnormalizedCoordinates = VK_FALSE };
        }
    };

    /**
     * @class texture2d
     * @brief 2D 纹理类，从文件或内存加载图像并创建 Vulkan 2D 纹理
     *
     * 继承自 texture，实现 2D 图像的内存分配、视图创建及可选的 mipmap 生成。
     * 支持格式转换（format_initial -> format_final）及暂存缓冲区的数据拷贝。
     */
    class texture2d : public texture {
    protected:
        VkExtent2D extent {}; ///< 2D 纹理的宽高

        /**
         * @brief 内部创建逻辑：分配图像内存、创建视图并传输数据
         * @param format_initial 源数据格式（与 stb 加载结果匹配）
         * @param format_final 目标 Vulkan 格式
         * @param generateMipmap 是否生成 mipmap
         */
        void Create_Internal(VkFormat format_initial, VkFormat format_final,
                             bool generateMipmap) {
            uint32_t mipLevelCount =
                generateMipmap ? CalculateMipLevelCount(extent) : 1;
            // 创建图像并分配内存
            CreateImageMemory(VK_IMAGE_TYPE_2D, format_final,
                              { extent.width, extent.height, 1 }, mipLevelCount,
                              1);
            // 创建图像视图
            CreateImageView(VK_IMAGE_VIEW_TYPE_2D, format_final, mipLevelCount,
                            1);
            // Blit数据到图像，并生成 mipmap
            if (format_initial == format_final) {
                CopyBlitAndGenerateMipmap2d(
                    stagingBuffer::Buffer_MainThread(), imageMemory.Image(),
                    imageMemory.Image(), extent, mipLevelCount, 1);
            } else {
                if (VkImage image_conversion =
                        stagingBuffer::AliasedImage2d_MainThread(format_initial,
                                                                 extent)) {
                    // 若需要格式转换，但是能为暂存缓冲区创建混叠图像，则直接
                    // blit
                    BlitAndGenerateMipmap2d(image_conversion,
                                            imageMemory.Image(), extent,
                                            mipLevelCount, 1);
                } else {
                    // 否则，创建新的暂存图像用于中转
                    VkImageCreateInfo imageCreateInfo = {
                        .imageType = VK_IMAGE_TYPE_2D,
                        .format = format_initial,
                        .extent = { extent.width, extent.height, 1 },
                        .mipLevels = 1,
                        .arrayLayers = 1,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    };
                    vulkan::imageMemory imageMemory_conversion(
                        imageCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                    // 从暂存缓冲区拷贝到图像，然后再 blit
                    CopyBlitAndGenerateMipmap2d(
                        stagingBuffer::Buffer_MainThread(),
                        imageMemory_conversion.Image(), imageMemory.Image(),
                        extent, mipLevelCount, 1);
                }
            }
        }

    public:
        texture2d() = default;

        /**
         * @brief 从文件路径创建 2D 纹理
         * @param filepath 图像文件路径
         * @param format_initial 源格式（需与文件格式兼容）
         * @param format_final 目标 Vulkan 格式
         * @param generateMipmap 是否生成 mipmap，默认 true
         */
        texture2d(const char *filepath, VkFormat format_initial,
                  VkFormat format_final, bool generateMipmap = true) {
            Create(filepath, format_initial, format_final, generateMipmap);
        }

        /**
         * @brief 从内存数据创建 2D 纹理
         * @param pImageData 图像像素数据指针
         * @param extent 图像宽高
         * @param format_initial 源格式
         * @param format_final 目标 Vulkan 格式
         * @param generateMipmap 是否生成 mipmap，默认 true
         */
        texture2d(const uint8_t *pImageData, VkExtent2D extent,
                  VkFormat format_initial, VkFormat format_final,
                  bool generateMipmap = true) {
            Create(pImageData, extent, format_initial, format_final,
                   generateMipmap);
        }

        /** @brief 获取纹理尺寸（宽高） */
        VkExtent2D Extent() const { return extent; }

        /** @brief 获取纹理宽度 */
        uint32_t Width() const { return extent.width; }

        /** @brief 获取纹理高度 */
        uint32_t Height() const { return extent.height; }

        /**
         * @brief 从文件路径加载并创建 2D 纹理
         * @param filepath 图像文件路径
         * @param format_initial 源格式
         * @param format_final 目标 Vulkan 格式
         * @param generateMipmap 是否生成 mipmap，默认 true
         */
        void Create(const char *filepath, VkFormat format_initial,
                    VkFormat format_final, bool generateMipmap = true) {
            VkExtent2D extent;

            formatInfo formatInfo = FormatInfo(
                format_initial); // 根据指定的 format_initial 取得格式信息

            std::unique_ptr<uint8_t[]> pImageData =
                LoadFile(filepath, extent, formatInfo);

            if (pImageData) {
                Create(pImageData.get(), extent, format_initial, format_final,
                       generateMipmap);
            }
        }

        /**
         * @brief 从内存数据加载并创建 2D 纹理
         * @param pImageData 图像像素数据指针
         * @param extent 图像宽高
         * @param format_initial 源格式
         * @param format_final 目标 Vulkan 格式
         * @param generateMipmap 是否生成 mipmap，默认 true
         */
        void Create(const uint8_t *pImageData, VkExtent2D extent,
                    VkFormat format_initial, VkFormat format_final,
                    bool generateMipmap = true) {
            this->extent = extent;

            size_t imageDataSize =
                size_t(FormatInfo(format_initial).sizePerPixel) * extent.width *
                extent.height;

            stagingBuffer::BufferData_MainThread(
                pImageData, imageDataSize); // 拷贝数据到暂存缓冲区

            Create_Internal(format_initial, format_final, generateMipmap);
        }
    };

} // namespace vulkan
