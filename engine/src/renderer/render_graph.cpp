#include "renderer/render_graph.hpp"
#include <vulkan/vulkan_core.h>

namespace renderer {

RenderGraph::~RenderGraph() {
    for (auto &tex : textures) {
        // importExternal / setExternalImage 的 ImageView 由调用方（如
        // swapchain）拥有，不得在此销毁。 仅 createTexture +
        // compile/createResources 创建的纹理带 VMA allocation。
        if (!tex.allocation) {
            continue;
        }
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex.view, nullptr);
        }
        vmaDestroyImage(allocator, tex.image, tex.allocation);
    }
}

TextureHandle RenderGraph::createTexture(const TextureDesc &desc) {
    TextureResource r;
    r.desc = desc;
    textures.push_back(r);
    return (uint32_t)textures.size() - 1;
}

TextureHandle RenderGraph::importExternal(const TextureDesc &desc, VkImage img,
                                          VkImageView view) {
    TextureResource r;
    r.desc = desc;
    r.image = img;
    r.view = view;
    textures.push_back(r);
    return (uint32_t)textures.size() - 1;
}

void RenderGraph::setExternalImage(TextureHandle handle, VkImage img,
                                   VkImageView view) {
    textures[handle].image = img;
    textures[handle].view = view;
}

void PassBuilder::writeColor(TextureHandle h) {
    pass.usages.push_back({ h, Access::Write,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
}

void PassBuilder::readSampled(TextureHandle h) {
    pass.usages.push_back({ h, Access::Read,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
}

void RenderGraph::addPass(const std::string &name,
                          std::function<void(PassBuilder &)> setup,
                          std::function<void(VkCommandBuffer)> exec) {
    Pass pass;
    pass.name = name;
    pass.execute = exec;
    PassBuilder b(*this, pass);
    setup(b);
    passes.push_back(pass);
}

void RenderGraph::createResources() {
    for (auto &tex : textures) {
        if (tex.image)
            continue;

        VkImageCreateInfo ii { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = tex.desc.format,
                               .extent = { tex.desc.width, tex.desc.height, 1 },
                               .mipLevels = 1,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = tex.desc.usage };

        VmaAllocationCreateInfo ai { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
        vmaCreateImage(allocator, &ii, &ai, &tex.image, &tex.allocation,
                       nullptr);

        VkImageViewCreateInfo vi {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = tex.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = tex.desc.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        vkCreateImageView(device, &vi, nullptr, &tex.view);
    }
}

void RenderGraph::buildBarriers() {
    barriersPerPass.resize(passes.size());
    for (uint32_t i = 0; i < passes.size(); ++i) {
        auto &pass = passes[i];
        auto &out = barriersPerPass[i];
        for (auto &u : pass.usages) {
            auto &tex = textures[u.handle];
            bool need = tex.currentLayout != u.targetLayout ||
                        tex.lastAccess != u.accessMask ||
                        tex.lastStage != u.stage;
            if (!need)
                continue;

            VkImageMemoryBarrier2 b {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = tex.lastStage,
                .srcAccessMask = tex.lastAccess,
                .dstStageMask = u.stage,
                .dstAccessMask = u.accessMask,
                .oldLayout = tex.currentLayout,
                .newLayout = u.targetLayout,
                .image = tex.image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };
            out.push_back(b);
            tex.currentLayout = u.targetLayout;
            tex.lastStage = u.stage;
            tex.lastAccess = u.accessMask;
        }
    }
}

void RenderGraph::compile() {
    createResources();
    buildBarriers();
}

void RenderGraph::execute(VkCommandBuffer cmd) {
    for (uint32_t i = 0; i < passes.size(); ++i) {
        auto &b = barriersPerPass[i];
        if (!b.empty()) {
            VkDependencyInfo di { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                  .imageMemoryBarrierCount = (uint32_t)b.size(),
                                  .pImageMemoryBarriers = b.data() };
            vkCmdPipelineBarrier2(cmd, &di);
        }
        passes[i].execute(cmd);
    }
}

} // namespace renderer
