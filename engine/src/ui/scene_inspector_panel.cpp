/**
 * @file scene_inspector_panel.cpp
 */

#include "ui/scene_inspector_panel.hpp"

#include "scene/components.hpp"
#include "scene/scene.hpp"
#include "ui/editor_selection.hpp"
#include "ui/imgui_hazel_helpers.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include <ImGuizmo.h>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace lumen::ui {
namespace {

static ::entt::entity g_material_path_entity { ::entt::null };
static char g_mat_albedo_path[512] {};
static char g_mat_normal_path[512] {};
static char g_mat_mr_path[512] {};
static char g_mat_ao_path[512] {};
static char g_mat_emissive_path[512] {};

[[nodiscard]] bool material_path_non_empty(const char *buf) {
    if (!buf || !buf[0]) {
        return false;
    }
    const char *p = buf;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return *p != '\0';
}

static const void *kCmpEntity { reinterpret_cast<const void *>(
    uintptr_t { 1 }) };
static const void *kCmpTransform { reinterpret_cast<const void *>(
    uintptr_t { 2 }) };
static const void *kCmpHierarchy { reinterpret_cast<const void *>(
    uintptr_t { 3 }) };
static const void *kCmpRendering { reinterpret_cast<const void *>(
    uintptr_t { 4 }) };
static const void *kCmpMaterial { reinterpret_cast<const void *>(
    uintptr_t { 5 }) };
static const void *kCmpLight { reinterpret_cast<const void *>(
    uintptr_t { 6 }) };

} // namespace

SceneInspectorPanel::SceneInspectorPanel(scene::Scene *scene,
                                         EditorSelection *selection)
    : scene_(scene), selection_(selection) {}

