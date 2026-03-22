/**
 * @file main.cpp
 * @brief Demo3D：进入 3D 世界 - 透视、深度缓冲、OBJ 模型加载
 */

#include "engine.hpp"

#include "core/logger.hpp"
#include "core/obj_loader.hpp"
#include "core/path.hpp"
#include "core/time.hpp"
#include "platform/event_debug.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/pass/render_graph.hpp"
#include "render/pass/render_pass.hpp"
#include "render/pass/render_target.hpp"
#include "render/pipeline.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/image.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"
#include "render/shader.hpp"
#include "render/swapchain.hpp"
#include "ui/gizmo.hpp"
#include "ui/gpu_capabilities_panel.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/log_panel.hpp"
#include "ui/input_bridge.hpp"
#include "ui/texture_view_panel.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Vertex = lumen::core::ObjVertex;

/// UBO：mat4 mvp + mat3 normalMatrix + 4 光源 (std140 对齐)
struct UBO {
    glm::mat4 mvp;
    glm::mat3 normalMatrix;
    glm::vec4 light0; // xyz=方向 w=强度
    glm::vec4 light1;
    glm::vec4 light2;
    glm::vec4 light3;
};

/// Push Constants：mode + modelColor
struct PushConstants {
    uint32_t mode;
    float _pad[3];
    glm::vec4 modelColor;
};

namespace {

constexpr uint32_t kMaxFramesInFlight { 2 };
constexpr const char *kObjPath { "assets/meshes/monkey/monkey.obj" };
constexpr float kMinOrbitRadius { 0.8f };
constexpr float kMaxOrbitRadius { 20.0f };

/// 与 Unity Scene 视图一致：Q 视图、W 移动、E 旋转、R 缩放
enum class SceneGizmoTool : std::uint8_t {
    View,
    Move,
    Rotate,
    Scale,
};

ImGuizmo::OPERATION scene_gizmo_to_operation(SceneGizmoTool t) {
    switch (t) {
    case SceneGizmoTool::Move:
        return ImGuizmo::TRANSLATE;
    case SceneGizmoTool::Rotate:
        return ImGuizmo::ROTATE;
    case SceneGizmoTool::Scale:
        return ImGuizmo::SCALE;
    case SceneGizmoTool::View:
    default:
        return ImGuizmo::TRANSLATE;
    }
}

} // namespace

