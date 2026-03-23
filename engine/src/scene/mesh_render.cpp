/**
 * @file mesh_render.cpp
 */

#include "scene/mesh_render.hpp"

#include <algorithm>

namespace lumen::scene {
namespace {

[[nodiscard]] std::uint32_t compute_dominant_slot(
    const std::vector<lumen::core::GltfSubmeshRange> &ranges,
                        std::size_t num_materials) {
    if (ranges.empty() || num_materials == 0) {
        return 0;
    }
    std::vector<std::size_t> tri_per_mat(num_materials, 0);
    for (const auto &r : ranges) {
        if (r.material_index >= 0 &&
            static_cast<std::size_t>(r.material_index) < num_materials) {
            tri_per_mat[static_cast<std::size_t>(r.material_index)] +=
                static_cast<std::size_t>(r.index_count) / 3u;
        }
    }
    std::uint32_t best = 0;
    std::size_t best_tri = 0;
    for (std::size_t i = 0; i < tri_per_mat.size(); ++i) {
        if (tri_per_mat[i] > best_tri) {
            best_tri = tri_per_mat[i];
            best = static_cast<std::uint32_t>(i);
        }
    }
    return best;
}

[[nodiscard]] std::uint64_t
pipeline_batch_key(const MaterialComponent &m) {
    std::uint64_t k = static_cast<std::uint64_t>(m.alpha_mode) & 3u;
    k |= m.double_sided ? (1ull << 4) : 0;
    const std::hash<std::string> h;
    k ^= static_cast<std::uint64_t>(h(m.albedo_path)) << 8;
    k ^= static_cast<std::uint64_t>(h(m.metallic_roughness_path)) << 24;
    return k;
}

} // namespace

void populate_mesh_from_gltf_data(
    MeshComponent &mesh, MeshMaterialSlotsComponent &slots,
    const std::vector<core::GltfSubmeshRange> &ranges,
    const std::vector<MaterialComponent> &materials) {
    mesh.submeshes.clear();
    slots.slots.clear();
    mesh.dominant_material_slot = 0;

    if (ranges.empty() || materials.empty()) {
        return;
    }

    mesh.submeshes.reserve(ranges.size());
    for (const auto &r : ranges) {
        SubMeshSlice s;
        s.first_index = r.first_index;
        s.index_count = r.index_count;
        if (r.material_index >= 0 &&
            static_cast<std::size_t>(r.material_index) < materials.size()) {
            s.material_slot =
                static_cast<std::uint32_t>(r.material_index);
        } else {
            s.material_slot = 0;
        }
        mesh.submeshes.push_back(s);
    }

    slots.slots.reserve(materials.size());
    for (const auto &m : materials) {
        MaterialInstance mi;
        mi.source_asset_id.clear();
        mi.resolved = m;
        slots.slots.push_back(std::move(mi));
    }

    mesh.dominant_material_slot =
        compute_dominant_slot(ranges, materials.size());
}

void sync_material_component_to_dominant_slot(
    const MaterialComponent &primary, MeshComponent &mesh,
    MeshMaterialSlotsComponent *slots) {
    if (slots == nullptr || slots->slots.empty()) {
        return;
    }
    const std::uint32_t i = mesh.dominant_material_slot;
    if (i < slots->slots.size()) {
        slots->slots[i].resolved = primary;
    }
}

void build_submesh_batch_order(const MeshComponent &mesh,
                               const MeshMaterialSlotsComponent &slots,
                               std::vector<std::uint32_t> &out_submesh_indices) {
    out_submesh_indices.clear();
    const std::size_t n = mesh.submeshes.size();
    out_submesh_indices.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        out_submesh_indices[i] = i;
    }

    const auto mat_for = [&](std::uint32_t sub_i) -> const MaterialComponent & {
        const std::uint32_t slot = mesh.submeshes[sub_i].material_slot;
        if (slot < slots.slots.size()) {
            return slots.slots[slot].resolved;
        }
        static const MaterialComponent kDefault {};
        return kDefault;
    };

    std::sort(out_submesh_indices.begin(), out_submesh_indices.end(),
              [&](std::uint32_t a, std::uint32_t b) {
                  const std::uint64_t ka = pipeline_batch_key(mat_for(a));
                  const std::uint64_t kb = pipeline_batch_key(mat_for(b));
                  if (ka != kb) {
                      return ka < kb;
                  }
                  return a < b;
              });
}

} // namespace lumen::scene