void SceneInspectorPanel::on_imgui_render() {
    if (!scene_ || !selection_) {
        return;
    }

    ImGui::Begin("Properties");
    ::entt::registry &reg = scene_->registry();
    const ::entt::entity e = selection_->entity;

    if (!reg.valid(e)) {
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }

    // Hazel：顶部实体名 + 同行「Add Component」
    if (auto *name = reg.try_get<lumen::scene::NameComponent>(e)) {
        char name_buf[256];
        std::snprintf(name_buf, sizeof(name_buf), "%s", name->name.c_str());
        if (ImGui::InputTextWithHint("##EntityName", "Name", name_buf,
                                     sizeof(name_buf))) {
            name->name = name_buf;
        }
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    ImGui::PopItemWidth();

    if (imgui_hazel_component_begin("Entity", kCmpEntity)) {
        if (const auto *oid = reg.try_get<lumen::scene::ObjectId>(e)) {
            ImGui::BeginDisabled();
            char idBuf[32];
            std::snprintf(idBuf, sizeof(idBuf), "%" PRIu32, oid->id);
            ImGui::InputText("Object ID", idBuf, sizeof(idBuf),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Pick / 序列化用 uint32，与 EnTT 句柄无关。");
        }
        ImGui::TreePop();
    }

    if (reg.all_of<lumen::scene::TransformComponent>(e)) {
        if (imgui_hazel_component_begin("Transform", kCmpTransform)) {
            auto &tr = reg.get<lumen::scene::TransformComponent>(e);
            glm::vec3 pos {};
            glm::vec3 rot_deg {};
            glm::vec3 scl {};
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(tr.matrix), glm::value_ptr(pos),
                glm::value_ptr(rot_deg), glm::value_ptr(scl));
            ImGui::TextDisabled("Local space");
            bool edited = false;
            edited |= imgui_hazel_draw_vec3("Position", pos, 0.0f, 100.0f,
                                            0.01f, "%.3f");
            edited |= imgui_hazel_draw_vec3("Rotation (deg)", rot_deg, 0.0f,
                                            100.0f, 0.5f, "%.1f");
            edited |= imgui_hazel_draw_vec3("Scale", scl, 1.0f, 100.0f, 0.01f,
                                            "%.3f", 1e-2f, 1e3f);
            if (edited) {
                ImGuizmo::RecomposeMatrixFromComponents(
                    glm::value_ptr(pos), glm::value_ptr(rot_deg),
                    glm::value_ptr(scl), glm::value_ptr(tr.matrix));
            }
            ImGui::TreePop();
        }
    }

    if (imgui_hazel_component_begin("Hierarchy", kCmpHierarchy)) {
        if (auto *par = reg.try_get<lumen::scene::ParentComponent>(e)) {
            if (par->parent != ::entt::null && reg.valid(par->parent)) {
                const auto *pn =
                    reg.try_get<lumen::scene::NameComponent>(par->parent);
                ImGui::Text(
                    "Parent: %s (%u)", pn ? pn->name.c_str() : "?",
                    static_cast<unsigned>(::entt::to_integral(par->parent)));
                if (ImGui::Button("Clear parent")) {
                    scene_->set_parent(e, ::entt::null);
                }
            } else {
                ImGui::TextUnformatted("Parent: (none)");
            }
        } else {
            ImGui::TextUnformatted("Parent: (none)");
        }
        ImGui::TreePop();
    }

    if (imgui_hazel_component_begin("Rendering", kCmpRendering)) {
        if (reg.all_of<lumen::scene::DrawableTag>(e)) {
            ImGui::TextUnformatted(
                "Drawable: enabled (mesh uses first drawable)");
            if (ImGui::Button("Remove Drawable")) {
                reg.remove<lumen::scene::DrawableTag>(e);
            }
        } else {
            ImGui::TextDisabled("No Drawable tag on this entity.");
            if (ImGui::Button("Add Drawable")) {
                reg.emplace<lumen::scene::DrawableTag>(e);
            }
        }
        ImGui::TreePop();
    }

    if (reg.all_of<lumen::scene::MaterialComponent>(e)) {
        if (imgui_hazel_component_begin("Material", kCmpMaterial)) {
            auto &mat = reg.get<lumen::scene::MaterialComponent>(e);
            if (e != g_material_path_entity) {
                g_material_path_entity = e;
                std::snprintf(g_mat_albedo_path, sizeof(g_mat_albedo_path),
                              "%s", mat.albedo_path.c_str());
                std::snprintf(g_mat_normal_path, sizeof(g_mat_normal_path),
                              "%s", mat.normal_path.c_str());
                std::snprintf(g_mat_mr_path, sizeof(g_mat_mr_path), "%s",
                              mat.metallic_roughness_path.c_str());
                std::snprintf(g_mat_ao_path, sizeof(g_mat_ao_path), "%s",
                              mat.ao_path.c_str());
                std::snprintf(g_mat_emissive_path, sizeof(g_mat_emissive_path),
                              "%s", mat.emissive_path.c_str());
            }
            ImGui::TextDisabled("标量与贴图二选一：路径非空则绑定贴图并与因子相"
                                "乘；空则仅用标量因子。");

            ImGui::Separator();
            ImGui::TextUnformatted("Alpha & faces");
            {
                int am = static_cast<int>(mat.alpha_mode);
                if (ImGui::Combo("Alpha mode", &am,
                                 "Opaque\0Alpha mask\0Alpha blend\0\0")) {
                    mat.alpha_mode =
                        static_cast<lumen::scene::MaterialAlphaMode>(am);
                }
                ImGui::DragFloat("Alpha cutoff (mask)", &mat.alpha_cutoff,
                                 0.01f, 0.0f, 1.0f, "%.2f");
                ImGui::Checkbox("Double sided", &mat.double_sided);
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Base color");
            ImGui::InputText("Albedo texture path", g_mat_albedo_path,
                             sizeof(g_mat_albedo_path));
            if (material_path_non_empty(g_mat_albedo_path)) {
                ImGui::TextDisabled("已启用反照率贴图（× Model color）；不再用 "
                                    "Base color 乘 RGB。");
            } else {
                ImGui::ColorEdit4("Base color (scalar)",
                                  glm::value_ptr(mat.base_color_factor));
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Normal");
            ImGui::InputText("Normal map path", g_mat_normal_path,
                             sizeof(g_mat_normal_path));
            if (!material_path_non_empty(g_mat_normal_path)) {
                ImGui::TextDisabled("无法线贴图：使用几何法线。");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Metallic / Roughness");
            ImGui::InputText("Metallic-Roughness texture path", g_mat_mr_path,
                             sizeof(g_mat_mr_path));
            if (material_path_non_empty(g_mat_mr_path)) {
                ImGui::TextDisabled("已启用 MR 贴图：B=金属，G=粗糙（glTF）。");
            } else {
                ImGui::DragFloat("Metallic (scalar)", &mat.metallic_factor,
                                 0.01f, 0.0f, 1.0f, "%.2f");
                ImGui::DragFloat("Roughness (scalar)", &mat.roughness_factor,
                                 0.01f, 0.04f, 1.0f, "%.2f");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Ambient occlusion");
            ImGui::InputText("AO texture path", g_mat_ao_path,
                             sizeof(g_mat_ao_path));
            if (material_path_non_empty(g_mat_ao_path)) {
                ImGui::TextDisabled("已启用 AO 贴图（R 通道）。");
            } else {
                ImGui::DragFloat("AO (scalar)", &mat.ao_factor, 0.01f, 0.0f,
                                 1.0f, "%.2f");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Emissive");
            ImGui::InputText("Emissive texture path", g_mat_emissive_path,
                             sizeof(g_mat_emissive_path));
            if (material_path_non_empty(g_mat_emissive_path)) {
                ImGui::TextDisabled("已启用自发光贴图。");
            } else {
                ImGui::DragFloat3("Emissive (scalar)",
                                  glm::value_ptr(mat.emissive_factor), 0.01f);
            }

            if (ImGui::Button("Apply material", ImVec2(-1.0f, 0.0f))) {
                mat.albedo_path = g_mat_albedo_path;
                mat.normal_path = g_mat_normal_path;
                mat.metallic_roughness_path = g_mat_mr_path;
                mat.ao_path = g_mat_ao_path;
                mat.emissive_path = g_mat_emissive_path;
                if (selection_) {
                    selection_->material_texture_reload_requested = true;
                }
            }
            if (ImGui::Button("Remove Material##mat", ImVec2(-1.0f, 0.0f))) {
                reg.remove<lumen::scene::MaterialComponent>(e);
                g_material_path_entity = ::entt::null;
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::LightComponent>(e)) {
        if (imgui_hazel_component_begin("Light", kCmpLight)) {
            auto &light = reg.get<lumen::scene::LightComponent>(e);
            int type_i = static_cast<int>(light.type);
            if (ImGui::Combo("Type", &type_i, "Directional\0Point\0Spot\0\0")) {
                light.type = static_cast<lumen::scene::LightType>(type_i);
            }
            ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.02f, 0.0f, 64.0f,
                             "%.2f");
            if (light.type == lumen::scene::LightType::Directional ||
                light.type == lumen::scene::LightType::Spot) {
                (void)imgui_hazel_draw_vec3("Direction (local)",
                                            light.local_direction, 0.0f, 120.0f,
                                            0.01f, "%.3f");
            }
            if (light.type == lumen::scene::LightType::Point ||
                light.type == lumen::scene::LightType::Spot) {
                ImGui::DragFloat("Range", &light.range, 0.05f, 0.01f, 256.0f,
                                 "%.2f");
            }
            if (light.type == lumen::scene::LightType::Spot) {
                float inner_deg = light.inner_radians * 57.2957795f;
                float outer_deg = light.outer_radians * 57.2957795f;
                if (ImGui::DragFloat("Inner cone (deg)", &inner_deg, 0.25f,
                                     0.1f, 89.0f, "%.1f")) {
                    light.inner_radians = inner_deg * 0.0174532925f;
                }
                if (ImGui::DragFloat("Outer cone (deg)", &outer_deg, 0.25f,
                                     0.2f, 90.0f, "%.1f")) {
                    light.outer_radians = outer_deg * 0.0174532925f;
                }
            }
            if (ImGui::Button("Remove Light")) {
                reg.remove<lumen::scene::LightComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!reg.all_of<lumen::scene::LightComponent>(e)) {
            if (ImGui::MenuItem("Light")) {
                reg.emplace<lumen::scene::LightComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::DrawableTag>(e)) {
            if (ImGui::MenuItem("Drawable")) {
                reg.emplace<lumen::scene::DrawableTag>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::TransformComponent>(e)) {
            if (ImGui::MenuItem("Transform")) {
                reg.emplace<lumen::scene::TransformComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::MaterialComponent>(e)) {
            if (ImGui::MenuItem("Material")) {
                reg.emplace<lumen::scene::MaterialComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace lumen::ui
