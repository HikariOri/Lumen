/**
 * @file ibl_bake.cpp
 * @brief GPU 离屏烘焙 IBL（Irradiance 立方体、Prefilter 立方体 mipmap、BRDF 2D LUT）
 */

#include "ibl_bake.hpp"

#include "core/logger.hpp"
#include "core/path.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pipeline.hpp"
#include "render/resource/buffer.hpp"
#include "render/resource/cubemap_file_loader.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/sampler.hpp"
#include "render/shader.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace pbr {
namespace {

constexpr uint32_t kEnvFace { 512 };
constexpr uint32_t kIrradianceFace { 32 };
constexpr uint32_t kPrefilterFace { 128 };
constexpr uint32_t kBrdfLutSize { 512 };
constexpr VkFormat kIblFormat { VK_FORMAT_R32G32B32A32_SFLOAT };

// 单位立方体 36 顶点（位置），与 LearnOpenGL 天空盒一致
constexpr std::array<float, 36U * 3U> kCubePositions { {
    -1.0F, 1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  -1.0F, -1.0F,
    1.0F,  -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, 1.0F,  -1.0F,

    -1.0F, -1.0F, 1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  -1.0F,
    -1.0F, 1.0F,  -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, 1.0F,

    1.0F,  -1.0F, -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F, -1.0F,

    -1.0F, -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F, -1.0F, 1.0F,

    -1.0F, 1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,  1.0F,  1.0F,
    1.0F,  1.0F,  1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,  -1.0F,

    -1.0F, -1.0F, -1.0F, -1.0F, -1.0F, 1.0F,  1.0F,  -1.0F, 1.0F,
    1.0F,  -1.0F, 1.0F,  1.0F,  -1.0F, -1.0F, -1.0F, -1.0F, -1.0F,
} };

[[nodiscard]] glm::mat4 capture_projection_vk() {
    glm::mat4 p = glm::perspective(glm::radians(90.0F), 1.0F, 0.1F, 10.0F);
    p[1][1] *= -1.0F;
    return p;
}

[[nodiscard]] std::array<glm::mat4, 6> capture_views() {
    return {
        glm::lookAt(glm::vec3(0.0F), glm::vec3(1.0F, 0.0F, 0.0F),
                    glm::vec3(0.0F, -1.0F, 0.0F)),
        glm::lookAt(glm::vec3(0.0F), glm::vec3(-1.0F, 0.0F, 0.0F),
                    glm::vec3(0.0F, -1.0F, 0.0F)),
        glm::lookAt(glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F),
                    glm::vec3(0.0F, 0.0F, 1.0F)),
        glm::lookAt(glm::vec3(0.0F), glm::vec3(0.0F, -1.0F, 0.0F),
                    glm::vec3(0.0F, 0.0F, -1.0F)),
        glm::lookAt(glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 1.0F),
                    glm::vec3(0.0F, -1.0F, 0.0F)),
        glm::lookAt(glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, -1.0F),
                    glm::vec3(0.0F, -1.0F, 0.0F)),
    };
}

[[nodiscard]] VkImageView create_face_mip_view(VkDevice dev, VkImage img,
                                               VkFormat fmt, uint32_t face,
                                               uint32_t mip) {
    VkImageViewCreateInfo vi { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = mip;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.baseArrayLayer = face;
    vi.subresourceRange.layerCount = 1;
    VkImageView v { VK_NULL_HANDLE };
    if (vkCreateImageView(dev, &vi, nullptr, &v) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return v;
}

void destroy_view(VkDevice dev, VkImageView v) {
    if (v != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, v, nullptr);
    }
}

void barrier_undefined_to_color_attachment(
    VkCommandBuffer cmd, VkImage img, uint32_t base_mip,
    uint32_t mip_count, uint32_t base_layer, uint32_t layer_count) {
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = base_mip;
    b.subresourceRange.levelCount = mip_count;
    b.subresourceRange.baseArrayLayer = base_layer;
    b.subresourceRange.layerCount = layer_count;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
        nullptr, 1, &b);
}

