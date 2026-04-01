/**
 * @file scene_inspector_panel.cpp
 */

#include "ui/scene_inspector_panel.hpp"

#include "scene/components.hpp"
#include "scene/id_lookup.hpp"
#include "scene/mesh.hpp"
#include "scene/scene.hpp"
#include "ui/editor_selection.hpp"
#include "ui/imgui_hazel_helpers.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include <algorithm>

#include <ImGuizmo.h>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace lumen::ui {
namespace {

static const void *CMP_ENTITY { reinterpret_cast<const void *>(
    uintptr_t { 1 }) };
static const void *CMP_TRANSFORM { reinterpret_cast<const void *>(
    uintptr_t { 2 }) };
static const void *CMP_HIERARCHY { reinterpret_cast<const void *>(
    uintptr_t { 3 }) };
static const void *CMP_DIR_LIGHT { reinterpret_cast<const void *>(
    uintptr_t { 4 }) };
static const void *CMP_POINT_LIGHT { reinterpret_cast<const void *>(
    uintptr_t { 5 }) };
static const void *CMP_SPOT_LIGHT { reinterpret_cast<const void *>(
    uintptr_t { 6 }) };
static const void *CMP_SUB_MESH { reinterpret_cast<const void *>(
    uintptr_t { 7 }) };
static const void *CMP_MESH_RENDERER { reinterpret_cast<const void *>(
    uintptr_t { 8 }) };

void remove_light_components(entt::registry &reg, entt::entity ent) {
    if (reg.all_of<lumen::scene::DirectionalLightComponent>(ent)) {
        reg.remove<lumen::scene::DirectionalLightComponent>(ent);
    }
    if (reg.all_of<lumen::scene::PointLightComponent>(ent)) {
        reg.remove<lumen::scene::PointLightComponent>(ent);
    }
    if (reg.all_of<lumen::scene::SpotLightComponent>(ent)) {
        reg.remove<lumen::scene::SpotLightComponent>(ent);
    }
}

} // namespace

SceneInspectorPanel::SceneInspectorPanel(scene::Scene *scene,
                                         EditorSelection *selection)
    : scene_(scene), selection_(selection) {}

