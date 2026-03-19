/**
 * @file image.hpp
 * @brief VkImage 封装：2D、Cube、Mipmap、View
 *
 * 提供 Image 创建、View、布局转换与 RAII 管理。
 */

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace lumen {
    namespace render {

        class Context;

        /// Image 类型
        enum class ImageType {
            Tex2D,
            TexCube,
        };

        /// Image 创建信息
        struct ImageCreateInfo {
            uint32_t width { 0 };
            uint32_t height { 0 };
            uint32_t depth { 1 };
            uint32_t mipLevels { 1 };
            uint32_t arrayLayers { 1 };
            VkFormat format { VK_FORMAT_R8G8B8A8_UNORM };
            ImageType type { ImageType::Tex2D };
            /// 用作颜色附件、深度附件、采样等
            VkImageUsageFlags usage { VK_IMAGE_USAGE_SAMPLED_BIT };
            /// 是否生成 Mipmap
            bool generateMipmaps { false };
            /// ImageView 的 aspect，深度附件用 VK_IMAGE_ASPECT_DEPTH_BIT
            VkImageAspectFlags aspectMask { VK_IMAGE_ASPECT_COLOR_BIT };
        };

        /**
         * @class Image
         * @brief Vulkan Image 封装
         *
         * RAII 管理 Image、DeviceMemory 与 ImageView。
         */
        class Image {
        public:
            Image() = default;
            Image(const Image &) = delete;
            Image(Image &&other) noexcept;
            Image &operator=(const Image &) = delete;
            Image &operator=(Image &&other) noexcept;
            ~Image();

            /**
             * @brief 创建 Image 并分配内存、创建 View
             * @param ctx 已初始化的 Context
             * @param info 创建信息
             * @return 成功返回 true
             */
            bool create(const Context &ctx, const ImageCreateInfo &info);

            /**
             * @brief 从文件加载并创建纹理 Image
             * @param ctx Context
             * @param filePath 图片路径（PNG/JPG 等）
             * @return 成功返回 true
             */
            bool create_from_file(const Context &ctx, const char *filePath);

            /**
             * @brief 创建深度附件 Image（用于 RenderPass）
             * @param ctx Context
             * @param width 宽度
             * @param height 高度
             * @return 成功返回 true
             */
            bool create_depth_attachment(const Context &ctx, uint32_t width,
                                         uint32_t height);

            /// VkImage 句柄
            [[nodiscard]] VkImage handle() const { return image_; }

            /// ImageView 句柄
            [[nodiscard]] VkImageView view() const { return imageView_; }

            /// 格式
            [[nodiscard]] VkFormat format() const { return format_; }

            /// 宽度
            [[nodiscard]] uint32_t width() const { return width_; }

            /// 高度
            [[nodiscard]] uint32_t height() const { return height_; }

            /// Mip 层数
            [[nodiscard]] uint32_t mip_levels() const { return mipLevels_; }

            /// 是否有效
            [[nodiscard]] bool is_valid() const {
                return image_ != VK_NULL_HANDLE;
            }

        private:
            void destroy_();

            VkDevice device_ { VK_NULL_HANDLE };
            VkImage image_ { VK_NULL_HANDLE };
            VkDeviceMemory memory_ { VK_NULL_HANDLE };
            VkImageView imageView_ { VK_NULL_HANDLE };
            VkFormat format_ { VK_FORMAT_UNDEFINED };
            uint32_t width_ { 0 };
            uint32_t height_ { 0 };
            uint32_t mipLevels_ { 0 };
        };

    } // namespace render
} // namespace lumen
