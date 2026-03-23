/**
 * @file material_library.cpp
 */

#include "scene/material_library.hpp"

#include "core/logger.hpp"

#include <fstream>
#include <sstream>

#include <ghc/filesystem.hpp>
#include <nlohmann/json.hpp>

namespace lumen::scene {
namespace {

namespace fs = ghc::filesystem;

[[nodiscard]] const char *alpha_mode_to_string(MaterialAlphaMode m) {
    switch (m) {
    case MaterialAlphaMode::Opaque:
        return "opaque";
    case MaterialAlphaMode::Mask:
        return "mask";
    case MaterialAlphaMode::Blend:
        return "blend";
    }
    return "opaque";
}

[[nodiscard]] MaterialAlphaMode alpha_mode_from_string(const std::string &s) {
    if (s == "mask") {
        return MaterialAlphaMode::Mask;
    }
    if (s == "blend") {
        return MaterialAlphaMode::Blend;
    }
    return MaterialAlphaMode::Opaque;
}

[[nodiscard]] nlohmann::json material_to_json(const MaterialComponent &m) {
    nlohmann::json j;
    j["base_color_factor"] = { m.base_color_factor.x, m.base_color_factor.y,
                                m.base_color_factor.z, m.base_color_factor.w };
    j["metallic_factor"] = m.metallic_factor;
    j["roughness_factor"] = m.roughness_factor;
    j["ao_factor"] = m.ao_factor;
    j["emissive_factor"] = { m.emissive_factor.x, m.emissive_factor.y,
                               m.emissive_factor.z };
    j["alpha_mode"] = alpha_mode_to_string(m.alpha_mode);
    j["alpha_cutoff"] = m.alpha_cutoff;
    j["double_sided"] = m.double_sided;
    j["spec_gloss_texture_in_mr_slot"] = m.spec_gloss_texture_in_mr_slot;
    j["albedo_path"] = m.albedo_path;
    j["normal_path"] = m.normal_path;
    j["metallic_roughness_path"] = m.metallic_roughness_path;
    j["ao_path"] = m.ao_path;
    j["emissive_path"] = m.emissive_path;
    return j;
}

void material_from_json(const nlohmann::json &j, MaterialComponent &out) {
    out = MaterialComponent {};
    if (j.contains("base_color_factor") && j["base_color_factor"].is_array() &&
        j["base_color_factor"].size() >= 4) {
        const auto &a = j["base_color_factor"];
        out.base_color_factor = glm::vec4(static_cast<float>(a[0].get<double>()),
                                          static_cast<float>(a[1].get<double>()),
                                          static_cast<float>(a[2].get<double>()),
                                          static_cast<float>(a[3].get<double>()));
    }
    if (j.contains("metallic_factor")) {
        out.metallic_factor = static_cast<float>(j["metallic_factor"].get<double>());
    }
    if (j.contains("roughness_factor")) {
        out.roughness_factor =
            static_cast<float>(j["roughness_factor"].get<double>());
    }
    if (j.contains("ao_factor")) {
        out.ao_factor = static_cast<float>(j["ao_factor"].get<double>());
    }
    if (j.contains("emissive_factor") && j["emissive_factor"].is_array() &&
        j["emissive_factor"].size() >= 3) {
        const auto &e = j["emissive_factor"];
        out.emissive_factor =
            glm::vec3(static_cast<float>(e[0].get<double>()),
                      static_cast<float>(e[1].get<double>()),
                      static_cast<float>(e[2].get<double>()));
    }
    if (j.contains("alpha_mode")) {
        out.alpha_mode =
            alpha_mode_from_string(j["alpha_mode"].get<std::string>());
    }
    if (j.contains("alpha_cutoff")) {
        out.alpha_cutoff = static_cast<float>(j["alpha_cutoff"].get<double>());
    }
    if (j.contains("double_sided")) {
        out.double_sided = j["double_sided"].get<bool>();
    }
    if (j.contains("spec_gloss_texture_in_mr_slot")) {
        out.spec_gloss_texture_in_mr_slot =
            j["spec_gloss_texture_in_mr_slot"].get<bool>();
    }
    if (j.contains("albedo_path")) {
        out.albedo_path = j["albedo_path"].get<std::string>();
    }
    if (j.contains("normal_path")) {
        out.normal_path = j["normal_path"].get<std::string>();
    }
    if (j.contains("metallic_roughness_path")) {
        out.metallic_roughness_path =
            j["metallic_roughness_path"].get<std::string>();
    }
    if (j.contains("ao_path")) {
        out.ao_path = j["ao_path"].get<std::string>();
    }
    if (j.contains("emissive_path")) {
        out.emissive_path = j["emissive_path"].get<std::string>();
    }
}

} // namespace

bool MaterialAssetLibrary::load_from_json_file(std::string_view path) {
    assets_.clear();
    const std::string p { path };
    std::ifstream in(p);
    if (!in) {
        LUMEN_LOG_WARN("MaterialAssetLibrary: 无法打开 {}", p);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(ss.str());
    } catch (const std::exception &e) {
        LUMEN_LOG_ERROR("MaterialAssetLibrary JSON 解析失败 {}: {}", p,
                        e.what());
        return false;
    }
    if (!root.contains("materials") || !root["materials"].is_object()) {
        LUMEN_LOG_WARN("MaterialAssetLibrary: 根对象缺少 materials: {}", p);
        return false;
    }
    for (const auto &[key, val] : root["materials"].items()) {
        MaterialComponent m {};
        material_from_json(val, m);
        assets_[key] = std::move(m);
    }
    LUMEN_LOG_DEBUG("MaterialAssetLibrary: 已加载 {} 项自 {}", assets_.size(),
                    p);
    return true;
}

bool MaterialAssetLibrary::save_to_json_file(std::string_view path) const {
    nlohmann::json root;
    root["version"] = 1;
    nlohmann::json mats = nlohmann::json::object();
    for (const auto &[id, m] : assets_) {
        mats[id] = material_to_json(m);
    }
    root["materials"] = std::move(mats);
    const std::string p { path };
    std::ofstream out(p);
    if (!out) {
        LUMEN_LOG_ERROR("MaterialAssetLibrary: 无法写入 {}", p);
        return false;
    }
    out << root.dump(2);
    return true;
}

std::optional<MaterialComponent>
MaterialAssetLibrary::try_get(std::string_view id) const {
    const std::string k { id };
    const auto it = assets_.find(k);
    if (it == assets_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace lumen::scene
