/**
 * @file asset/font_registry.cpp
 */

#include "asset/font_registry.hpp"

#include "core/log/logger.hpp"

#include <imgui.h>

#include <cstdio>

namespace lumen::asset {
namespace {

[[nodiscard]] std::string make_font_key(const FontLoadDesc &desc) {
    const int preset = static_cast<int>(desc.preset);
    const int merge = desc.merge_mode ? 1 : 0;
    char buf[64];
    (void)std::snprintf(buf, sizeof(buf), "%d|%d|", preset, merge);
    return std::string { buf } + desc.ttf_path + '|' +
           std::to_string(static_cast<double>(desc.pixel_size));
}

[[nodiscard]] const ImWchar *glyph_ranges_for(ImGuiIO &io,
                                              FontGlyphPreset preset) {
    switch (preset) {
    case FontGlyphPreset::DefaultLatin:
        return io.Fonts->GetGlyphRangesDefault();
    case FontGlyphPreset::ChineseSimplifiedCommon:
        return io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    case FontGlyphPreset::ChineseFull:
        return io.Fonts->GetGlyphRangesChineseFull();
    case FontGlyphPreset::Japanese: return io.Fonts->GetGlyphRangesJapanese();
    }
    return io.Fonts->GetGlyphRangesDefault();
}

} // namespace

ImFont *FontRegistry::get_or_load_ttf(ImGuiIO &io, const FontLoadDesc &desc) {
    if (desc.ttf_path.empty()) {
        return nullptr;
    }
    const std::string key = make_font_key(desc);
    if (const auto it = fonts_by_key_.find(key); it != fonts_by_key_.end()) {
        return it->second;
    }
    ImFontConfig cfg {};
    if (desc.merge_mode) {
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
    }
    ImFont *f = io.Fonts->AddFontFromFileTTF(
        desc.ttf_path.c_str(), desc.pixel_size,
        desc.merge_mode ? &cfg : nullptr, glyph_ranges_for(io, desc.preset));
    if (f != nullptr) {
        fonts_by_key_.emplace(key, f);
    }
    return f;
}

void FontRegistry::clear_session() { fonts_by_key_.clear(); }

void FontRegistry::setup_imgui_application_fonts(
    ImGuiIO &io, const char *cjk_font_ttf_path,
    const char *cjk_font_japanese_merge_path, float cjk_font_size_pixels) {
    const float size =
        cjk_font_size_pixels > 0.0F ? cjk_font_size_pixels : 18.0F;
    const bool has_sc =
        cjk_font_ttf_path != nullptr && cjk_font_ttf_path[0] != '\0';
    const bool has_jp_merge = cjk_font_japanese_merge_path != nullptr &&
                              cjk_font_japanese_merge_path[0] != '\0';

    if (has_sc && has_jp_merge) {
        ImFont *base = get_or_load_ttf(
            io, FontLoadDesc { .ttf_path = cjk_font_ttf_path,
                               .pixel_size = size,
                               .preset = FontGlyphPreset::DefaultLatin,
                               .merge_mode = false });
        if (base == nullptr) {
            LUMEN_LOG_WARN("ImGui: failed to load primary CJK font ({}); "
                           "falling back to default font",
                           cjk_font_ttf_path);
            io.Fonts->AddFontDefault();
        } else {
            (void)get_or_load_ttf(
                io, FontLoadDesc { .ttf_path = cjk_font_ttf_path,
                                   .pixel_size = size,
                                   .preset = FontGlyphPreset::ChineseFull,
                                   .merge_mode = true });
            ImFont *jp = get_or_load_ttf(
                io, FontLoadDesc { .ttf_path = cjk_font_japanese_merge_path,
                                   .pixel_size = size,
                                   .preset = FontGlyphPreset::Japanese,
                                   .merge_mode = true });
            if (jp == nullptr) {
                LUMEN_LOG_WARN("ImGui: failed to merge Japanese font ({}); "
                               "Japanese glyphs may be missing",
                               cjk_font_japanese_merge_path);
            }
        }
    } else if (has_sc) {
        ImFont *loaded = get_or_load_ttf(
            io,
            FontLoadDesc { .ttf_path = cjk_font_ttf_path,
                           .pixel_size = size,
                           .preset = FontGlyphPreset::ChineseSimplifiedCommon,
                           .merge_mode = false });
        if (loaded == nullptr) {
            LUMEN_LOG_WARN(
                "ImGui: failed to load CJK font ({}); Chinese UI will be "
                "missing glyphs",
                cjk_font_ttf_path);
            io.Fonts->AddFontDefault();
        }
    } else {
        io.Fonts->AddFontDefault();
    }
}

} // namespace lumen::asset
