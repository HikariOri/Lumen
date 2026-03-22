/**
 * @file imgui_backend.cpp
 * @brief ImGui Vulkan + SDL3 后端实现
 */

#include "ui/imgui_backend.hpp"

#include "core/logger.hpp"

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace lumen::ui {

bool imgui_backend_init(const ImGuiBackendInitInfo &info) {
    if (!info.ctx || !info.swapchain || !info.renderPass || !info.window) {
        LUMEN_LOG_ERROR("ImGuiBackend: 缺少必要参数");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    {
        ImFontConfig font_cfg {};
        const float size =
            info.cjk_font_size_pixels > 0.0f ? info.cjk_font_size_pixels
                                             : 18.0f;
        if (info.cjk_font_ttf_path != nullptr &&
            info.cjk_font_ttf_path[0] != '\0') {
            ImFont *loaded = io.Fonts->AddFontFromFileTTF(
                info.cjk_font_ttf_path, size, &font_cfg,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (!loaded) {
                LUMEN_LOG_WARN(
                    "ImGui: failed to load CJK font ({}); Chinese UI will be "
                    "missing glyphs",
                    info.cjk_font_ttf_path);
                io.Fonts->AddFontDefault();
            }
        } else {
            io.Fonts->AddFontDefault();
        }
    }

    if (!ImGui_ImplSDL3_InitForVulkan(info.window)) {
        LUMEN_LOG_ERROR("ImGui SDL3 后端初始化失败");
        ImGui::DestroyContext();
        return false;
    }

    ImGui_ImplVulkan_InitInfo vulkanInfo {};
    vulkanInfo.Instance = info.ctx->instance();
    vulkanInfo.PhysicalDevice = info.ctx->physical_device();
    vulkanInfo.Device = info.ctx->device();
    vulkanInfo.QueueFamily = info.ctx->graphics_queue_family();
    vulkanInfo.Queue = info.ctx->graphics_queue();
    vulkanInfo.DescriptorPool = VK_NULL_HANDLE;
    vulkanInfo.DescriptorPoolSize = 1000;
    vulkanInfo.RenderPass = info.renderPass;
    vulkanInfo.MinImageCount = 2;
    vulkanInfo.ImageCount = info.swapchain->image_count();
    vulkanInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vulkanInfo.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS) {
            LUMEN_LOG_ERROR("ImGui Vulkan: VkResult = {}",
                            static_cast<int>(err));
        }
    };

    if (!ImGui_ImplVulkan_Init(&vulkanInfo)) {
        LUMEN_LOG_ERROR("ImGui Vulkan 后端初始化失败");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    // 新版 ImGui Vulkan 后端内部处理字体纹理上传
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        LUMEN_LOG_ERROR("ImGui 字体纹理创建失败");
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    LUMEN_LOG_DEBUG("ImGui 后端初始化完成");
    return true;
}

void imgui_backend_shutdown() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void imgui_backend_set_min_image_count(uint32_t /*minImageCount*/) {
    // 不调用 ImGui_ImplVulkan_SetMinImageCount：当 swapchain 从 2 图变为 3 图时
    // 会触发 IM_ASSERT(0)，ImGui 暂不支持此运行时变更，保持初始化时的值即可
}

void imgui_backend_new_frame() {
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void imgui_backend_render(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void *imgui_backend_add_texture(VkSampler sampler, VkImageView imageView,
                                VkImageLayout imageLayout) {
    VkDescriptorSet set =
        ImGui_ImplVulkan_AddTexture(sampler, imageView, imageLayout);
    return static_cast<void *>(set);
}

void imgui_backend_remove_texture(void *textureId) {
    if (textureId)
        ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(textureId));
}

} // namespace lumen::ui
