/**
 * @file gltf_loader.cpp
 * @brief glTF 2.0 / GLB 加载（Assimp），几何与 PBR 材质路径
 */

#include "core/gltf_loader.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"

#include "scene/components.hpp"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <string>
#include <string_view>
#include <vector>

#include <ghc/filesystem.hpp>

namespace lumen {
namespace core {

namespace {

namespace fs = ghc::filesystem;

std::string resource_relative_path(const fs::path &absolute_path) {
    std::error_code ec;
    const fs::path root { lumen::core::get_resource_path("") };
    fs::path rel = fs::relative(absolute_path, root, ec);
    if (ec) {
        return absolute_path.generic_string();
    }
    return rel.generic_string();
}

glm::mat4 ai_matrix_to_glm(const aiMatrix4x4 &m) {
    return glm::mat4(static_cast<float>(m.a1), static_cast<float>(m.b1),
                     static_cast<float>(m.c1), static_cast<float>(m.d1),
                     static_cast<float>(m.a2), static_cast<float>(m.b2),
                     static_cast<float>(m.c2), static_cast<float>(m.d2),
                     static_cast<float>(m.a3), static_cast<float>(m.b3),
                     static_cast<float>(m.c3), static_cast<float>(m.d3),
                     static_cast<float>(m.a4), static_cast<float>(m.b4),
                     static_cast<float>(m.c4), static_cast<float>(m.d4));
}

void append_ai_mesh(const aiMesh *mesh, const glm::mat4 &world, ObjMesh &out,
                    std::vector<GltfSubmeshRange> *ranges) {
    const glm::mat3 n_world =
        glm::transpose(glm::inverse(glm::mat3(world)));

    const std::uint32_t submesh_first_index =
        ranges != nullptr ? static_cast<std::uint32_t>(out.indices.size()) : 0u;
    const std::uint32_t vbase =
        static_cast<std::uint32_t>(out.vertices.size());

    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D &p = mesh->mVertices[i];
        const glm::vec3 wp =
            glm::vec3(world * glm::vec4(p.x, p.y, p.z, 1.F));

        glm::vec3 n { 0.F, 1.F, 0.F };
        if (mesh->HasNormals()) {
            const aiVector3D &ln = mesh->mNormals[i];
            const glm::vec3 ln_g { ln.x, ln.y, ln.z };
            n = glm::normalize(n_world * ln_g);
        }

        glm::vec2 uv { 0.F };
        if (mesh->mTextureCoords[0] != nullptr) {
            const aiVector3D &t = mesh->mTextureCoords[0][i];
            uv = glm::vec2(t.x, t.y);
        }

        out.vertices.push_back(ObjVertex { wp, n, uv });
    }

    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace &face = mesh->mFaces[f];
        if (face.mNumIndices != 3) {
            continue;
        }
        for (unsigned j = 0; j < 3; ++j) {
            out.indices.push_back(vbase + face.mIndices[j]);
        }
    }

    if (ranges != nullptr) {
        const std::uint32_t total =
            static_cast<std::uint32_t>(out.indices.size());
        if (total > submesh_first_index) {
            ranges->push_back(GltfSubmeshRange { submesh_first_index,
                                                 total - submesh_first_index,
                                                 static_cast<int>(mesh->mMaterialIndex) });
        }
    }
}

void process_node(const aiNode *node, const aiMatrix4x4 &parent,
                  const aiScene *scene, ObjMesh &out,
                  std::vector<GltfSubmeshRange> *ranges) {
    const aiMatrix4x4 transform = parent * node->mTransformation;
    const glm::mat4 world = ai_matrix_to_glm(transform);

    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        append_ai_mesh(mesh, world, out, ranges);
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c) {
        process_node(node->mChildren[c], transform, scene, out, ranges);
    }
}

void set_texture_path_from_material(const aiMaterial *mat,
                                    aiTextureType type, unsigned tex_index,
                                    const fs::path &model_dir,
                                    std::string &out_path) {
    aiString path;
    if (mat->GetTexture(type, tex_index, &path) != AI_SUCCESS) {
        return;
    }
    if (path.length == 0) {
        return;
    }
    const char *s = path.C_Str();
    if (s[0] == '*') {
        return;
    }
    const fs::path full = fs::absolute(model_dir / fs::path(s));
    out_path = resource_relative_path(full.lexically_normal());
}