void barrier_color_attachment_to_shader_read(
    VkCommandBuffer cmd, VkImage img, uint32_t base_mip,
    uint32_t mip_count, uint32_t base_layer, uint32_t layer_count) {
    VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = base_mip;
    b.subresourceRange.levelCount = mip_count;
    b.subresourceRange.baseArrayLayer = base_layer;
    b.subresourceRange.layerCount = layer_count;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
        &b);
}

} // namespace

bool bake_ibl(lumen::render::Context &ctx, lumen::render::CommandPool &cmdPool,
              VkQueue queue, const char *hdr_path, IblTextures &out,
              std::string &err) {
    VkDevice dev = ctx.device();
    const auto proj = capture_projection_vk();
    const auto views = capture_views();

    lumen::render::SamplerConfig linearClamp {};
    linearClamp.minFilter = VK_FILTER_LINEAR;
    linearClamp.magFilter = VK_FILTER_LINEAR;
    linearClamp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linearClamp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linearClamp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    linearClamp.maxAnisotropy = 1.0F;
    linearClamp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    linearClamp.maxLod = 32.0F;

    std::string load_err;
    if (!lumen::render::load_cubemap_from_hdr_equirectangular_file(
            ctx, hdr_path, queue, cmdPool, linearClamp, out.environment,
            kEnvFace, &load_err)) {
        err = "环境 HDR 加载失败: " + load_err;
        return false;
    }

    lumen::render::ImageCreateInfo irrInfo {};
    irrInfo.width = kIrradianceFace;
    irrInfo.height = kIrradianceFace;
    irrInfo.mipLevels = 1;
    irrInfo.arrayLayers = 6;
    irrInfo.format = kIblFormat;
    irrInfo.type = lumen::render::ImageType::TexCube;
    irrInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
    irrInfo.generateMipmaps = false;
    if (!out.irradiance.create(ctx, irrInfo, linearClamp)) {
        err = "创建 Irradiance 目标失败";
        return false;
    }

    const uint32_t prefilter_mips =
        static_cast<uint32_t>(std::floor(
            std::log2(static_cast<float>(std::max(kPrefilterFace, 1U))))) +
        1U;
    lumen::render::ImageCreateInfo preInfo {};
    preInfo.width = kPrefilterFace;
    preInfo.height = kPrefilterFace;
    preInfo.mipLevels = prefilter_mips;
    preInfo.arrayLayers = 6;
    preInfo.format = kIblFormat;
    preInfo.type = lumen::render::ImageType::TexCube;
    preInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
    preInfo.generateMipmaps = false;
    if (!out.prefilter.create(ctx, preInfo, linearClamp)) {
        err = "创建 Prefilter 目标失败";
        return false;
    }

    lumen::render::ImageCreateInfo lutInfo {};
    lutInfo.width = kBrdfLutSize;
    lutInfo.height = kBrdfLutSize;
    lutInfo.mipLevels = 1;
    lutInfo.arrayLayers = 1;
    lutInfo.format = kIblFormat;
    lutInfo.type = lumen::render::ImageType::Tex2D;
    lutInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
    lutInfo.generateMipmaps = false;
    if (!out.brdf_lut.create(ctx, lutInfo, linearClamp)) {
        err = "创建 BRDF LUT 失败";
        return false;
    }

    lumen::render::RenderPassConfig rpc {};
    rpc.useDepth = false;
    rpc.colorAttachment.format = kIblFormat;
    rpc.colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    rpc.colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rpc.colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    lumen::render::RenderPass rp;
    if (!rp.create(dev, rpc)) {
        err = "IBL RenderPass 创建失败";
        return false;
    }

    const std::string cube_vs =
        lumen::core::get_resource_path("shaders/ibl_cube.vert.spv");
    const std::string irr_fs =
        lumen::core::get_resource_path("shaders/ibl_irradiance.frag.spv");
    const std::string pre_vs =
        lumen::core::get_resource_path("shaders/ibl_prefilter.vert.spv");
    const std::string pre_fs =
        lumen::core::get_resource_path("shaders/ibl_prefilter.frag.spv");
    const std::string brdf_vs =
        lumen::core::get_resource_path("shaders/ibl_brdf.vert.spv");
    const std::string brdf_fs =
        lumen::core::get_resource_path("shaders/ibl_brdf.frag.spv");

    lumen::render::ShaderModule sm_cube_vs, sm_irr_fs, sm_pre_vs, sm_pre_fs,
        sm_brdf_vs, sm_brdf_fs;
    if (!sm_cube_vs.create_from_file(dev, cube_vs.c_str()) ||
        !sm_irr_fs.create_from_file(dev, irr_fs.c_str()) ||
        !sm_pre_vs.create_from_file(dev, pre_vs.c_str()) ||
        !sm_pre_fs.create_from_file(dev, pre_fs.c_str()) ||
        !sm_brdf_vs.create_from_file(dev, brdf_vs.c_str()) ||
        !sm_brdf_fs.create_from_file(dev, brdf_fs.c_str())) {
        err = "IBL 着色器加载失败";
        return false;
    }

    lumen::render::DescriptorSetLayout dsl_env;
    std::vector<lumen::render::DescriptorBinding> bind_env = {
        { .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .count = 1,
          .stages = VK_SHADER_STAGE_FRAGMENT_BIT },
    };
    if (!dsl_env.create(ctx, bind_env)) {
        err = "DescriptorSetLayout 创建失败";
        return false;
    }

    lumen::render::DescriptorPool dpool;
    if (!dpool.create(ctx,
                      { { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          .count = 4 } },
                      4)) {
        err = "DescriptorPool 创建失败";
        return false;
    }

    VkDescriptorSet ds_irr { VK_NULL_HANDLE };
    VkDescriptorSet ds_pre { VK_NULL_HANDLE };
    if (!dpool.allocate(dev, dsl_env.handle(), ds_irr) ||
        !dpool.allocate(dev, dsl_env.handle(), ds_pre)) {
        err = "DescriptorSet 分配失败";
        return false;
    }

    lumen::render::write_descriptor_set(
        dev, ds_irr, {},
        { { .binding = 0,
            .imageView = out.environment.view(),
            .sampler = out.environment.sampler(),
            .imageLayout = out.environment.descriptor_layout() } });
    lumen::render::write_descriptor_set(
        dev, ds_pre, {},
        { { .binding = 0,
            .imageView = out.environment.view(),
            .sampler = out.environment.sampler(),
            .imageLayout = out.environment.descriptor_layout() } });

    VkPushConstantRange pc_irr {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(glm::mat4),
    };
    lumen::render::PipelineLayout pl_irr;
    if (!pl_irr.create(ctx, { dsl_env.handle() }, { pc_irr })) {
        err = "PipelineLayout (irr) 失败";
        return false;
    }

    VkPushConstantRange pc_pre {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(glm::mat4) + sizeof(float) * 4,
    };
    lumen::render::PipelineLayout pl_pre;
    if (!pl_pre.create(ctx, { dsl_env.handle() }, { pc_pre })) {
        err = "PipelineLayout (pre) 失败";
        return false;
    }

    lumen::render::PipelineLayout pl_brdf;
    if (!pl_brdf.create(ctx, {}, {})) {
        err = "PipelineLayout (brdf) 失败";
        return false;
    }

    lumen::render::GraphicsPipelineConfig cfg_irr {};
    cfg_irr.shaderStages.push_back({ sm_cube_vs.handle(),
                                     VK_SHADER_STAGE_VERTEX_BIT, "main" });
    cfg_irr.shaderStages.push_back({ sm_irr_fs.handle(),
                                     VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    cfg_irr.vertexBindings.push_back(
        { .binding = 0,
          .stride = sizeof(float) * 3,
          .inputRate = lumen::render::VertexInputRate::PerVertex });
    cfg_irr.vertexAttributes.push_back(
        { .location = 0,
          .binding = 0,
          .format = lumen::render::VertexAttributeFormat::F32Vec3,
          .offset = 0 });
    cfg_irr.depthTest = false;
    cfg_irr.depthWrite = false;
    // 烘焙使用负高度视口（与 CPU 环境 cubemap 面内上下一致）时，视口空间绕序会反转；
    // 若仍用 FRONT_BIT（原“内看天空盒”设定），可见面会被全部剔除 → 整面黑。改用 BACK_BIT。
    cfg_irr.cullMode = VK_CULL_MODE_BACK_BIT;
    cfg_irr.frontFace = VK_FRONT_FACE_CLOCKWISE;

    lumen::render::GraphicsPipeline pipe_irr;
    if (!pipe_irr.create(ctx, pl_irr, rp, 0, cfg_irr)) {
        err = "Irradiance Pipeline 失败";
        return false;
    }

    lumen::render::GraphicsPipelineConfig cfg_pre = cfg_irr;
    cfg_pre.shaderStages.clear();
    cfg_pre.shaderStages.push_back({ sm_pre_vs.handle(),
                                     VK_SHADER_STAGE_VERTEX_BIT, "main" });
    cfg_pre.shaderStages.push_back({ sm_pre_fs.handle(),
                                     VK_SHADER_STAGE_FRAGMENT_BIT, "main" });

    lumen::render::GraphicsPipeline pipe_pre;
    if (!pipe_pre.create(ctx, pl_pre, rp, 0, cfg_pre)) {
        err = "Prefilter Pipeline 失败";
        return false;
    }

    lumen::render::GraphicsPipelineConfig cfg_brdf {};
    cfg_brdf.shaderStages.push_back({ sm_brdf_vs.handle(),
                                      VK_SHADER_STAGE_VERTEX_BIT, "main" });
    cfg_brdf.shaderStages.push_back({ sm_brdf_fs.handle(),
                                      VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    cfg_brdf.depthTest = false;
    cfg_brdf.depthWrite = false;
    cfg_brdf.cullMode = VK_CULL_MODE_NONE;

    lumen::render::GraphicsPipeline pipe_brdf;
    if (!pipe_brdf.create(ctx, pl_brdf, rp, 0, cfg_brdf)) {
        err = "BRDF Pipeline 失败";
        return false;
    }

    lumen::render::VertexBuffer vbuf;
    if (!vbuf.create_device_local_and_upload(ctx, queue, cmdPool,
                                              kCubePositions.data(),
                                              sizeof(kCubePositions))) {
        err = "立方体顶点上传失败";
        return false;
    }

    auto buffers = cmdPool.allocate(1);
    if (buffers.empty() || !buffers[0].is_valid()) {
        err = "CommandBuffer 分配失败";
        return false;
    }
    lumen::render::CommandBuffer cmd = std::move(buffers[0]);

    if (!cmd.begin()) {
        err = "CommandBuffer::begin 失败";
        return false;
    }

    std::vector<VkFramebuffer> transient_fbs;
    std::vector<VkImageView> transient_views;
    transient_fbs.reserve(6U + 6U * prefilter_mips + 1U);
    transient_views.reserve(6U + 6U * prefilter_mips + 1U);

    auto destroy_transient_attachments = [&]() {
        for (VkFramebuffer f : transient_fbs) {
            if (f != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(dev, f, nullptr);
            }
        }
        transient_fbs.clear();
        for (VkImageView v : transient_views) {
            destroy_view(dev, v);
        }
        transient_views.clear();
    };

    VkCommandBuffer cb = cmd.handle();

    barrier_undefined_to_color_attachment(cb, out.irradiance.image(), 0, 1, 0,
                                          6);

    VkClearValue clear {};
    clear.color = { { 0.0F, 0.0F, 0.0F, 1.0F } };

    for (uint32_t face = 0; face < 6; ++face) {
        VkImageView att =
            create_face_mip_view(dev, out.irradiance.image(), kIblFormat, face,
                                 0);
        if (att == VK_NULL_HANDLE) {
            err = "Irradiance ImageView 失败";
            return false;
        }
        VkFramebuffer fb { VK_NULL_HANDLE };
        VkFramebufferCreateInfo fbi { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbi.renderPass = rp.handle();
        fbi.attachmentCount = 1;
        fbi.pAttachments = &att;
        fbi.width = kIrradianceFace;
        fbi.height = kIrradianceFace;
        fbi.layers = 1;
        if (vkCreateFramebuffer(dev, &fbi, nullptr, &fb) != VK_SUCCESS) {
            destroy_view(dev, att);
            err = "Framebuffer 创建失败";
            return false;
        }

        VkRenderPassBeginInfo rpb { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpb.renderPass = rp.handle();
        rpb.framebuffer = fb;
        rpb.renderArea.offset = { 0, 0 };
        rpb.renderArea.extent = { kIrradianceFace, kIrradianceFace };
        rpb.clearValueCount = 1;
        rpb.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipe_irr.handle());
        VkDescriptorSet ds = ds_irr;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pl_irr.handle(), 0, 1, &ds, 0, nullptr);
        const glm::mat4 vp = proj * views[face];
        vkCmdPushConstants(cb, pl_irr.handle(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::mat4), glm::value_ptr(vp));
        const float fs = static_cast<float>(kIrradianceFace);
        // 负高度视口：与 CPU HDR→Cubemap（cubemap_file_loader 面内 v 递增方向）对齐，
        // 否则 Irradiance / Prefilter 相对 Environment 会上下颠倒。
        VkViewport vp_irr { 0.0F, fs, fs, -fs, 0.0F, 1.0F };
        vkCmdSetViewport(cb, 0, 1, &vp_irr);
        VkRect2D scr { { 0, 0 }, { kIrradianceFace, kIrradianceFace } };
        vkCmdSetScissor(cb, 0, 1, &scr);
        VkDeviceSize off { 0 };
        VkBuffer vb = vbuf.handle();
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
        vkCmdDraw(cb, 36, 1, 0, 0);
        vkCmdEndRenderPass(cb);

        transient_fbs.push_back(fb);
        transient_views.push_back(att);
    }

    barrier_color_attachment_to_shader_read(cb, out.irradiance.image(), 0, 1, 0,
                                            6);

    barrier_undefined_to_color_attachment(cb, out.prefilter.image(), 0,
                                            prefilter_mips, 0, 6);

    for (uint32_t mip = 0; mip < prefilter_mips; ++mip) {
        const uint32_t dim = std::max(1U, kPrefilterFace >> mip);
        const float roughness =
            (prefilter_mips <= 1)
                ? 0.0F
                : static_cast<float>(mip) /
                      static_cast<float>(prefilter_mips - 1);
        for (uint32_t face = 0; face < 6; ++face) {
            VkImageView att = create_face_mip_view(
                dev, out.prefilter.image(), kIblFormat, face, mip);
            if (att == VK_NULL_HANDLE) {
                err = "Prefilter ImageView 失败";
                return false;
            }
            VkFramebuffer fb { VK_NULL_HANDLE };
            VkFramebufferCreateInfo fbi { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fbi.renderPass = rp.handle();
            fbi.attachmentCount = 1;
            fbi.pAttachments = &att;
            fbi.width = dim;
            fbi.height = dim;
            fbi.layers = 1;
            if (vkCreateFramebuffer(dev, &fbi, nullptr, &fb) != VK_SUCCESS) {
                destroy_view(dev, att);
                err = "Prefilter Framebuffer 失败";
                return false;
            }

            VkRenderPassBeginInfo rpb { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            rpb.renderPass = rp.handle();
            rpb.framebuffer = fb;
            rpb.renderArea.offset = { 0, 0 };
            rpb.renderArea.extent = { dim, dim };
            rpb.clearValueCount = 1;
            rpb.pClearValues = &clear;

            vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipe_pre.handle());
            VkDescriptorSet dsp = ds_pre;
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pl_pre.handle(), 0, 1, &dsp, 0, nullptr);
            const glm::mat4 vp = proj * views[face];
            struct PushP {
                glm::mat4 projView;
                glm::vec4 rough_pad;
            } push {};
            push.projView = vp;
            push.rough_pad = glm::vec4(roughness, 0.0F, 0.0F, 0.0F);
            vkCmdPushConstants(cb, pl_pre.handle(),
                               VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushP), &push);
            VkDeviceSize off { 0 };
            VkBuffer vb = vbuf.handle();
            vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
            const float fd = static_cast<float>(dim);
            VkViewport vp_pre { 0.0F, fd, fd, -fd, 0.0F, 1.0F };
            vkCmdSetViewport(cb, 0, 1, &vp_pre);
            VkRect2D sc { { 0, 0 }, { dim, dim } };
            vkCmdSetScissor(cb, 0, 1, &sc);
            vkCmdDraw(cb, 36, 1, 0, 0);
            vkCmdEndRenderPass(cb);

            transient_fbs.push_back(fb);
            transient_views.push_back(att);
        }
    }

    barrier_color_attachment_to_shader_read(cb, out.prefilter.image(), 0,
                                            prefilter_mips, 0, 6);

    barrier_undefined_to_color_attachment(cb, out.brdf_lut.image(), 0, 1, 0, 1);

    {
        VkImageView att = VK_NULL_HANDLE;
        VkImageViewCreateInfo vi { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = out.brdf_lut.image();
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = kIblFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(dev, &vi, nullptr, &att) != VK_SUCCESS) {
            err = "BRDF ImageView 失败";
            return false;
        }
        VkFramebuffer fb { VK_NULL_HANDLE };
        VkFramebufferCreateInfo fbi { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbi.renderPass = rp.handle();
        fbi.attachmentCount = 1;
        fbi.pAttachments = &att;
        fbi.width = kBrdfLutSize;
        fbi.height = kBrdfLutSize;
        fbi.layers = 1;
        if (vkCreateFramebuffer(dev, &fbi, nullptr, &fb) != VK_SUCCESS) {
            destroy_view(dev, att);
            err = "BRDF Framebuffer 失败";
            return false;
        }

        VkRenderPassBeginInfo rpb { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rpb.renderPass = rp.handle();
        rpb.framebuffer = fb;
        rpb.renderArea.offset = { 0, 0 };
        rpb.renderArea.extent = { kBrdfLutSize, kBrdfLutSize };
        rpb.clearValueCount = 1;
        rpb.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipe_brdf.handle());
        VkViewport vp_brdf { 0.0F,
                             0.0F,
                             static_cast<float>(kBrdfLutSize),
                             static_cast<float>(kBrdfLutSize),
                             0.0F,
                             1.0F };
        vkCmdSetViewport(cb, 0, 1, &vp_brdf);
        VkRect2D sc { { 0, 0 },
                      { kBrdfLutSize, kBrdfLutSize } };
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);

        transient_fbs.push_back(fb);
        transient_views.push_back(att);
    }

    barrier_color_attachment_to_shader_read(cb, out.brdf_lut.image(), 0, 1, 0,
                                            1);

    if (!cmd.end()) {
        destroy_transient_attachments();
        err = "CommandBuffer::end 失败";
        return false;
    }

    VkSubmitInfo sub { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    sub.commandBufferCount = 1;
    VkCommandBuffer cbc = cmd.handle();
    sub.pCommandBuffers = &cbc;

    VkFence fence { VK_NULL_HANDLE };
    VkFenceCreateInfo fi { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(dev, &fi, nullptr, &fence) != VK_SUCCESS) {
        destroy_transient_attachments();
        err = "Fence 创建失败";
        return false;
    }
    if (vkQueueSubmit(queue, 1, &sub, fence) != VK_SUCCESS) {
        vkDestroyFence(dev, fence, nullptr);
        destroy_transient_attachments();
        err = "IBL vkQueueSubmit 失败";
        return false;
    }
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fence, nullptr);

    destroy_transient_attachments();

    std::vector<lumen::render::CommandBuffer> free_list;
    free_list.push_back(std::move(cmd));
    cmdPool.free(free_list);

    LUMEN_APP_LOG_INFO("IBL 烘焙完成（Irradiance / Prefilter / BRDF LUT）");
    return true;
}

} // namespace pbr
