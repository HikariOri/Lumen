#pragma once

namespace renderer {

using TextureHandle = uint32_t;

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
};

struct TextureResource {
    TextureDesc desc;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 lastStage = 0;
    VkAccessFlags2 lastAccess = 0;
};

enum class Access { Read, Write };

struct TextureUsage {
    TextureHandle handle;
    Access access;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 accessMask;
    VkImageLayout targetLayout;
};

struct Pass {
    std::string name;
    std::vector<TextureUsage> usages;
    std::function<void(VkCommandBuffer)> execute;
};

class RenderGraph {
public:
    RenderGraph(VkDevice dev, VmaAllocator alloc)
        : device(dev), allocator(alloc) {}
    ~RenderGraph();

    TextureHandle createTexture(const TextureDesc &desc);
    TextureHandle importExternal(const TextureDesc &desc, VkImage img,
                                 VkImageView view);
    void setExternalImage(TextureHandle handle, VkImage img, VkImageView view);
    TextureResource &getTexture(TextureHandle h) { return textures[h]; }

    void addPass(const std::string &name,
                 std::function<void(class PassBuilder &)> setup,
                 std::function<void(VkCommandBuffer)> exec);

    void compile();
    void execute(VkCommandBuffer cmd);

private:
    void createResources();
    void buildBarriers();

    VkDevice device;
    VmaAllocator allocator;
    std::vector<TextureResource> textures;
    std::vector<Pass> passes;
    std::vector<std::vector<VkImageMemoryBarrier2>> barriersPerPass;
};

class PassBuilder {
public:
    PassBuilder(RenderGraph &g, Pass &p) : rg(g), pass(p) {}
    void writeColor(TextureHandle h);
    void readSampled(TextureHandle h);

private:
    RenderGraph &rg;
    Pass &pass;
};

} // namespace renderer
