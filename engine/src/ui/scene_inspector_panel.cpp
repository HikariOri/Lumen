/**
 * @file scene_inspector_panel.cpp
 */

#include "ui/scene_inspector_panel.hpp"

#include "scene/components.hpp"
#include "scene/id_lookup.hpp"
#include "asset/geometry/mesh_asset.hpp"
#include "render/material/material.hpp"
#include "render/resource/texture.hpp"
#include "scene/scene.hpp"
#include "ui/editor_selection.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_hazel_helpers.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <optional>
#include <vector>

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
static const void *CMP_MESH_INSTANCE_REF { reinterpret_cast<const void *>(
    uintptr_t { 9 }) };
static const void *CMP_SUB_MESH_INSTANCE_REF { reinterpret_cast<const void *>(
    uintptr_t { 10 }) };
static const void *CMP_SCENE_MESH_HANDLE { reinterpret_cast<const void *>(
    uintptr_t { 11 }) };

std::vector<void *> g_mat_preview_tex_ids {};

void clear_material_texture_previews() {
    for (void *id : g_mat_preview_tex_ids) {
        if (id != nullptr) {
            imgui_backend_remove_texture(id);
        }
    }
    g_mat_preview_tex_ids.clear();
}

[[nodiscard]] const char *
alpha_mode_label(const lumen::render::MaterialAlphaMode m) {
    using lumen::render::MaterialAlphaMode;
    switch (m) {
    case MaterialAlphaMode::Opaque:
        return "Opaque";
    case MaterialAlphaMode::Mask:
        return "Mask";
    case MaterialAlphaMode::Blend:
        return "Blend";
    }
    return "?";
}