void SceneInspectorPanel::on_imgui_render() {
    if (!scene_ || !selection_) {
        return;
    }

    ImGui::Begin("Properties");
    entt::registry &reg = scene_->registry();
    const entt::entity e = selection_->entity;

    if (!reg.valid(e)) {
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }

    // Hazel：顶部实体标签 + 同行「Add Component」
    if (auto *tc = reg.try_get<lumen::scene::TagComponent>(e)) {
        char tag_buf[256];
        std::snprintf(tag_buf, sizeof(tag_buf), "%s", tc->tag.c_str());
        if (ImGui::InputTextWithHint("##EntityTag", "Tag", tag_buf,
                                     sizeof(tag_buf))) {
            tc->tag = tag_buf;
        }
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(-1.0f);
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    ImGui::PopItemWidth();

    if (imgui_hazel_component_begin("Entity", CMP_ENTITY)) {
        if (const auto *idc = reg.try_get<lumen::scene::IDComponent>(e)) {
            ImGui::BeginDisabled();
            char idBuf[32];
            std::snprintf(idBuf, sizeof(idBuf), "%" PRIu64,
                          static_cast<std::uint64_t>(idc->id));
            ImGui::InputText("ID", idBuf, sizeof(idBuf),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::TextDisabled("uint64 稳定身份；与 EnTT 句柄无关。");
        }
        ImGui::TreePop();
    }

    if (reg.all_of<lumen::scene::TransformComponent>(e)) {
        if (imgui_hazel_component_begin("Transform", CMP_TRANSFORM)) {
            auto &tr = reg.get<lumen::scene::TransformComponent>(e);
            glm::mat4 local_mat = tr.get_transform();
            glm::vec3 pos {};
            glm::vec3 rot_deg {};
            glm::vec3 scl {};
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(local_mat), glm::value_ptr(pos),
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
                    glm::value_ptr(scl), glm::value_ptr(local_mat));
                tr.set_transform(local_mat);
            }
            ImGui::TreePop();
        }
    }

    if (imgui_hazel_component_begin("Hierarchy", CMP_HIERARCHY)) {
        if (auto *par = reg.try_get<lumen::scene::RelationshipComponent>(e)) {
            if (par->parent != lumen::core::INVALID_ID) {
                const auto p_ent =
                    lumen::scene::find_entity_with_id(reg, par->parent);
                if (p_ent && reg.valid(*p_ent)) {
                    const auto *pn =
                        reg.try_get<lumen::scene::TagComponent>(*p_ent);
                    ImGui::Text("Parent: %s (id=%" PRIu64 ")",
                                pn ? pn->tag.c_str() : "?",
                                static_cast<std::uint64_t>(par->parent));
                    if (ImGui::Button("Clear parent")) {
                        scene_->set_parent(e, entt::null);
                    }
                } else {
                    ImGui::Text("Parent: broken id=%" PRIu64,
                                static_cast<std::uint64_t>(par->parent));
                    if (ImGui::Button("Clear parent")) {
                        scene_->set_parent(e, entt::null);
                    }
                }
            } else {
                ImGui::TextUnformatted("Parent: (none)");
            }
        } else {
            ImGui::TextUnformatted("Parent: (none)");
        }
        ImGui::TreePop();
    }

    if (reg.all_of<lumen::scene::MeshRendererComponent>(e)) {
        if (imgui_hazel_component_begin("Mesh Renderer", CMP_MESH_RENDERER)) {
            auto &meshRenderer =
                reg.get<lumen::scene::MeshRendererComponent>(e);
            ImGui::Text("Mesh: %p",
                        static_cast<const void *>(meshRenderer.mesh));
            if (meshRenderer.mesh != nullptr) {
                const auto &prims = meshRenderer.mesh->primitives;
                std::size_t drawable = 0;
                for (const auto &p : prims) {
                    if (p.is_drawable()) {
                        ++drawable;
                    }
                }
                ImGui::Text("Primitives: %zu（可绘制 %zu）", prims.size(),
                            drawable);
                if (!prims.empty() &&
                    ImGui::TreeNodeEx("Primitive 摘要",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
                    const std::size_t showMax =
                        (std::min)(prims.size(), std::size_t { 24 });
                    for (std::size_t i = 0; i < showMax; ++i) {
                        const auto &p = prims[i];
                        ImGui::BulletText(
                            "[%zu] index_count=%u %s", i, p.index_count,
                            p.is_drawable() ? "可绘制" : "跳过");
                    }
                    if (prims.size() > showMax) {
                        ImGui::TextDisabled("… 其余 %zu 项未展开",
                                            prims.size() - showMax);
                    }
                    ImGui::TreePop();
                }
            } else {
                ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.2F, 1.0F),
                                   "mesh 为空指针");
            }
            ImGui::TextDisabled(
                "整网绘制：append_mesh_render_items(meshBuffer, *mesh, …)。");
            if (ImGui::Button("Remove##meshrenderer", ImVec2(-1.0F, 0.0F))) {
                reg.remove<lumen::scene::MeshRendererComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::SubMeshRendererComponent>(e)) {
        if (imgui_hazel_component_begin("SubMesh Renderer", CMP_SUB_MESH)) {
            auto &subMeshRenderer =
                reg.get<lumen::scene::SubMeshRendererComponent>(e);
            ImGui::Text("Mesh: %p",
                        static_cast<const void *>(subMeshRenderer.mesh));
            const std::uint32_t primCount =
                subMeshRenderer.mesh != nullptr
                    ? static_cast<std::uint32_t>(
                          subMeshRenderer.mesh->primitives.size())
                    : 0U;
            ImGui::DragScalar("Primitive index", ImGuiDataType_U32,
                              &subMeshRenderer.primitiveIndex, 0.25F, nullptr,
                              nullptr);
            if (subMeshRenderer.mesh != nullptr) {
                const bool inRange =
                    subMeshRenderer.primitiveIndex < primCount;
                ImGui::Text("有效下标范围: [0, %u)", primCount);
                if (inRange) {
                    const auto &p = subMeshRenderer
                                        .mesh->primitives[subMeshRenderer
                                                              .primitiveIndex];
                    ImGui::Text("该 primitive: index_count=%u %s",
                                p.index_count,
                                p.is_drawable() ? "可绘制" : "不可绘制");
                } else {
                    ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.2F, 1.0F),
                                       "下标越界，绘制时会被跳过");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.2F, 1.0F),
                                   "mesh 为空指针");
            }
            ImGui::Text("Material override: %p",
                        static_cast<const void *>(
                            subMeshRenderer.materialOverride));
            ImGui::TextDisabled(
                "世界矩阵 = 父链 × 局部 Transform；收集绘制见 "
                "append_submesh_render_items。");
            if (ImGui::Button("Remove##submeshrenderer",
                              ImVec2(-1.0F, 0.0F))) {
                reg.remove<lumen::scene::SubMeshRendererComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::DirectionalLightComponent>(e)) {
        if (imgui_hazel_component_begin("Directional Light", CMP_DIR_LIGHT)) {
            auto &L = reg.get<lumen::scene::DirectionalLightComponent>(e);
            ImGui::ColorEdit3("Radiance (linear)", glm::value_ptr(L.radiance),
                              ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Intensity", &L.intensity, 0.02f, 0.0f, 256.0f,
                             "%.2f");
            ImGui::Checkbox("Cast shadows", &L.castShadows);
            ImGui::Checkbox("Soft shadows", &L.softShadows);
            ImGui::DragFloat("Light size (PCSS)", &L.lightSize, 0.01f, 0.0f,
                             32.0f, "%.2f");
            ImGui::DragFloat("Shadow amount", &L.shadowAmount, 0.01f, 0.0f,
                             1.0f, "%.2f");
            ImGui::TextDisabled("方向由 Transform 与世界矩阵推导（见 light.cpp）。");
            if (ImGui::Button("Remove##dirlight", ImVec2(-1.0f, 0.0f))) {
                reg.remove<lumen::scene::DirectionalLightComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::PointLightComponent>(e)) {
        if (imgui_hazel_component_begin("Point Light", CMP_POINT_LIGHT)) {
            auto &L = reg.get<lumen::scene::PointLightComponent>(e);
            ImGui::ColorEdit3("Radiance (linear)", glm::value_ptr(L.radiance),
                              ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Intensity", &L.intensity, 0.02f, 0.0f, 256.0f,
                             "%.2f");
            ImGui::DragFloat("Light size", &L.lightSize, 0.01f, 0.0f, 32.0f,
                             "%.2f");
            ImGui::DragFloat("Min radius", &L.minRadius, 0.02f, 0.01f, 256.0f,
                             "%.2f");
            ImGui::DragFloat("Radius", &L.radius, 0.05f, 0.01f, 512.0f, "%.2f");
            ImGui::Checkbox("Cast shadows", &L.castShadows);
            ImGui::Checkbox("Soft shadows", &L.softShadows);
            ImGui::DragFloat("Falloff", &L.falloff, 0.02f, 0.0f, 8.0f, "%.2f");
            ImGui::TextDisabled("世界位置由 Transform（及父层级）决定。");
            if (ImGui::Button("Remove##pointlight", ImVec2(-1.0f, 0.0f))) {
                reg.remove<lumen::scene::PointLightComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::SpotLightComponent>(e)) {
        if (imgui_hazel_component_begin("Spot Light", CMP_SPOT_LIGHT)) {
            auto &L = reg.get<lumen::scene::SpotLightComponent>(e);
            ImGui::ColorEdit3("Radiance (linear)", glm::value_ptr(L.radiance),
                              ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Intensity", &L.intensity, 0.02f, 0.0f, 256.0f,
                             "%.2f");
            ImGui::DragFloat("Range", &L.range, 0.05f, 0.01f, 512.0f, "%.2f");
            ImGui::DragFloat("Cone angle (deg)", &L.angle, 0.25f, 1.0f, 179.0f,
                             "%.1f");
            ImGui::DragFloat("Angle attenuation", &L.angleAttenuation, 0.05f,
                             0.01f, 32.0f, "%.2f");
            ImGui::Checkbox("Cast shadows", &L.castShadows);
            ImGui::Checkbox("Soft shadows", &L.softShadows);
            ImGui::DragFloat("Falloff", &L.falloff, 0.02f, 0.0f, 8.0f, "%.2f");
            ImGui::TextDisabled("位置与锥轴（局部 -Z）由 Transform 决定。");
            if (ImGui::Button("Remove##spotlight", ImVec2(-1.0f, 0.0f))) {
                reg.remove<lumen::scene::SpotLightComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        if (!reg.all_of<lumen::scene::TransformComponent>(e)) {
            if (ImGui::MenuItem("Transform")) {
                reg.emplace<lumen::scene::TransformComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        const bool has_dir = reg.all_of<lumen::scene::DirectionalLightComponent>(e);
        const bool has_point = reg.all_of<lumen::scene::PointLightComponent>(e);
        const bool has_spot = reg.all_of<lumen::scene::SpotLightComponent>(e);
        if (ImGui::MenuItem("Directional Light", nullptr, has_dir)) {
            if (!has_dir) {
                remove_light_components(reg, e);
                reg.emplace<lumen::scene::DirectionalLightComponent>(e);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Point Light", nullptr, has_point)) {
            if (!has_point) {
                remove_light_components(reg, e);
                reg.emplace<lumen::scene::PointLightComponent>(e);
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("Spot Light", nullptr, has_spot)) {
            if (!has_spot) {
                remove_light_components(reg, e);
                reg.emplace<lumen::scene::SpotLightComponent>(e);
            }
            ImGui::CloseCurrentPopup();
        }
        if (!reg.all_of<lumen::scene::MeshRendererComponent>(e)) {
            if (ImGui::MenuItem("Mesh Renderer")) {
                reg.emplace<lumen::scene::MeshRendererComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::SubMeshRendererComponent>(e)) {
            if (ImGui::MenuItem("SubMesh Renderer")) {
                reg.emplace<lumen::scene::SubMeshRendererComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace lumen::ui
