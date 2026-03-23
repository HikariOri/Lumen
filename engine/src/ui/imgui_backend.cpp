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
namespace {

/**
 * @brief Lumen 编辑器默认 ImGui 样式：深蓝灰底、冷色强调，与 3D
 * 视口常见清屏色协调
 */
void apply_lumen_imgui_style() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 5.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 22.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    ImVec4 *c = style.Colors;
    const ImVec4 accent { 0.38f, 0.62f, 0.98f, 1.0f };
    const ImVec4 accent_h { 0.50f, 0.72f, 1.00f, 1.0f };
    const ImVec4 accent_muted { 0.28f, 0.42f, 0.62f, 1.0f };
    const ImVec4 bg_root { 0.07f, 0.08f, 0.11f, 1.0f };
    const ImVec4 bg_panel { 0.10f, 0.11f, 0.15f, 1.0f };
    const ImVec4 bg_frame { 0.14f, 0.15f, 0.20f, 1.0f };
    const ImVec4 border { 0.22f, 0.24f, 0.32f, 1.0f };
    const ImVec4 text { 0.93f, 0.94f, 0.96f, 1.0f };
    const ImVec4 text_dim { 0.52f, 0.55f, 0.62f, 1.0f };

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = text_dim;
    c[ImGuiCol_WindowBg] = bg_root;
    c[ImGuiCol_ChildBg] = bg_panel;
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.16f, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = bg_frame;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.27f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.25f, 0.34f, 1.0f);
    c[ImGuiCol_TitleBg] = bg_panel;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.15f, 0.21f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = bg_panel;
    c[ImGuiCol_MenuBarBg] = bg_panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.72f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.34f, 0.42f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.42f, 0.44f, 0.52f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = accent_muted;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent_h;
    c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.30f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.34f, 0.46f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.40f, 0.55f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.22f, 0.28f, 0.40f, 0.72f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.42f, 0.62f, 0.62f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.48f, 0.70f, 0.78f);
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accent_muted;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.58f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.48f, 0.72f, 0.86f);
    c[ImGuiCol_TabActive] = ImVec4(0.16f, 0.18f, 0.24f, 1.0f);
    c[ImGuiCol_TabUnfocused] = c[ImGuiCol_Tab];
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.15f, 0.20f, 1.0f);
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.0f);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = accent_h;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accent_h;
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.38f);
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.05f, 0.07f, 0.62f);
}

/**
 * @brief 与 TheCherno **Hazel** `ImGuiLayer::SetDarkThemeColors` 一致的暗色主题
 *（见 github.com/TheCherno/Hazel `Hazel/src/Hazel/ImGui/ImGuiLayer.cpp`）。
 *
 * Hazel 在启用 Viewports 时还将 `WindowRounding = 0` 且保证 `WindowBg` 不透明；
 * Lumen 当前未接 ImGui 多视口，仍采用相同圆角与窗口底色以贴近其 Dock 区观感。
 */
void apply_hazel_imgui_style() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4 { 0.1f, 0.105f, 0.11f, 1.0f };

    colors[ImGuiCol_Header] = ImVec4 { 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = ImVec4 { 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_HeaderActive] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_Button] = ImVec4 { 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = ImVec4 { 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_ButtonActive] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_FrameBg] = ImVec4 { 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = ImVec4 { 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_FrameBgActive] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };

    colors[ImGuiCol_Tab] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabHovered] = ImVec4 { 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive] = ImVec4 { 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4 { 0.2f, 0.205f, 0.21f, 1.0f };

    colors[ImGuiCol_TitleBg] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgActive] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4 { 0.15f, 0.1505f, 0.151f, 1.0f };

    // Dock 空区与主窗底色一致（Hazel 未单独设置，此处避免与默认 Dark 差异过大）
    colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];
}

} // namespace

bool imgui_backend_init(const ImGuiBackendInitInfo &info) {
    if (!info.ctx || !info.swapchain || !info.renderPass || !info.window) {
        LUMEN_LOG_ERROR("ImGuiBackend: 缺少必要参数");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    apply_lumen_imgui_style();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    {
        const float size = info.cjk_font_size_pixels > 0.0f
                               ? info.cjk_font_size_pixels
                               : 18.0f;
        const bool has_sc = info.cjk_font_ttf_path != nullptr &&
                            info.cjk_font_ttf_path[0] != '\0';
        const bool has_jp_merge =
            info.cjk_font_japanese_merge_path != nullptr &&
            info.cjk_font_japanese_merge_path[0] != '\0';

        if (has_sc && has_jp_merge) {
            // 思源黑体：先拉丁 + 再合并中文全量（同一 SC 文件），再合并日文 OTF
            ImFont *base = io.Fonts->AddFontFromFileTTF(
                info.cjk_font_ttf_path, size, nullptr,
                io.Fonts->GetGlyphRangesDefault());
            if (!base) {
                LUMEN_LOG_WARN("ImGui: failed to load primary CJK font ({}); "
                               "falling back to default font",
                               info.cjk_font_ttf_path);
                io.Fonts->AddFontDefault();
            } else {
                ImFontConfig merge_cfg {};
                merge_cfg.MergeMode = true;
                merge_cfg.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(
                    info.cjk_font_ttf_path, size, &merge_cfg,
                    io.Fonts->GetGlyphRangesChineseFull());
                ImFont *jp = io.Fonts->AddFontFromFileTTF(
                    info.cjk_font_japanese_merge_path, size, &merge_cfg,
                    io.Fonts->GetGlyphRangesJapanese());
                if (!jp) {
                    LUMEN_LOG_WARN("ImGui: failed to merge Japanese font ({}); "
                                   "Japanese glyphs may be missing",
                                   info.cjk_font_japanese_merge_path);
                }
            }
        } else if (has_sc) {
            ImFontConfig font_cfg {};
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