void fill_material_assimp(const aiMaterial *mat, const fs::path &model_dir,
                          scene::MaterialComponent &out) {
    out = scene::MaterialComponent {};
    out.spec_gloss_texture_in_mr_slot = false;

    aiColor4D base { 1.F, 1.F, 1.F, 1.F };
    if (aiGetMaterialColor(mat, AI_MATKEY_BASE_COLOR, &base) == AI_SUCCESS) {
        out.base_color_factor = glm::vec4(static_cast<float>(base.r),
                                          static_cast<float>(base.g),
                                          static_cast<float>(base.b),
                                          static_cast<float>(base.a));
    } else if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &base) ==
               AI_SUCCESS) {
        out.base_color_factor = glm::vec4(static_cast<float>(base.r),
                                          static_cast<float>(base.g),
                                          static_cast<float>(base.b),
                                          static_cast<float>(base.a));
    }

    ai_real metallic = static_cast<ai_real>(1.0);
    if (aiGetMaterialFloat(mat, AI_MATKEY_METALLIC_FACTOR, &metallic) ==
        AI_SUCCESS) {
        out.metallic_factor = static_cast<float>(metallic);
    }
    ai_real roughness = static_cast<ai_real>(1.0);
    if (aiGetMaterialFloat(mat, AI_MATKEY_ROUGHNESS_FACTOR, &roughness) ==
        AI_SUCCESS) {
        out.roughness_factor = static_cast<float>(roughness);
    }

    aiColor4D emis { 0.F, 0.F, 0.F, 1.F };
    if (aiGetMaterialColor(mat, AI_MATKEY_COLOR_EMISSIVE, &emis) ==
        AI_SUCCESS) {
        out.emissive_factor = glm::vec3(static_cast<float>(emis.r),
                                        static_cast<float>(emis.g),
                                        static_cast<float>(emis.b));
    }

    int two_sided = 0;
    if (aiGetMaterialInteger(mat, AI_MATKEY_TWOSIDED, &two_sided) ==
        AI_SUCCESS) {
        out.double_sided = two_sided != 0;
    }

    aiString alpha_mode {};
    if (aiGetMaterialString(mat, AI_MATKEY_GLTF_ALPHAMODE, &alpha_mode) ==
        AI_SUCCESS) {
        const std::string_view sv { alpha_mode.C_Str() };
        if (sv == "MASK") {
            out.alpha_mode = scene::MaterialAlphaMode::Mask;
        } else if (sv == "BLEND") {
            out.alpha_mode = scene::MaterialAlphaMode::Blend;
        } else {
            out.alpha_mode = scene::MaterialAlphaMode::Opaque;
        }
    }

    ai_real cutoff = static_cast<ai_real>(0.5);
    if (aiGetMaterialFloat(mat, AI_MATKEY_GLTF_ALPHACUTOFF, &cutoff) ==
        AI_SUCCESS) {
        out.alpha_cutoff = static_cast<float>(cutoff);
    }

    set_texture_path_from_material(mat, aiTextureType_BASE_COLOR, 0,
                                   model_dir, out.albedo_path);
    if (out.albedo_path.empty()) {
        set_texture_path_from_material(mat, aiTextureType_DIFFUSE, 0,
                                       model_dir, out.albedo_path);
    }

    set_texture_path_from_material(
        mat, aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, model_dir,
        out.metallic_roughness_path);
    if (out.metallic_roughness_path.empty()) {
        set_texture_path_from_material(mat, aiTextureType_METALNESS, 0,
                                       model_dir, out.metallic_roughness_path);
    }

    set_texture_path_from_material(mat, aiTextureType_NORMALS, 0, model_dir,
                                   out.normal_path);
    if (out.normal_path.empty()) {
        set_texture_path_from_material(mat, aiTextureType_HEIGHT, 0, model_dir,
                                       out.normal_path);
    }

    set_texture_path_from_material(mat, aiTextureType_AMBIENT_OCCLUSION, 0,
                                   model_dir, out.ao_path);
    if (out.ao_path.empty()) {
        set_texture_path_from_material(mat, aiTextureType_LIGHTMAP, 0,
                                       model_dir, out.ao_path);
    }

    set_texture_path_from_material(mat, aiTextureType_EMISSIVE, 0, model_dir,
                                   out.emissive_path);
}