static int run_demo3d() {
    lumen::platform::Window window;
    lumen::platform::WindowConfig winConfig;
    winConfig.title = "Demo3D - OBJ 模型";
    winConfig.width = 1280;
    winConfig.height = 720;

    if (!window.create(winConfig)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    auto extensions = window.get_vulkan_instance_extensions();
    lumen::render::ContextConfig ctxConfig;
    ctxConfig.instanceExtensions.assign(extensions.begin(), extensions.end());

    lumen::render::Context ctx;
    if (!ctx.init_instance(ctxConfig)) {
        LUMEN_APP_LOG_ERROR("Vulkan Instance 初始化失败");
        return -1;
    }

    lumen::render::Surface surface(
        ctx.instance(), window.create_vulkan_surface(ctx.instance()));
    if (!surface.is_valid()) {
        LUMEN_APP_LOG_ERROR("Surface 创建失败");
        return -1;
    }

    if (!ctx.init_device(surface.handle())) {
        LUMEN_APP_LOG_ERROR("Vulkan Device 初始化失败");
        return -1;
    }

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);

    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        return -1;
    }

    // RenderPass：主屏（呈现）
    lumen::render::RenderPassConfig rpConfig;
    rpConfig.useDepth = true;
    rpConfig.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass renderPass;
    if (!renderPass.create(ctx.device(), rpConfig)) {
        return -1;
    }

    // 离屏渲染目标：Scene → OffscreenRenderTarget → ImGui 采样
    lumen::render::OffscreenRenderTargetConfig sceneTargetConfig;
    sceneTargetConfig.width = static_cast<uint32_t>(w);
    sceneTargetConfig.height = static_cast<uint32_t>(h);
    sceneTargetConfig.format = swapchain.image_format();
    sceneTargetConfig.useDepth = true;
    sceneTargetConfig.colorFinalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::OffscreenRenderTarget sceneTarget;
    if (!sceneTarget.create(ctx, sceneTargetConfig)) {
        LUMEN_APP_LOG_ERROR("场景离屏目标创建失败");
        return -1;
    }

    constexpr uint32_t kAuxViewportW { 320 };
    constexpr uint32_t kAuxViewportH { 240 };
    lumen::render::OffscreenRenderTargetConfig auxConfig;
    auxConfig.width = kAuxViewportW;
    auxConfig.height = kAuxViewportH;
    auxConfig.format = swapchain.image_format();
    auxConfig.useDepth = true;
    auxConfig.colorFinalLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumen::render::OffscreenRenderTarget wireframeTarget;
    lumen::render::OffscreenRenderTarget normalTarget;
    lumen::render::OffscreenRenderTarget depthTarget;
    if (!wireframeTarget.create(ctx, auxConfig) ||
        !normalTarget.create(ctx, auxConfig) ||
        !depthTarget.create(ctx, auxConfig)) {
        LUMEN_APP_LOG_ERROR("辅助视口创建失败");
        return -1;
    }

    lumen::render::Sampler sceneSampler;
    if (!sceneSampler.create(ctx)) {
        return -1;
    }

    // 深度附件（主屏）
    lumen::render::Image depthImage;
    if (!depthImage.create_depth_attachment(ctx, static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h))) {
        LUMEN_APP_LOG_ERROR("深度附件创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), renderPass.handle(), swapchain,
                             depthImage.view())) {
        return -1;
    }

    // 着色器
    std::string vertPath =
        lumen::core::get_resource_path("shaders/cube.vert.spv");
    std::string fragPath =
        lumen::core::get_resource_path("shaders/cube.frag.spv");
    lumen::render::ShaderModule vertShader;
    lumen::render::ShaderModule fragShader;
    if (!vertShader.create_from_file(ctx.device(), vertPath.c_str()) ||
        !fragShader.create_from_file(ctx.device(), fragPath.c_str())) {
        LUMEN_APP_LOG_ERROR("着色器加载失败");
        return -1;
    }

    // OBJ 模型加载
    lumen::core::ObjMesh mesh;
    std::string objPath = lumen::core::get_resource_path(kObjPath);
    if (!lumen::core::load_obj(objPath, mesh)) {
        LUMEN_APP_LOG_ERROR("OBJ 加载失败: {}", objPath);
        return -1;
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        LUMEN_APP_LOG_ERROR("OBJ 模型为空: {}", objPath);
        return -1;
    }

    // 顶点 / 索引缓冲
    lumen::render::VertexBuffer vertexBuffer;
    lumen::render::IndexBuffer indexBuffer;
    const size_t vertexBytes = mesh.vertices.size() * sizeof(Vertex);
    const size_t indexBytes = mesh.indices.size() * sizeof(uint32_t);
    if (!vertexBuffer.create(ctx, vertexBytes) ||
        !indexBuffer.create(ctx, indexBytes)) {
        return -1;
    }
    vertexBuffer.upload(mesh.vertices.data(), vertexBytes);
    indexBuffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    indexBuffer.upload(mesh.indices.data(), indexBytes);

    const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());

    lumen::render::CommandPool cmdPool;
    if (!cmdPool.create(ctx, ctx.graphics_queue_family())) {
        return -1;
    }

    // 纹理
    lumen::render::Texture texture;
    std::string texPath = lumen::core::get_resource_path(
        "assets/textures/ikun2026_happy_new_year.jpg");
    if (!texture.create_from_file(ctx, texPath.c_str(), ctx.graphics_queue(),
                                  cmdPool)) {
        constexpr uint32_t kTexSize = 64;
        std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
        for (uint32_t y = 0; y < kTexSize; ++y) {
            for (uint32_t x = 0; x < kTexSize; ++x) {
                bool checker = ((x / 8) + (y / 8)) % 2 == 0;
                uint8_t v = checker ? 255 : 80;
                size_t i = (y * kTexSize + x) * 4;
                pixels[i + 0] = v;
                pixels[i + 1] = v;
                pixels[i + 2] = checker ? 180 : 100;
                pixels[i + 3] = 255;
            }
        }
        texture.create_from_memory(ctx, pixels.data(), pixels.size(), kTexSize,
                                   kTexSize, ctx.graphics_queue(), cmdPool);
    }

    // Descriptor
    lumen::render::DescriptorSetLayout descLayout;
    descLayout.create(
        ctx, { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
               { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT } });

    lumen::render::DescriptorPool descPool;
    descPool.create(
        ctx,
        { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight },
          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFramesInFlight } },
        kMaxFramesInFlight);

    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight> uniformBuffers;
    std::array<VkDescriptorSet, kMaxFramesInFlight> descriptorSets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        uniformBuffers[i].create(ctx, sizeof(UBO));
        descPool.allocate(ctx.device(), descLayout.handle(), descriptorSets[i]);
        lumen::render::write_descriptor_buffer(
            ctx.device(), descriptorSets[i], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBuffers[i].handle(), 0,
            sizeof(UBO));
        lumen::render::write_descriptor_image(ctx.device(), descriptorSets[i],
                                              1, texture.view(),
                                              texture.sampler());
    }

    lumen::render::PipelineLayout pipelineLayout;
    VkPushConstantRange pushRange {};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PushConstants);
    pipelineLayout.create(ctx, { descLayout.handle() }, { pushRange });

    lumen::render::GraphicsPipelineConfig pipeConfig;
    pipeConfig.stages.push_back(
        { vertShader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipeConfig.stages.push_back(
        { fragShader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipeConfig.vertexBindings.push_back(
        { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
    pipeConfig.vertexAttributes.push_back(
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) });
    pipeConfig.vertexAttributes.push_back(
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
    pipeConfig.vertexAttributes.push_back(
        { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
    pipeConfig.depthTest = true;
    pipeConfig.depthWrite = true;
    pipeConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    pipeConfig.frontFace =
        VK_FRONT_FACE_COUNTER_CLOCKWISE; // Blender OBJ 外表面为 CCW

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipelineLayout.handle(),
                         sceneTarget.render_pass(), 0, pipeConfig)) {
        return -1;
    }

    lumen::render::GraphicsPipelineConfig wireframeConfig = pipeConfig;
    wireframeConfig.polygonMode = VK_POLYGON_MODE_LINE;
    wireframeConfig.cullMode = VK_CULL_MODE_NONE;
    lumen::render::GraphicsPipeline wireframePipeline;
    if (!wireframePipeline.create(ctx, pipelineLayout.handle(),
                                  sceneTarget.render_pass(), 0,
                                  wireframeConfig)) {
        return -1;
    }

    auto cmdBuffers = cmdPool.allocate(kMaxFramesInFlight);
    lumen::render::FrameSync frameSync;
    frameSync.create(ctx.device(), swapchain.image_count(), kMaxFramesInFlight);

    lumen::platform::EventPump pump;
    uint32_t renderMode { 0 };
    float orbitYaw { 0.0f };
    float orbitPitch { 0.3f };
    float orbitRadius { 2.5f };
    glm::mat4 scene_view { 1.0f };
    glm::mat4 scene_proj { 1.0f };
    glm::mat4 model_matrix { 1.0f };
    SceneGizmoTool scene_gizmo_tool { SceneGizmoTool::Rotate };
    bool gizmo_world_mode { false };
    int fbWidth { w }, fbHeight { h };
    bool needRecreateSwapchain { false };
    uint32_t currentFrame { 0 };
    bool running { true };
    float dt { 0.016f };
    uint32_t nextSceneW { 0 };
    uint32_t nextSceneH { 0 };
    lumen::ui::TextureViewRect sceneRect {}; // Scene Image 屏幕坐标，供射线拾取等使用
    uint32_t nextWireframeW { 0 };
    uint32_t nextWireframeH { 0 };
    uint32_t nextNormalW { 0 };
    uint32_t nextNormalH { 0 };
    uint32_t nextDepthW { 0 };
    uint32_t nextDepthH { 0 };
    glm::vec4 clearColor { 0.1f, 0.12f, 0.18f, 1.0f };
    glm::vec4 modelColor { 1.0f, 1.0f, 1.0f, 1.0f };

    // ImGui 后端
    lumen::ui::ImGuiBackendInitInfo imguiInfo;
    imguiInfo.ctx = &ctx;
    imguiInfo.swapchain = &swapchain;
    imguiInfo.renderPass = renderPass.handle();
    imguiInfo.window = window.sdl_window();
// #ifdef _WIN32
    // 默认内置字体无 CJK；微软雅黑覆盖常用简体 UI 文案
    // imguiInfo.cjk_font_ttf_path = "C:/Windows/Fonts/msyh.ttc";
// #endif
    if (!lumen::ui::imgui_backend_init(imguiInfo)) {
        LUMEN_APP_LOG_ERROR("ImGui 初始化失败");
        return -1;
    }

    lumen::ui::PanelManager ui_panels;
    ui_panels.add(std::make_unique<lumen::ui::LogPanel>());
    ui_panels.add(std::make_unique<lumen::ui::GpuCapabilitiesPanel>(ctx));

    auto sceneTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), sceneTarget.color_view(),
            sceneTarget.color_sample_layout()));
    auto wireframeTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), wireframeTarget.color_view(),
            wireframeTarget.color_sample_layout()));
    auto normalTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), normalTarget.color_view(),
            normalTarget.color_sample_layout()));
    auto depthTextureId =
        reinterpret_cast<ImTextureID>(lumen::ui::imgui_backend_add_texture(
            sceneSampler.handle(), depthTarget.color_view(),
            depthTarget.color_sample_layout()));

    // RenderGraph：声明 reads/writes，自动排序与同步
    lumen::render::RGImage rgColor =
        lumen::render::RGImage::from_texture(sceneTarget.color_image(), false);
    lumen::render::RGImage rgDepth =
        lumen::render::RGImage::from_texture(sceneTarget.depth_image(), true);
    lumen::render::RGImage rgWireframeColor =
        lumen::render::RGImage::from_texture(
            wireframeTarget.color_image(), false);
    lumen::render::RGImage rgWireframeDepth =
        lumen::render::RGImage::from_texture(
            wireframeTarget.depth_image(), true);
    lumen::render::RGImage rgNormalColor =
        lumen::render::RGImage::from_texture(
            normalTarget.color_image(), false);
    lumen::render::RGImage rgNormalDepth =
        lumen::render::RGImage::from_texture(
            normalTarget.depth_image(), true);
    lumen::render::RGImage rgDepthColor =
        lumen::render::RGImage::from_texture(
            depthTarget.color_image(), false);
    lumen::render::RGImage rgDepthDepth =
        lumen::render::RGImage::from_texture(
            depthTarget.depth_image(), true);
    lumen::render::RGImage rgSwapchain =
        lumen::render::RGImage::from_swapchain(swapchain);

    lumen::render::RenderGraph renderGraph(&ctx);
    renderGraph.add_pass(lumen::render::RGPass {
        .name = "Scene",
        .reads = {},
        .writes = { &rgColor, &rgDepth },
        .execute =
            [&](VkCommandBuffer cmd, uint32_t /*swapchainImageIndex*/) {
                const VkExtent2D sceneExtent = sceneTarget.extent();
                VkRenderPassBeginInfo sceneRpBegin {
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                };
                sceneRpBegin.renderPass = sceneTarget.render_pass();
                sceneRpBegin.framebuffer = sceneTarget.framebuffer();
                sceneRpBegin.renderArea = { { 0, 0 }, sceneExtent };
                VkClearValue sceneClearValues[2];
                sceneClearValues[0].color = { { clearColor.r, clearColor.g,
                                                clearColor.b, clearColor.a } };
                sceneClearValues[1].depthStencil = { 1.0f, 0 };
                sceneRpBegin.clearValueCount = 2;
                sceneRpBegin.pClearValues = sceneClearValues;
                vkCmdBeginRenderPass(cmd, &sceneRpBegin,
                                     VK_SUBPASS_CONTENTS_INLINE);

                VkViewport sceneViewport {};
                sceneViewport.width = static_cast<float>(sceneExtent.width);
                sceneViewport.height = static_cast<float>(sceneExtent.height);
                sceneViewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &sceneViewport);
                VkRect2D sceneScissor { { 0, 0 }, sceneExtent };
                vkCmdSetScissor(cmd, 0, 1, &sceneScissor);

                VkPipeline activePipeline = (renderMode == 1u)
                                                ? wireframePipeline.handle()
                                                : pipeline.handle();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  activePipeline);
                PushConstants pushData {};
                pushData.mode = renderMode;
                pushData.modelColor = modelColor;
                vkCmdPushConstants(cmd, pipelineLayout.handle(),
                                   VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(PushConstants), &pushData);
                VkDescriptorSet descSet = descriptorSets[currentFrame];
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout.handle(), 0, 1, &descSet,
                                        0, nullptr);
                VkBuffer vb = vertexBuffer.handle();
                VkDeviceSize vbOffset { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
                vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0,
                                     indexBuffer.vk_index_type());
                vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
                vkCmdEndRenderPass(cmd);
            },
    });

    auto addAuxPass = [&](const char* name, lumen::render::RGImage* outColor,
                         lumen::render::RGImage* outDepth,
                         lumen::render::OffscreenRenderTarget& target,
                         VkPipeline pipelineHandle, uint32_t mode) {
        renderGraph.add_pass(lumen::render::RGPass {
            .name = name,
            .reads = {},
            .writes = { outColor, outDepth },
            .execute =
                [&, pipelineHandle, mode](
                    VkCommandBuffer cmd, uint32_t /*swapchainImageIndex*/) {
                    const VkExtent2D ext = target.extent();
                    if (ext.width == 0 || ext.height == 0)
                        return;
                    VkRenderPassBeginInfo rpBegin {
                        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                    };
                    rpBegin.renderPass = target.render_pass();
                    rpBegin.framebuffer = target.framebuffer();
                    rpBegin.renderArea = { { 0, 0 }, ext };
                    VkClearValue clearVals[2];
                    clearVals[0].color = { { clearColor.r, clearColor.g,
                                             clearColor.b, clearColor.a } };
                    clearVals[1].depthStencil = { 1.0f, 0 };
                    rpBegin.clearValueCount = 2;
                    rpBegin.pClearValues = clearVals;
                    vkCmdBeginRenderPass(cmd, &rpBegin,
                                         VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport vp {};
                    vp.width = static_cast<float>(ext.width);
                    vp.height = static_cast<float>(ext.height);
                    vp.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    VkRect2D scissor { { 0, 0 }, ext };
                    vkCmdSetScissor(cmd, 0, 1, &scissor);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipelineHandle);
                    PushConstants pc {};
                    pc.mode = mode;
                    pc.modelColor = modelColor;
                    vkCmdPushConstants(cmd, pipelineLayout.handle(),
                                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(PushConstants), &pc);
                    VkDescriptorSet ds = descriptorSets[currentFrame];
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipelineLayout.handle(), 0, 1, &ds,
                                            0, nullptr);
                    VkBuffer vb = vertexBuffer.handle();
                    VkDeviceSize vbOff { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOff);
                    vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0,
                                         indexBuffer.vk_index_type());
                    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
                    vkCmdEndRenderPass(cmd);
                },
        });
    };

    addAuxPass("Wireframe", &rgWireframeColor, &rgWireframeDepth,
               wireframeTarget, wireframePipeline.handle(), 1);
    addAuxPass("Normal", &rgNormalColor, &rgNormalDepth, normalTarget,
               pipeline.handle(), 2);
    addAuxPass("Depth", &rgDepthColor, &rgDepthDepth, depthTarget,
               pipeline.handle(), 3);

    renderGraph.add_pass(lumen::render::RGPass {
        .name = "UI",
        .reads = { &rgColor },
        .writes = { &rgSwapchain },
        .execute =
            [&](VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
                VkRenderPassBeginInfo rpBegin {
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
                };
                rpBegin.renderPass = renderPass.handle();
                rpBegin.framebuffer = framebuffers.get(swapchainImageIndex);
                rpBegin.renderArea = { { 0, 0 }, swapchain.extent() };
                VkClearValue clearValues[2];
                clearValues[0].color = { { clearColor.r, clearColor.g,
                                           clearColor.b, clearColor.a } };
                clearValues[1].depthStencil = { 1.0f, 0 };
                rpBegin.clearValueCount = 2;
                rpBegin.pClearValues = clearValues;
                vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport viewport {};
                viewport.width = static_cast<float>(swapchain.extent().width);
                viewport.height = static_cast<float>(swapchain.extent().height);
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor { { 0, 0 }, swapchain.extent() };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                ImGuiID dockspaceId =
                    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
                ui_panels.set_default_dock_id(dockspaceId);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Scene", sceneTextureId, &nextSceneW, &nextSceneH, &sceneRect,
                    ImVec2(0, 0), ImVec2(1, 1),
                    [&](const lumen::ui::TextureViewRect &r) {
                        if (scene_gizmo_tool != SceneGizmoTool::View) {
                            lumen::ui::imguizmo_manipulate(
                                r, scene_view, scene_proj, &model_matrix,
                                scene_gizmo_to_operation(scene_gizmo_tool),
                                gizmo_world_mode ? ImGuizmo::WORLD
                                                 : ImGuizmo::LOCAL);
                        }
                    });
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Wireframe", wireframeTextureId, &nextWireframeW,
                    &nextWireframeH);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Normal", normalTextureId, &nextNormalW, &nextNormalH);
                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                lumen::ui::imgui_texture_view_panel(
                    "Depth", depthTextureId, &nextDepthW, &nextDepthH);

                ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(280, 0),
                                         ImGuiCond_FirstUseEver);
                ImGui::Begin("Demo3D");
                ImGui::Text("Render Mode");
                int mode = static_cast<int>(renderMode);
                ImGui::RadioButton("Lit", &mode, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Wireframe", &mode, 1);
                ImGui::RadioButton("Normal", &mode, 2);
                ImGui::SameLine();
                ImGui::RadioButton("Depth", &mode, 3);
                renderMode = static_cast<uint32_t>(mode);
                ImGui::ColorEdit4("Clear Color", glm::value_ptr(clearColor));
                ImGui::ColorEdit4("Model Color", glm::value_ptr(modelColor));
                ImGui::Separator();
                ImGui::SliderFloat("Camera Distance", &orbitRadius,
                                   kMinOrbitRadius, kMaxOrbitRadius, "%.1f");
                ImGui::Text("Gizmo (Unity: Q/W/E/R)");
                if (ImGui::RadioButton("View (Q)",
                                      scene_gizmo_tool == SceneGizmoTool::View)) {
                    scene_gizmo_tool = SceneGizmoTool::View;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Move (W)",
                                      scene_gizmo_tool == SceneGizmoTool::Move)) {
                    scene_gizmo_tool = SceneGizmoTool::Move;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate (E)",
                                      scene_gizmo_tool == SceneGizmoTool::Rotate)) {
                    scene_gizmo_tool = SceneGizmoTool::Rotate;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale (R)",
                                      scene_gizmo_tool == SceneGizmoTool::Scale)) {
                    scene_gizmo_tool = SceneGizmoTool::Scale;
                }
                ImGui::Checkbox("World mode", &gizmo_world_mode);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "FPS: %.1f",
                                   1.0f / (dt > 0.0f ? dt : 0.016f));
                const auto sceneMouseState =
                    lumen::ui::viewport_mouse_state(
                        sceneRect, pump.input().mouse_x(),
                        pump.input().mouse_y());
                lumen::ui::imgui_viewport_mouse_debug(sceneRect, sceneMouseState,
                                                     "Scene");
                ImGui::End();

                ui_panels.render_all();

                lumen::ui::imgui_backend_render(cmd);
                vkCmdEndRenderPass(cmd);
            },
    });

    lumen::ui::imgui_setup_event_pump(pump);
    lumen::platform::add_input_debug_handler(pump); // 调试：输出鼠标键盘事件到 logs/engine.log

    LUMEN_APP_LOG_INFO(
        "Demo3D 启动 [A/D/↑/↓] 相机偏航/俯仰 [右键拖拽] 相机 [左键拖拽] 模型 [滚轮] 缩放 "
        "[Q/W/E/R] Gizmo 视图/移动/旋转/缩放（与 Unity Scene 一致） "
        "[0] 光照 [1] 线框 [2] 法线 [3] 深度 [ESC] 退出");

    constexpr float kMouseSensitivity { 0.007f };
    constexpr float kZoomSpeed { 0.25f };

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape) {
            running = false;
        } else if (e.key == lumen::platform::Key::Num0) {
            renderMode = 0;
        } else if (e.key == lumen::platform::Key::Num1) {
            renderMode = 1;
        } else if (e.key == lumen::platform::Key::Num2) {
            renderMode = 2;
        } else if (e.key == lumen::platform::Key::Num3) {
            renderMode = 3;
        }
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fbWidth = r.width;
        fbHeight = r.height;
        needRecreateSwapchain = true;
    });
    // 拖拽时启用相对鼠标模式（ImGui 未占用时）
    pump.on_mouse_button_down(
        [&](const lumen::platform::EventMouseButtonDown &e) {
            const auto sceneHover = lumen::ui::viewport_mouse_state(
                sceneRect, pump.input().mouse_x(), pump.input().mouse_y());
            if (lumen::ui::imgui_wants_mouse() && !sceneHover.inViewport)
                return;
            if (e.button == lumen::platform::MouseButton::Left ||
                e.button == lumen::platform::MouseButton::Right) {
                SDL_SetWindowRelativeMouseMode(window.sdl_window(), true);
            }
        });
    pump.on_mouse_button_up([&](const lumen::platform::EventMouseButtonUp &e) {
        if (e.button == lumen::platform::MouseButton::Left ||
            e.button == lumen::platform::MouseButton::Right) {
            const auto &inp = pump.input();
            bool otherDown = (e.button == lumen::platform::MouseButton::Left
                                  ? inp.is_mouse_button_down(
                                        lumen::platform::MouseButton::Right)
                                  : inp.is_mouse_button_down(
                                        lumen::platform::MouseButton::Left));
            if (!otherDown) {
                SDL_SetWindowRelativeMouseMode(window.sdl_window(), false);
            }
        }
    });
    pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel &e) {
        const auto sceneHover = lumen::ui::viewport_mouse_state(
            sceneRect, pump.input().mouse_x(), pump.input().mouse_y());
        if (lumen::ui::imgui_wants_mouse() && !sceneHover.inViewport)
            return;
        orbitRadius = glm::clamp(orbitRadius - e.deltaY * kZoomSpeed,
                                 kMinOrbitRadius, kMaxOrbitRadius);
    });

    constexpr float kOrbitSpeed = 1.2f;
    constexpr uint64_t kAcquireTimeoutNs = 100'000'000;
    constexpr uint64_t kFenceWaitTimeoutNs = 16'000'000;

    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;

    while (running) {
        if (!pump.poll())
            break;

        if (needRecreateSwapchain) {
            window.get_framebuffer_size(&fbWidth, &fbHeight);
            if (fbWidth > 0 && fbHeight > 0) {
                lumen::render::Image newDepth;
                newDepth.create_depth_attachment(
                    ctx, static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight));
                // 必须先 destroy framebuffers 再替换 depthImage，否则旧 depth view
                // 仍被 framebuffer 引用时被销毁会触发 VUID-vkDestroyImageView-01026
                lumen::render::recreate_swapchain_resources(
                    ctx, swapchain, framebuffers, frameSync,
                    renderPass.handle(), static_cast<uint32_t>(fbWidth),
                    static_cast<uint32_t>(fbHeight), kMaxFramesInFlight,
                    newDepth.view());
                depthImage = std::move(newDepth);

                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(sceneTextureId));

                sceneTarget.resize(ctx, static_cast<uint32_t>(fbWidth),
                                   static_cast<uint32_t>(fbHeight));
                sceneTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), sceneTarget.color_view(),
                        sceneTarget.color_sample_layout()));

                rgColor = lumen::render::RGImage::from_texture(
                    sceneTarget.color_image(), false);
                rgDepth = lumen::render::RGImage::from_texture(
                    sceneTarget.depth_image(), true);
                rgSwapchain = lumen::render::RGImage::from_swapchain(swapchain);
                nextSceneW = static_cast<uint32_t>(fbWidth);
                nextSceneH = static_cast<uint32_t>(fbHeight);
            }
            needRecreateSwapchain = false;
            continue;
        }

        while (!frameSync.wait_fence(currentFrame, kFenceWaitTimeoutNs)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running)
            break;

        dt = static_cast<float>(frame_dt.tick_seconds());

        lumen::ui::imgui_backend_new_frame();

        // Q/W/E/R 须在 NewFrame 之后用 ImGui 按键查询：poll() 里
        // imgui_wants_keyboard() 在 Dock 焦点下几乎恒为 true，会挡掉 on_key_down。
        {
            const ImGuiIO &io = ImGui::GetIO();
            if (!io.WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                    scene_gizmo_tool = SceneGizmoTool::View;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Move;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Rotate;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                    scene_gizmo_tool = SceneGizmoTool::Scale;
                }
            }
        }

        // 根据上一帧 UI 计算的显示尺寸调整离屏渲染分辨率（wait_idle
        // 避免多帧并发时资源冲突）
        const bool needSceneResize =
            nextSceneW > 0 && nextSceneH > 0 &&
            (sceneTarget.extent().width != nextSceneW ||
             sceneTarget.extent().height != nextSceneH);
        const bool needWireframeResize =
            nextWireframeW > 0 && nextWireframeH > 0 &&
            (wireframeTarget.extent().width != nextWireframeW ||
             wireframeTarget.extent().height != nextWireframeH);
        const bool needNormalResize =
            nextNormalW > 0 && nextNormalH > 0 &&
            (normalTarget.extent().width != nextNormalW ||
             normalTarget.extent().height != nextNormalH);
        const bool needDepthResize =
            nextDepthW > 0 && nextDepthH > 0 &&
            (depthTarget.extent().width != nextDepthW ||
             depthTarget.extent().height != nextDepthH);
        if (needSceneResize || needWireframeResize || needNormalResize ||
            needDepthResize) {
            ctx.wait_idle();
            if (needSceneResize) {
                sceneTarget.resize(ctx, nextSceneW, nextSceneH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(sceneTextureId));
                sceneTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), sceneTarget.color_view(),
                        sceneTarget.color_sample_layout()));
                rgColor = lumen::render::RGImage::from_texture(
                    sceneTarget.color_image(), false);
                rgDepth = lumen::render::RGImage::from_texture(
                    sceneTarget.depth_image(), true);
            }
            if (needWireframeResize) {
                wireframeTarget.resize(ctx, nextWireframeW, nextWireframeH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(wireframeTextureId));
                wireframeTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), wireframeTarget.color_view(),
                        wireframeTarget.color_sample_layout()));
                rgWireframeColor = lumen::render::RGImage::from_texture(
                    wireframeTarget.color_image(), false);
                rgWireframeDepth = lumen::render::RGImage::from_texture(
                    wireframeTarget.depth_image(), true);
            }
            if (needNormalResize) {
                normalTarget.resize(ctx, nextNormalW, nextNormalH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(normalTextureId));
                normalTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), normalTarget.color_view(),
                        normalTarget.color_sample_layout()));
                rgNormalColor = lumen::render::RGImage::from_texture(
                    normalTarget.color_image(), false);
                rgNormalDepth = lumen::render::RGImage::from_texture(
                    normalTarget.depth_image(), true);
            }
            if (needDepthResize) {
                depthTarget.resize(ctx, nextDepthW, nextDepthH);
                lumen::ui::imgui_backend_remove_texture(
                    reinterpret_cast<void *>(depthTextureId));
                depthTextureId = reinterpret_cast<ImTextureID>(
                    lumen::ui::imgui_backend_add_texture(
                        sceneSampler.handle(), depthTarget.color_view(),
                        depthTarget.color_sample_layout()));
                rgDepthColor = lumen::render::RGImage::from_texture(
                    depthTarget.color_image(), false);
                rgDepthDepth = lumen::render::RGImage::from_texture(
                    depthTarget.depth_image(), true);
            }
        }

        const auto &inp = pump.input();
        // 悬停在 Scene 的 Image 上时 ImGui 会 WantCaptureMouse，但仍应允许 3D 导航
        const auto sceneNavMouse = lumen::ui::viewport_mouse_state(
            sceneRect, inp.mouse_x(), inp.mouse_y());
        const bool imguiBlocksKb = lumen::ui::imgui_wants_keyboard();
        const bool imguiBlocksMouse =
            lumen::ui::imgui_wants_mouse() && !sceneNavMouse.inViewport;
        if (scene_gizmo_tool == SceneGizmoTool::View) {
            lumen::ui::imguizmo_reset_interaction_state();
        }
        if (!imguiBlocksKb) {
            if (inp.is_key_down(lumen::platform::Key::A))
                orbitYaw += kOrbitSpeed * dt;
            if (inp.is_key_down(lumen::platform::Key::D))
                orbitYaw -= kOrbitSpeed * dt;
            // 俯仰用方向键，避免与 Unity 式 W（移动 Gizmo）冲突
            if (inp.is_key_down(lumen::platform::Key::Up))
                orbitPitch =
                    glm::clamp(orbitPitch + kOrbitSpeed * dt, 0.1f, 1.4f);
            if (inp.is_key_down(lumen::platform::Key::Down))
                orbitPitch =
                    glm::clamp(orbitPitch - kOrbitSpeed * dt, 0.1f, 1.4f);
        }

        // 左键拖拽模型、右键拖拽相机（与启动日志一致；Scene Image 上仍允许导航）
        if (!imguiBlocksMouse && !lumen::ui::imguizmo_is_using() &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Left)) {
            model_matrix =
                glm::rotate(glm::mat4(1.0f),
                            -inp.mouse_delta_x() * kMouseSensitivity,
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                model_matrix;
            model_matrix =
                glm::rotate(glm::mat4(1.0f),
                            inp.mouse_delta_y() * kMouseSensitivity,
                            glm::vec3(1.0f, 0.0f, 0.0f)) *
                model_matrix;
        }
        if (!imguiBlocksMouse &&
            inp.is_mouse_button_down(lumen::platform::MouseButton::Right)) {
            orbitYaw -= inp.mouse_delta_x() * kMouseSensitivity;
            orbitPitch =
                glm::clamp(orbitPitch + inp.mouse_delta_y() * kMouseSensitivity,
                           0.1f, 1.4f);
        }

        uint32_t imageIndex = swapchain.acquire_next_image(
            frameSync.image_available(currentFrame), VK_NULL_HANDLE,
            kAcquireTimeoutNs);
        if (imageIndex == UINT32_MAX)
            continue;

        // MVP 矩阵（与 Scene 视口 Gizmo 共用 view / proj）
        glm::vec3 cameraPos =
            orbitRadius * glm::vec3(std::sin(orbitYaw) * std::cos(orbitPitch),
                                    std::sin(orbitPitch),
                                    std::cos(orbitYaw) * std::cos(orbitPitch));
        scene_view = glm::lookAt(cameraPos, glm::vec3(0.0f),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
        const VkExtent2D sceneExtentForProj = sceneTarget.extent();
        scene_proj =
            glm::perspective(glm::radians(42.0f),
                             static_cast<float>(sceneExtentForProj.width) /
                                 static_cast<float>(sceneExtentForProj.height),
                             0.1f, 100.0f);
        scene_proj[1][1] *= -1.0f; // Vulkan NDC Y 向下；frontFace 与模型绕序匹配
        UBO ubo {};
        ubo.mvp = scene_proj * scene_view * model_matrix;
        ubo.normalMatrix =
            glm::mat3(glm::transpose(glm::inverse(model_matrix)));
        // 多光源：方向为从表面指向光源，Blender 猴头正面朝 -Z
        ubo.light0 = glm::vec4(0.0f, 0.5f, -1.0f, 1.2f); // 主光：正前方偏上，强
        ubo.light1 = glm::vec4(-0.6f, 0.5f, -0.6f, 0.7f); // 填充：左前方
        ubo.light2 = glm::vec4(0.5f, 0.3f, -0.8f, 0.6f);  // 右前方补光
        ubo.light3 = glm::vec4(0.0f, -0.5f, -0.9f, 0.5f); // 底光：照下巴
        uniformBuffers[currentFrame].update(ubo);

        VkCommandBuffer cmd = cmdBuffers[currentFrame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo beginInfo {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            continue;
        }

        renderGraph.execute(cmd, imageIndex);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            continue;
        }

        VkSemaphore waitSem = frameSync.image_available(currentFrame);
        VkSemaphore signalSem = frameSync.render_finished(imageIndex);
        VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSem;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSem;

        if (vkQueueSubmit(ctx.graphics_queue(), 1, &submitInfo,
                          frameSync.in_flight_fence(currentFrame)) !=
            VK_SUCCESS)
            continue;

        VkResult presentResult =
            swapchain.present(ctx.present_queue(), imageIndex, signalSem);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
            needRecreateSwapchain = true;

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    lumen::ui::imgui_backend_shutdown();
    LUMEN_APP_LOG_INFO("Demo3D 退出");
    return 0;
}

int main() {
    if (!lumen::core::Logger::init())
        return -1;
    int result = run_demo3d();
    lumen::core::Logger::shutdown();
    return result;
}