void draw_texture_slot_preview(const char *slot_label,
                               const lumen::render::Texture *tex,
                               const float thumb_px) {
    ImGui::BulletText("%s", slot_label);
    ImGui::Indent();
    if (tex == nullptr || !tex->is_valid()) {
        ImGui::TextDisabled("（无 / 引擎默认占位）");
        ImGui::Unindent();
        return;
    }
    void *const tid = imgui_backend_add_texture(
        tex->sampler(), tex->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (tid == nullptr) {
        ImGui::TextDisabled("ImGui 注册失败");
        ImGui::Unindent();
        return;
    }
    g_mat_preview_tex_ids.push_back(tid);
    ImGui::Image(reinterpret_cast<ImTextureID>(tid),
                 ImVec2(thumb_px, thumb_px));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%u × %u", tex->width(), tex->height());
    ImGui::Unindent();
}

void draw_pbr_material_inspector(const lumen::render::Material *effective,
                                 const lumen::render::Material *prim_mat,
                                 const lumen::render::Material *override_mat) {
    if (effective == nullptr) {
        ImGui::TextDisabled("无可解析材质");
        return;
    }

    if (override_mat != nullptr) {
        ImGui::TextDisabled("来源: Material override");
    } else if (prim_mat != nullptr) {
        ImGui::TextDisabled("来源: Primitive 默认材质");
    } else {
        ImGui::TextDisabled("来源: （未绑定）");
    }

    constexpr ImGuiColorEditFlags k_ro_color = ImGuiColorEditFlags_Float |
                                               ImGuiColorEditFlags_NoInputs |
                                               ImGuiColorEditFlags_NoPicker;

    glm::vec4 bc = effective->baseColorFactor;
    ImGui::TextUnformatted("Base color（因子）");
    ImGui::SameLine();
    ImGui::ColorEdit4("##submat_bc", glm::value_ptr(bc), k_ro_color);

    ImGui::Text("Metallic %.3f   Roughness %.3f", effective->metallicFactor,
                effective->roughnessFactor);

    const glm::vec3 em = effective->emissiveFactor;
    ImGui::Text("Emissive 因子 (linear): %.3f, %.3f, %.3f", em.x, em.y, em.z);
    glm::vec3 em_vis = glm::clamp(em, 0.0F, 1.0F);
    ImGui::SameLine();
    ImGui::ColorEdit3("##submat_em", glm::value_ptr(em_vis), k_ro_color);

    ImGui::Text("AO strength %.3f", effective->occlusionStrength);
    ImGui::Text("Alpha: %s", alpha_mode_label(effective->alphaMode));
    ImGui::Text("Double sided: %s", effective->doubleSided ? "是" : "否");

    constexpr float k_thumb { 64.0F };
    if (ImGui::TreeNodeEx("PBR 贴图预览",
                          ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_texture_slot_preview("Albedo（sRGB）", effective->baseColorTex,
                                  k_thumb);
        draw_texture_slot_preview("Metallic-Roughness", effective->metallicRoughnessTex,
                                  k_thumb);
        draw_texture_slot_preview("Normal", effective->normalTex, k_thumb);
        draw_texture_slot_preview("Occlusion", effective->occlusionTex, k_thumb);
        draw_texture_slot_preview("Emissive", effective->emissiveTex, k_thumb);
        ImGui::TreePop();
    }
}

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

    clear_material_texture_previews();

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
                            "[%zu] indexCount=%u %s", i, p.indexCount,
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
                    ImGui::Text("该 primitive: indexCount=%u %s",
                                p.indexCount,
                                p.is_drawable() ? "可绘制" : "不可绘制");
                    const lumen::render::Material *const prim_mat =
                        p.material;
                    const lumen::render::Material *const ov =
                        subMeshRenderer.materialOverride;
                    const lumen::render::Material *const eff =
                        ov != nullptr ? ov : prim_mat;
                    if (ImGui::TreeNodeEx("材质",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                        draw_pbr_material_inspector(eff, prim_mat, ov);
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.2F, 1.0F),
                                       "下标越界，绘制时会被跳过");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.2F, 1.0F),
                                   "mesh 为空指针");
            }
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

    if (reg.all_of<lumen::scene::MeshInstanceRefRendererComponent>(e)) {
        if (imgui_hazel_component_begin("Mesh Renderer (InstanceRef)",
                                       CMP_MESH_INSTANCE_REF)) {
            auto &c =
                reg.get<lumen::scene::MeshInstanceRefRendererComponent>(e);
            const std::optional<lumen::asset::MeshInstanceRef::Resolved> rr =
                c.meshRef.resolve();
            ImGui::Text("meshIndex: %u", c.meshRef.meshIndex);
            ImGui::Text("scene expired: %s",
                        c.meshRef.scene.expired() ? "yes" : "no");
            if (rr.has_value() && rr->mesh != nullptr) {
                const auto &prims = rr->mesh->primitives;
                std::size_t drawable = 0;
                for (const auto &p : prims) {
                    if (p.is_drawable()) {
                        ++drawable;
                    }
                }
                ImGui::Text("Primitives: %zu（可绘制 %zu）", prims.size(),
                            drawable);
            } else {
                ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.2F, 1.0F),
                                   "无法解析（资产已卸载或 meshIndex 无效）");
            }
            ImGui::TextDisabled(
                "收集：`append_mesh_instance_ref_renderer_render_items`。");
            if (ImGui::Button("Remove##meshinstanceref",
                              ImVec2(-1.0F, 0.0F))) {
                reg.remove<lumen::scene::MeshInstanceRefRendererComponent>(e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::SubMeshInstanceRefRendererComponent>(e)) {
        if (imgui_hazel_component_begin("SubMesh Renderer (InstanceRef)",
                                       CMP_SUB_MESH_INSTANCE_REF)) {
            auto &c =
                reg.get<lumen::scene::SubMeshInstanceRefRendererComponent>(e);
            ImGui::Text("meshIndex: %u",
                        c.submeshRef.meshInstance.meshIndex);
            ImGui::DragScalar("Primitive index", ImGuiDataType_U32,
                              &c.submeshRef.primitiveIndex, 0.25F, nullptr,
                              nullptr);
            const std::optional<lumen::asset::SubMeshInstanceRef::ResolvedPrim>
                rp = c.submeshRef.resolve();
            if (rp.has_value()) {
                ImGui::Text("该 primitive: indexCount=%u %s",
                            rp->primitive->indexCount,
                            rp->primitive->is_drawable() ? "可绘制" : "不可绘制");
                const lumen::render::Material *const prim_mat =
                    rp->primitive->material;
                const lumen::render::Material *const ov = c.materialOverride;
                const lumen::render::Material *const eff =
                    ov != nullptr ? ov : prim_mat;
                if (ImGui::TreeNodeEx("材质",
                                      ImGuiTreeNodeFlags_DefaultOpen)) {
                    draw_pbr_material_inspector(eff, prim_mat, ov);
                    ImGui::TreePop();
                }
            } else {
                ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.2F, 1.0F),
                                   "无法解析（资产已卸载或下标无效）");
            }
            ImGui::TextDisabled("收集：`append_submesh_instance_ref_render_items`。");
            if (ImGui::Button("Remove##submeshinstanceref",
                              ImVec2(-1.0F, 0.0F))) {
                reg.remove<lumen::scene::SubMeshInstanceRefRendererComponent>(
                    e);
            }
            ImGui::TreePop();
        }
    }

    if (reg.all_of<lumen::scene::SceneMeshAssetHandleComponent>(e)) {
        if (imgui_hazel_component_begin("Scene mesh (AssetManager)",
                                       CMP_SCENE_MESH_HANDLE)) {
            const auto &c =
                reg.get<lumen::scene::SceneMeshAssetHandleComponent>(e);
            ImGui::BeginDisabled();
            char hbuf[32];
            std::snprintf(hbuf, sizeof(hbuf), "%" PRIu64,
                          static_cast<std::uint64_t>(c.handle.id));
            ImGui::InputText("AssetId", hbuf, sizeof(hbuf),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "与 `AssetManager::try_get_scene_mesh(handle)` 对应。");
            if (ImGui::Button("Remove##scenemeshhandle",
                              ImVec2(-1.0F, 0.0F))) {
                reg.remove<lumen::scene::SceneMeshAssetHandleComponent>(e);
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
        if (!reg.all_of<lumen::scene::MeshInstanceRefRendererComponent>(e)) {
            if (ImGui::MenuItem("Mesh Renderer (InstanceRef)")) {
                reg.emplace<lumen::scene::MeshInstanceRefRendererComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::SubMeshInstanceRefRendererComponent>(e)) {
            if (ImGui::MenuItem("SubMesh Renderer (InstanceRef)")) {
                reg.emplace<
                    lumen::scene::SubMeshInstanceRefRendererComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        if (!reg.all_of<lumen::scene::SceneMeshAssetHandleComponent>(e)) {
            if (ImGui::MenuItem("Scene mesh handle (debug)")) {
                reg.emplace<lumen::scene::SceneMeshAssetHandleComponent>(e);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace lumen::ui
