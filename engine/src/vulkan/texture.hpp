#pragma once

#include "vulkan/buffer.hpp"
#include "vulkan/upload_context.hpp"

namespace vulkan {

struct Texture2D {
    VkImage image { VK_NULL_HANDLE };
    VkImageView view { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VmaAllocator allocator { VK_NULL_HANDLE };
    VkSampler sampler = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    std::uint32_t width {};
    std::uint32_t height {};
    VkFormat format { VK_FORMAT_R8G8B8A8_SRGB };

    std::uint32_t mipLevels {};
    std::uint32_t array_layers {};
    VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageUsageFlags usage {};
    VkImageAspectFlags aspectMask {};

    void load_from_file(const std::string_view &path, VmaAllocator allocator,
                      VkDevice device, const UploadContext &uploadCtx);

    void destroy(VkDevice device);
};

// 全局纹理池（单例）
struct TexturePool {
    static TexturePool &instance();

    void init(VmaAllocator allocator, VkDevice device,
              const UploadContext *uploadContext);
    Texture2D *get_or_load(const std::string &path);

    void clear();

private:
    VmaAllocator allocator_ { VK_NULL_HANDLE };
    VkDevice device_ { VK_NULL_HANDLE };
    const UploadContext *uploadContext_ { nullptr };

    std::unordered_map<std::string, Texture2D> textures_;
};
} // namespace vulkan
