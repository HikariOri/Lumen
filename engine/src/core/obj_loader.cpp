/**
 * @file obj_loader.cpp
 * @brief OBJ 等网格加载（Assimp），输出 Vulkan 可用的顶点数据
 */

#include "core/obj_loader.hpp"
#include "core/logger.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <string>

namespace lumen {
namespace core {

namespace {

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

void append_ai_mesh(const aiMesh *mesh, const glm::mat4 &world, ObjMesh &out) {
    const glm::mat3 n_world =
        glm::transpose(glm::inverse(glm::mat3(world)));

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
}

void process_node(const aiNode *node, const aiMatrix4x4 &parent,
                  const aiScene *scene, ObjMesh &out) {
    const aiMatrix4x4 transform = parent * node->mTransformation;
    const glm::mat4 world = ai_matrix_to_glm(transform);

    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        append_ai_mesh(mesh, world, out);
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c) {
        process_node(node->mChildren[c], transform, scene, out);
    }
}

constexpr unsigned k_assimp_obj_flags =
    aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_GenSmoothNormals;

} // namespace

bool load_obj(std::string_view filePath, ObjMesh &outMesh) {
    outMesh.vertices.clear();
    outMesh.indices.clear();

    const std::string path { filePath };

    Assimp::Importer importer {};
    const aiScene *scene = importer.ReadFile(path, k_assimp_obj_flags);

    if (scene == nullptr || scene->mRootNode == nullptr) {
        LUMEN_LOG_ERROR("Assimp 加载 OBJ 失败 {}: {}", path,
                        importer.GetErrorString());
        return false;
    }

    process_node(scene->mRootNode, aiMatrix4x4(), scene, outMesh);

    if (outMesh.vertices.empty() || outMesh.indices.empty()) {
        LUMEN_LOG_ERROR("OBJ 网格为空: {}", path);
        return false;
    }

    LUMEN_LOG_DEBUG("OBJ 加载成功 {}: {} 顶点, {} 三角形", path,
                    outMesh.vertices.size(), outMesh.indices.size() / 3);
    return true;
}

} // namespace core
} // namespace lumen