int find_first_material_index(const aiNode *node, const aiScene *scene) {
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        return static_cast<int>(mesh->mMaterialIndex);
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c) {
        const int r = find_first_material_index(node->mChildren[c], scene);
        if (r >= 0) {
            return r;
        }
    }
    return -1;
}

constexpr unsigned k_assimp_gltf_flags =
    aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_GenSmoothNormals;

} // namespace

bool load_gltf(const std::string_view filePath, ObjMesh &outMesh,
               scene::MaterialComponent &outMaterial,
               std::vector<GltfSubmeshRange> *outSubmeshes,
               std::vector<scene::MaterialComponent> *outAllMaterials) {
    outMesh.vertices.clear();
    outMesh.indices.clear();
    if (outSubmeshes != nullptr) {
        outSubmeshes->clear();
        if (outAllMaterials == nullptr) {
            LUMEN_LOG_ERROR(
                "load_gltf: 提供 outSubmeshes 时必须同时提供 outAllMaterials");
            return false;
        }
    }

    const std::string path { filePath };
    const fs::path file_path = fs::absolute(fs::path(path));
    const fs::path model_dir = file_path.parent_path();

    Assimp::Importer importer {};
    const aiScene *scene = importer.ReadFile(path, k_assimp_gltf_flags);

    if (scene == nullptr || scene->mRootNode == nullptr) {
        LUMEN_LOG_ERROR("Assimp 加载失败 {}: {}", path,
                        importer.GetErrorString());
        return false;
    }

    process_node(scene->mRootNode, aiMatrix4x4(), scene, outMesh,
                 outSubmeshes);

    if (outMesh.vertices.empty() || outMesh.indices.empty()) {
        LUMEN_LOG_ERROR("glTF 网格为空: {}", path);
        return false;
    }

    int first_material = -1;
    if (scene->mRootNode != nullptr) {
        first_material = find_first_material_index(scene->mRootNode, scene);
    }

    outMaterial = scene::MaterialComponent {};
    if (outSubmeshes != nullptr && outAllMaterials != nullptr) {
        outAllMaterials->clear();
        outAllMaterials->resize(scene->mNumMaterials);
        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            fill_material_assimp(scene->mMaterials[i], model_dir,
                                 (*outAllMaterials)[i]);
        }
        int dominant = -1;
        size_t best_tri = 0;
        std::vector<size_t> tri_per_mat(scene->mNumMaterials, 0);
        for (const auto &r : *outSubmeshes) {
            if (r.material_index >= 0 &&
                r.material_index < static_cast<int>(scene->mNumMaterials)) {
                tri_per_mat[static_cast<size_t>(r.material_index)] +=
                    static_cast<size_t>(r.index_count) / 3u;
            }
        }
        for (size_t i = 0; i < tri_per_mat.size(); ++i) {
            if (tri_per_mat[i] > best_tri) {
                best_tri = tri_per_mat[i];
                dominant = static_cast<int>(i);
            }
        }
        if (dominant >= 0) {
            outMaterial = (*outAllMaterials)[static_cast<size_t>(dominant)];
        } else if (first_material >= 0 &&
                   first_material < static_cast<int>(scene->mNumMaterials)) {
            outMaterial =
                (*outAllMaterials)[static_cast<size_t>(first_material)];
        }
    } else if (first_material >= 0 &&
               first_material < static_cast<int>(scene->mNumMaterials)) {
        fill_material_assimp(scene->mMaterials[static_cast<unsigned>(
                                 first_material)],
                             model_dir, outMaterial);
    }

    LUMEN_LOG_DEBUG("glTF 加载成功 {}: {} 顶点, {} 索引", path,
                    outMesh.vertices.size(), outMesh.indices.size());
    return true;
}

} // namespace core
} // namespace lumen
