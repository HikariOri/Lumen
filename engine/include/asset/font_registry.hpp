/**
 * @file asset/font_registry.hpp
 * @brief ImGui TTF 加载去重（须在 `ImGui_ImplVulkan_CreateFontsTexture` 之前完成）
 *
 * @note 缓存键：TTF 路径 + `pixel_size` + `FontGlyphPreset` + `merge_mode`。须在
 * `imgui_backend_init` 内、调用 `ImGui_ImplVulkan_CreateFontsTexture` **之前** 完成
 * `AddFontFromFileTTF`。DPI / 字号变更需 `ImGui_ImplVulkan_DestroyFontsTexture` 后重建
 * atlas，并 `FontRegistry::clear_session()` 再按新参数重新加载（第二迭代可集中封装）。
 */

#pragma once

#include <string>
#include <unordered_map>

struct ImFont;
struct ImGuiIO;

namespace lumen::asset {

enum class FontGlyphPreset : std::uint8_t {
    DefaultLatin = 0,
    ChineseSimplifiedCommon = 1,
    ChineseFull = 2,
    Japanese = 3,
};

struct FontLoadDesc {
    std::string ttf_path {};
    float pixel_size { 18.0F };
    FontGlyphPreset preset { FontGlyphPreset::DefaultLatin };
    bool merge_mode { false };
};

class FontRegistry {
public:
    FontRegistry() = default;
    FontRegistry(const FontRegistry &) = delete;
    FontRegistry &operator=(const FontRegistry &) = delete;

    /**
     * @brief 按路径 + 字号 + 预设 + merge 去重；失败返回 nullptr
     */
    [[nodiscard]] ImFont *get_or_load_ttf(ImGuiIO &io, const FontLoadDesc &desc);

    void clear_session();

    /**
     * @brief 与 `ImGuiBackendInitInfo` CJK 分支等价，使用本注册表去重子步骤
     */
    void setup_imgui_application_fonts(ImGuiIO &io, const char *cjk_font_ttf_path,
                                       const char *cjk_font_japanese_merge_path,
                                       float cjk_font_size_pixels);

private:
    std::unordered_map<std::string, ImFont *> fonts_by_key_;
};

} // namespace lumen::asset
