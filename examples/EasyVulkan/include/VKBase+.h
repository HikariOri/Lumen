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

} // namespace vulkan
