#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace renderer {

using TextureHandle = uint32_t;

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    /// 合并为 Subpass 链时，颜色/输入附件需包含 INPUT_ATTACHMENT。
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
};

enum class TextureType {
    Swapchain,
    RenderTarget,
    Depth
};

struct TextureResource {
    TextureDesc desc;
    TextureType type = TextureType::RenderTarget;

    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 lastStage = 0;
    VkAccessFlags2 lastAccess = 0;
};

struct PassInfo {
    std::vector<TextureHandle> reads;
    std::vector<TextureHandle> writes;
    TextureHandle depth = UINT32_MAX;
    VkExtent2D extent{1, 1};

    VkClearColorValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth = 1.0f;
    uint32_t clearStencil = 0;
    bool enableClearDepth = true;
};

class RenderGraph {
public:
    RenderGraph(VkDevice dev, VmaAllocator alloc)
        : device(dev), allocator(alloc) {}

    ~RenderGraph();

    TextureHandle createTexture(const TextureDesc &desc);
    TextureHandle importSwapchain(const TextureDesc &desc, VkImage img,
                                  VkImageView view);
    TextureHandle createDepth(const TextureDesc &desc);

    void set_swapchain_image_views(std::vector<VkImageView> views);

    /// 与 PassInfo 等价，声明式添加逻辑 Pass（可合并为 Subpass）。
    void addGraphicsPass(const std::string &name, const PassInfo &info,
                         std::function<void(VkCommandBuffer)> record_draws);

    /// 直接声明 inputs / colors / depth（与合并算法一致）。
    void add_pass(const std::string &name,
                  const std::vector<TextureHandle> &inputs,
                  const std::vector<TextureHandle> &colors,
                  TextureHandle depth,
                  std::function<void(VkCommandBuffer)> exec);

    /// false：每个逻辑 Pass 单独 VkRenderPass（默认，兼容现有示例）。
    /// true：在满足依赖与 extent 时合并为同一 RenderPass 的多个 Subpass。
    void set_subpass_merging(bool enable);

    void compile();
    void execute(VkCommandBuffer cmd, uint32_t swapchain_image_index = 0);

    TextureResource &getTexture(TextureHandle h);

    void updateSwapchainImage(TextureHandle handle, VkImage newImage,
                              VkImageView newView);
    void resetLayouts();

    VkRenderPass render_pass_named(const std::string &name) const;
    /// 合并后管线需绑定对应 Subpass；未合并时为 0。
    uint32_t subpass_index_for(const std::string &name) const;

private:
    struct UserPass {
        std::string name;
        std::vector<TextureHandle> inputs;
        std::vector<TextureHandle> colors;
        TextureHandle depth = UINT32_MAX;
        VkExtent2D extent{1, 1};
        VkClearColorValue clear_color{};
        float clear_depth = 1.0f;
        uint32_t clear_stencil = 0;
        bool enable_clear_depth = true;
        std::function<void(VkCommandBuffer)> exec;
    };

    struct CompiledSubpass {
        std::string name;
        std::function<void(VkCommandBuffer)> exec;
        uint32_t subpass_index = 0;
    };

    struct CompiledBatch {
        std::size_t first_user_pass_index = 0;

        VkRenderPass render_pass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<CompiledSubpass> subpasses;
        VkExtent2D extent{};

        std::vector<VkClearValue> clear_values;
        std::vector<VkImageMemoryBarrier2> barriers_before_batch;

        std::vector<TextureHandle> color_outputs;
        std::vector<VkImageLayout> color_final_layouts;
        bool has_depth = false;
        VkImageLayout depth_final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::vector<TextureHandle> depth_handles;
    };

    bool texture_read_later(TextureHandle h,
                            std::size_t after_user_pass_index) const;
    VkImageLayout final_layout_for_color(const TextureResource &tex,
                                         TextureHandle h,
                                         std::size_t user_pass_index) const;

    void create_all_resources();
    void merge_into_batches();
    void build_batch_multi_subpass(CompiledBatch &batch,
                                   const std::vector<UserPass> &subs,
                                   std::size_t first_user_pass_index);
    void build_batch_single_subpass(CompiledBatch &batch, const UserPass &up,
                                      std::size_t user_pass_index);
    void rebuild_barriers_for_batch(std::size_t batch_index);
    void sync_batch_output_layouts(std::size_t batch_index);
    void destroy_compiled();
    VkExtent2D extent_for_user_pass(const UserPass &up) const;

    VkDevice device;
    VmaAllocator allocator;
    bool subpass_merging_enabled_ = false;

    std::vector<VkImageView> swapchain_image_views_;
    std::vector<TextureResource> textures;
    std::vector<UserPass> user_passes_;

    std::vector<CompiledBatch> compiled_batches_;
    std::unordered_map<std::string, VkRenderPass> name_to_render_pass_;
    std::unordered_map<std::string, uint32_t> name_to_subpass_;
};

} // namespace renderer
