/**
 * @file gltf/gltf_loader.cpp
 * @brief glTF 2.0 / GLB 加载（tinygltf + stb + libktx 解码嵌入图）
 */

#include "gltf/gltf_loader.hpp"
#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"

#include "render/material/material.hpp"

#include <ghc/filesystem.hpp>
#include <stb_image.h>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lumen {
namespace core {

namespace {

namespace fs = ghc::filesystem;

constexpr std::size_t KTX_MAGIC_MIN_BYTES = 12;
constexpr unsigned char KTX_MAGIC_BYTE0 = 0xAB;

constexpr float GLTF_MIN_ROUGHNESS = 0.04F;
constexpr float GLTF_DEFAULT_AO_FACTOR = 1.0F;
constexpr int INVALID_INDEX = -1;
constexpr std::size_t ACCESSOR_STRIDE_FLOAT3 = sizeof(float) * 3U;
constexpr std::size_t ACCESSOR_STRIDE_FLOAT2 = sizeof(float) * 2U;

constexpr std::string_view GLTF_ALPHA_MODE_MASK{"MASK"};
constexpr std::string_view GLTF_ALPHA_MODE_BLEND{"BLEND"};

/// `string_view::ends_with` 区分大小写；常见扩展名列全大写/全小写两种
[[nodiscard]] bool ends_with_ktx_filename(std::string_view s) {
    return s.ends_with(".ktx") || s.ends_with(".KTX") ||
           s.ends_with(".ktx2") || s.ends_with(".KTX2");
}

bool tinygltf_load_image(tinygltf::Image *image, const int imageIdx,
                       std::string *err, std::string *warn, int /*reqWidth*/,
                       int /*reqHeight*/, const unsigned char *bytes, int size,
                       void * /*user*/) {
    (void)imageIdx;
    (void)warn;
    if (image == nullptr || bytes == nullptr || size <= 0) {
        if (err != nullptr) {
            *err += "图像数据无效\n";
        }
        return false;
    }

    const std::string_view uri_sv { image->uri };
    const std::string_view name_sv { image->name };
    const bool extLooksKtx =
        ends_with_ktx_filename(uri_sv) || ends_with_ktx_filename(name_sv);
    const bool magicKtx =
        static_cast<std::size_t>(size) >= KTX_MAGIC_MIN_BYTES &&
        bytes[0] == KTX_MAGIC_BYTE0 && bytes[1] == 'K' && bytes[2] == 'T' &&
        bytes[3] == 'X' && bytes[4] == ' ';

    if (extLooksKtx || magicKtx) {
        std::string ktxErr;
        std::vector<std::uint8_t> rgba;
        std::uint32_t widthPx = 0;
        std::uint32_t heightPx = 0;
        if (!decode_ktx_memory_to_rgba8(bytes, static_cast<std::size_t>(size),
                                        rgba, widthPx, heightPx, &ktxErr)) {
            if (err != nullptr) {
                *err += ktxErr + '\n';
            }
            return false;
        }
        image->width = static_cast<int>(widthPx);
        image->height = static_cast<int>(heightPx);
        image->component = 4;
        image->bits = 8;
        image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        image->image = std::move(rgba);
        return true;
    }

    stbi_set_flip_vertically_on_load(1);
    int widthPx = 0;
    int heightPx = 0;
    int components = 0;
    stbi_uc *pixels =
        stbi_load_from_memory(bytes, size, &widthPx, &heightPx, &components,
                              STBI_rgb_alpha);
    if (pixels == nullptr) {
        if (err != nullptr) {
            *err += std::string("stbi_load_from_memory: ") +
                    (stbi_failure_reason() ? stbi_failure_reason() : "?") +
                    '\n';
        }
        return false;
    }
    image->width = widthPx;
    image->height = heightPx;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.assign(
        pixels,
        pixels + (static_cast<std::size_t>(widthPx) *
                  static_cast<std::size_t>(heightPx) * 4U));
    stbi_image_free(pixels);
    return true;
}

std::string resource_relative_path(const fs::path &absolutePath) {
    std::error_code ec;
    const fs::path root { lumen::core::get_resource_path("") };
    fs::path rel = fs::relative(absolutePath, root, ec);
    if (ec) {
        return absolutePath.generic_string();
    }
    return rel.generic_string();
}

glm::mat4 node_local_matrix(const tinygltf::Node &node) {
    if (node.matrix.size() == 16) {
        glm::dmat4 m(1.0);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                m[c][r] = node.matrix[static_cast<std::size_t>(c * 4 + r)];
            }
        }
        return glm::mat4(m);
    }
    glm::vec3 translation(0.F);
    glm::quat rotation(1.F, 0.F, 0.F, 0.F);
    glm::vec3 scaleVec(1.F);
    if (node.translation.size() >= 3) {
        translation = glm::vec3(static_cast<float>(node.translation[0]),
                                static_cast<float>(node.translation[1]),
                                static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() >= 4) {
        rotation = glm::quat(static_cast<float>(node.rotation[3]),
                             static_cast<float>(node.rotation[0]),
                             static_cast<float>(node.rotation[1]),
                             static_cast<float>(node.rotation[2]));
    }
    if (node.scale.size() >= 3) {
        scaleVec = glm::vec3(static_cast<float>(node.scale[0]),
                             static_cast<float>(node.scale[1]),
                             static_cast<float>(node.scale[2]));
    }
    const glm::mat4 translateM = glm::translate(glm::mat4(1.F), translation);
    const glm::mat4 rotateM = glm::mat4_cast(rotation);
    const glm::mat4 scaleM = glm::scale(glm::mat4(1.F), scaleVec);
    return translateM * rotateM * scaleM;
}

void traverse_nodes(const tinygltf::Model &model, int nodeIdx,
                   const glm::mat4 &parentWorld,
                   const std::function<void(int, const glm::mat4 &)> &onMesh) {
    const tinygltf::Node &node = model.nodes[nodeIdx];
    const glm::mat4 world = parentWorld * node_local_matrix(node);
    if (node.mesh >= 0) {
        onMesh(node.mesh, world);
    }
    for (int child : node.children) {
        traverse_nodes(model, child, world, onMesh);
    }
}

std::size_t accessor_byte_offset(const tinygltf::Model &model,
                               const tinygltf::Accessor &accessor,
                               std::size_t elementIndex,
                               std::size_t defaultElementStride) {
    const tinygltf::BufferView &view =
        model.bufferViews[accessor.bufferView];
    const std::size_t stride =
        static_cast<std::size_t>(accessor.ByteStride(view));
    const std::size_t effectiveStride =
        stride != 0 ? stride : defaultElementStride;
    return static_cast<std::size_t>(view.byteOffset + accessor.byteOffset) +
           elementIndex * effectiveStride;
}

std::uint32_t read_index(const tinygltf::Model &model,
                        const tinygltf::Accessor &accessor,
                        std::size_t elementIndex) {
    const tinygltf::Buffer &buf = model.buffers[model.bufferViews[accessor.bufferView].buffer];
    const std::size_t elementBytes =
        accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2U
                                                                         : 4U;
    const std::size_t offset =
        accessor_byte_offset(model, accessor, elementIndex, elementBytes);
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        std::uint16_t value = 0;
        std::memcpy(&value, buf.data.data() + offset, sizeof(value));
        return value;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, buf.data.data() + offset, sizeof(value));
    return value;
}

bool read_float3(const tinygltf::Model &model, int accessorIdx,
                std::size_t elementIndex, glm::vec3 &out) {
    const tinygltf::Accessor &accessor = model.accessors[accessorIdx];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC3) {
        return false;
    }
    const tinygltf::Buffer &buf =
        model.buffers[model.bufferViews[accessor.bufferView].buffer];
    const std::size_t offset =
        accessor_byte_offset(model, accessor, elementIndex, ACCESSOR_STRIDE_FLOAT3);
    const float *ptr = reinterpret_cast<const float *>(buf.data.data() + offset);
    out = glm::vec3(ptr[0], ptr[1], ptr[2]);
    return true;
}

bool read_float2(const tinygltf::Model &model, int accessorIdx,
                std::size_t elementIndex, glm::vec2 &out) {
    const tinygltf::Accessor &accessor = model.accessors[accessorIdx];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC2) {
        return false;
    }
    const tinygltf::Buffer &buf =
        model.buffers[model.bufferViews[accessor.bufferView].buffer];
    const std::size_t offset =
        accessor_byte_offset(model, accessor, elementIndex, ACCESSOR_STRIDE_FLOAT2);
    const float *ptr = reinterpret_cast<const float *>(buf.data.data() + offset);
    out = glm::vec2(ptr[0], ptr[1]);
    return true;
}

void set_texture_path(const fs::path &gltfDir, const tinygltf::Model &model,
                    int texIndex, std::string &outPath) {
    if (texIndex < 0 || texIndex >= static_cast<int>(model.textures.size())) {
        return;
    }
    const tinygltf::Texture &tex = model.textures[texIndex];
    if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) {
        return;
    }
    const tinygltf::Image &img = model.images[tex.source];
    if (img.uri.empty()) {
        return;
    }
    const fs::path full = fs::absolute(gltfDir / fs::path(img.uri));
    outPath = resource_relative_path(full.lexically_normal());
}

int json_texture_index(const tinygltf::Value &obj) {
    if (!obj.IsObject() || !obj.Has("index")) {
        return INVALID_INDEX;
    }
    const tinygltf::Value &indexVal = obj.Get("index");
    if (!indexVal.IsInt() && !indexVal.IsNumber()) {
        return INVALID_INDEX;
    }
    return indexVal.GetNumberAsInt();
}

void fill_material(const tinygltf::Model &model, int materialIndex,
                  const fs::path &gltfDir, render::MaterialLoadDesc &out) {
    out = render::MaterialLoadDesc {};
    if (materialIndex < 0 ||
        materialIndex >= static_cast<int>(model.materials.size())) {
        return;
    }
    const tinygltf::Material &mat = model.materials[materialIndex];

    if (mat.alphaMode == GLTF_ALPHA_MODE_MASK) {
        out.alpha_mode = render::MaterialAlphaMode::Mask;
    } else if (mat.alphaMode == GLTF_ALPHA_MODE_BLEND) {
        out.alpha_mode = render::MaterialAlphaMode::Blend;
    } else {
        out.alpha_mode = render::MaterialAlphaMode::Opaque;
    }
    out.alpha_cutoff = static_cast<float>(mat.alphaCutoff);
    out.double_sided = mat.doubleSided;
    out.ao_factor = GLTF_DEFAULT_AO_FACTOR;

    const auto &pbr = mat.pbrMetallicRoughness;
    if (pbr.baseColorFactor.size() >= 4) {
        out.base_color_factor =
            glm::vec4(static_cast<float>(pbr.baseColorFactor[0]),
                      static_cast<float>(pbr.baseColorFactor[1]),
                      static_cast<float>(pbr.baseColorFactor[2]),
                      static_cast<float>(pbr.baseColorFactor[3]));
    }
    out.metallic_factor = static_cast<float>(pbr.metallicFactor);
    out.roughness_factor = static_cast<float>(pbr.roughnessFactor);

    if (mat.emissiveFactor.size() >= 3) {
        out.emissive_factor =
            glm::vec3(static_cast<float>(mat.emissiveFactor[0]),
                      static_cast<float>(mat.emissiveFactor[1]),
                      static_cast<float>(mat.emissiveFactor[2]));
    }

    if (pbr.baseColorTexture.index >= 0) {
        set_texture_path(gltfDir, model, pbr.baseColorTexture.index,
                       out.albedo_path);
    }
    if (pbr.metallicRoughnessTexture.index >= 0) {
        set_texture_path(gltfDir, model, pbr.metallicRoughnessTexture.index,
                       out.metallic_roughness_path);
    }
    if (mat.normalTexture.index >= 0) {
        set_texture_path(gltfDir, model, mat.normalTexture.index, out.normal_path);
    }
    if (mat.occlusionTexture.index >= 0) {
        set_texture_path(gltfDir, model, mat.occlusionTexture.index, out.ao_path);
    }
    if (mat.emissiveTexture.index >= 0) {
        set_texture_path(gltfDir, model, mat.emissiveTexture.index,
                       out.emissive_path);
    }

    const auto sgIt = mat.extensions.find(
        "KHR_materials_pbrSpecularGlossiness");
    if (sgIt == mat.extensions.end() || !sgIt->second.IsObject()) {
        return;
    }
    const tinygltf::Value &specGloss = sgIt->second;

    if (specGloss.Has("diffuseFactor") &&
        specGloss.Get("diffuseFactor").IsArray()) {
        const tinygltf::Value &diffuseArr = specGloss.Get("diffuseFactor");
        if (diffuseArr.ArrayLen() >= 4) {
            out.base_color_factor = glm::vec4(
                static_cast<float>(diffuseArr.Get(0).GetNumberAsDouble()),
                static_cast<float>(diffuseArr.Get(1).GetNumberAsDouble()),
                static_cast<float>(diffuseArr.Get(2).GetNumberAsDouble()),
                static_cast<float>(diffuseArr.Get(3).GetNumberAsDouble()));
        }
    }
    if (specGloss.Has("diffuseTexture")) {
        const int texIdx = json_texture_index(specGloss.Get("diffuseTexture"));
        if (texIdx >= 0) {
            set_texture_path(gltfDir, model, texIdx, out.albedo_path);
        }
    }

    if (out.metallic_roughness_path.empty()) {
        out.metallic_factor = 0.F;
    }

    if (specGloss.Has("specularGlossinessTexture")) {
        const int texIdx =
            json_texture_index(specGloss.Get("specularGlossinessTexture"));
        if (texIdx >= 0) {
            set_texture_path(gltfDir, model, texIdx, out.metallic_roughness_path);
            out.spec_gloss_texture_in_mr_slot = true;
            float glossMul = 1.F;
            if (specGloss.Has("glossinessFactor") &&
                specGloss.Get("glossinessFactor").IsNumber()) {
                glossMul = static_cast<float>(
                    specGloss.Get("glossinessFactor").GetNumberAsDouble());
            }
            out.roughness_factor =
                std::clamp(glossMul, GLTF_MIN_ROUGHNESS, 1.F);
        }
    } else if (specGloss.Has("glossinessFactor") &&
               specGloss.Get("glossinessFactor").IsNumber()) {
        const float glossy = static_cast<float>(
            specGloss.Get("glossinessFactor").GetNumberAsDouble());
        out.roughness_factor =
            std::clamp(1.F - glossy, GLTF_MIN_ROUGHNESS, 1.F);
    }
}

void append_primitive(const tinygltf::Model &model,
                      const tinygltf::Primitive &prim, const glm::mat4 &world,
                      int gltf_mesh_idx, CpuMesh &outMesh,
                      std::vector<PrimitiveSlice> *ranges) {
    const int drawMode = prim.mode;
    if (drawMode != TINYGLTF_MODE_TRIANGLES && drawMode != -1) {
        return;
    }

    const auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) {
        return;
    }
    const std::uint32_t sliceFirstIndex =
        ranges != nullptr ? static_cast<std::uint32_t>(outMesh.indices.size())
                          : 0U;
    const int posAccessor = posIt->second;
    const tinygltf::Accessor &posAccessorRef = model.accessors[posAccessor];
    const std::size_t vertexCount = posAccessorRef.count;

    int normalAccessor = INVALID_INDEX;
    if (const auto nIt = prim.attributes.find("NORMAL");
        nIt != prim.attributes.end()) {
        normalAccessor = nIt->second;
    }
    int uvAccessor = INVALID_INDEX;
    if (const auto uvIt = prim.attributes.find("TEXCOORD_0");
        uvIt != prim.attributes.end()) {
        uvAccessor = uvIt->second;
    }

    const glm::mat3 normalWorld =
        glm::transpose(glm::inverse(glm::mat3(world)));

    const std::uint32_t vertexBase =
        static_cast<std::uint32_t>(outMesh.vertices.size());
    const std::size_t vertexStart = outMesh.vertices.size();
    outMesh.vertices.reserve(vertexStart + vertexCount);

    for (std::size_t vi = 0; vi < vertexCount; ++vi) {
        glm::vec3 localPos {};
        if (!read_float3(model, posAccessor, vi, localPos)) {
            outMesh.vertices.resize(vertexStart);
            return;
        }
        const glm::vec3 worldPos =
            glm::vec3(world * glm::vec4(localPos, 1.F));

        glm::vec3 normal { 0.F, 1.F, 0.F };
        if (normalAccessor >= 0) {
            glm::vec3 localNormal {};
            if (read_float3(model, normalAccessor, vi, localNormal)) {
                normal = glm::normalize(normalWorld * localNormal);
            }
        }

        glm::vec2 uv { 0.F };
        if (uvAccessor >= 0) {
            (void)read_float2(model, uvAccessor, vi, uv);
            uv.y = 1.0F - uv.y;
        }

        outMesh.vertices.push_back(
            CpuVertex { .position = worldPos, .normal = normal, .uv = uv });
    }

    if (prim.indices >= 0) {
        const tinygltf::Accessor &indexAccessor =
            model.accessors[prim.indices];
        for (std::size_t ii = 0; ii < indexAccessor.count; ++ii) {
            const std::uint32_t localIdx = read_index(model, indexAccessor, ii);
            outMesh.indices.push_back(vertexBase + localIdx);
        }
    } else {
        for (std::size_t ii = 0; ii + 2 < vertexCount; ii += 3) {
            outMesh.indices.push_back(vertexBase +
                                      static_cast<std::uint32_t>(ii));
            outMesh.indices.push_back(vertexBase +
                                      static_cast<std::uint32_t>(ii + 1));
            outMesh.indices.push_back(vertexBase +
                                      static_cast<std::uint32_t>(ii + 2));
        }
    }

    if (ranges != nullptr) {
        const std::uint32_t indexTotal =
            static_cast<std::uint32_t>(outMesh.indices.size());
        if (indexTotal > sliceFirstIndex) {
            ranges->push_back(PrimitiveSlice{
                .meshIndex = gltf_mesh_idx,
                .firstIndex = sliceFirstIndex,
                .indexCount = indexTotal - sliceFirstIndex,
                .materialIndex = prim.material,
            });
        }
    }
}

void append_mesh(const tinygltf::Model &model, int meshIdx,
                const glm::mat4 &world, CpuMesh &outMesh,
                std::vector<PrimitiveSlice> *ranges) {
    if (meshIdx < 0 || meshIdx >= static_cast<int>(model.meshes.size())) {
        return;
    }
    const tinygltf::Mesh &mesh = model.meshes[meshIdx];
    for (const auto &prim : mesh.primitives) {
        append_primitive(model, prim, world, meshIdx, outMesh, ranges);
    }
}

int find_first_material_index(const tinygltf::Model &model, int nodeIdx) {
    const tinygltf::Node &node = model.nodes[nodeIdx];
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh &mesh = model.meshes[node.mesh];
        for (const auto &prim : mesh.primitives) {
            if (prim.material >= 0) {
                return prim.material;
            }
        }
    }
    for (int child : node.children) {
        const int found = find_first_material_index(model, child);
        if (found >= 0) {
            return found;
        }
    }
    return INVALID_INDEX;
}

} // namespace

bool load_gltf(const std::string_view filePath, CpuMesh &outMesh,
               render::MaterialLoadDesc *outMainMaterial,
               std::vector<PrimitiveSlice> *outPrimitiveSlices,
               std::vector<render::MaterialLoadDesc> *outAllMaterials,
               std::size_t *outGltfMeshCount) {
    outMesh.vertices.clear();
    outMesh.indices.clear();
    if (outPrimitiveSlices != nullptr) {
        outPrimitiveSlices->clear();
        if (outAllMaterials == nullptr) {
            LUMEN_LOG_ERROR(
                "load_gltf: 提供 outPrimitiveSlices 时必须同时提供 "
                "outAllMaterials");
            return false;
        }
    }

    const std::string pathStr { filePath };
    const fs::path gltfPathAbs = fs::absolute(fs::path(pathStr));
    const fs::path gltfDir = gltfPathAbs.parent_path();

    tinygltf::TinyGLTF loader {};
    loader.SetImageLoader(tinygltf_load_image, nullptr);

    tinygltf::Model model {};
    std::string loadErr;
    std::string loadWarn;
    const std::string ext = gltfPathAbs.extension().string();
    const std::string_view ext_sv { ext };
    const bool isGlb = ext_sv.ends_with(".glb") || ext_sv.ends_with(".GLB");
    const bool loadOk =
        isGlb ? loader.LoadBinaryFromFile(&model, &loadErr, &loadWarn, pathStr)
              : loader.LoadASCIIFromFile(&model, &loadErr, &loadWarn, pathStr);

    if (!loadWarn.empty()) {
        LUMEN_LOG_WARN("glTF 警告 {}: {}", pathStr, loadWarn);
    }
    if (!loadOk) {
        LUMEN_LOG_ERROR("glTF 加载失败 {}: {}", pathStr, loadErr);
        return false;
    }

    if (model.scenes.empty()) {
        LUMEN_LOG_ERROR("glTF 无场景: {}", pathStr);
        return false;
    }

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    const tinygltf::Scene &scene = model.scenes[sceneIndex];

    int firstMaterialIdx = INVALID_INDEX;
    for (int rootNode : scene.nodes) {
        firstMaterialIdx = find_first_material_index(model, rootNode);
        if (firstMaterialIdx >= 0) {
            break;
        }
    }

    for (int rootNode : scene.nodes) {
        traverse_nodes(model, rootNode, glm::mat4(1.F),
                      [&](int meshIdx, const glm::mat4 &worldMat) {
                          append_mesh(model, meshIdx, worldMat, outMesh,
                                     outPrimitiveSlices);
                      });
    }

    if (outMesh.vertices.empty() || outMesh.indices.empty()) {
        LUMEN_LOG_ERROR("glTF 网格为空: {}", pathStr);
        return false;
    }

    const bool wantMultiMaterial =
        outPrimitiveSlices != nullptr && outAllMaterials != nullptr;

    if (wantMultiMaterial) {
        outAllMaterials->clear();
        outAllMaterials->resize(model.materials.size());
        for (std::size_t mi = 0; mi < model.materials.size(); ++mi) {
            fill_material(model, static_cast<int>(mi), gltfDir,
                         (*outAllMaterials)[mi]);
        }
        if (outMainMaterial != nullptr) {
            const int materialCount =
                static_cast<int>(model.materials.size());
            int dominantIdx = INVALID_INDEX;
            std::size_t bestTriCount = 0;
            std::vector<std::size_t> triCountPerMat(
                static_cast<std::size_t>(materialCount), 0);
            for (const auto &slice : *outPrimitiveSlices) {
                if (slice.materialIndex >= 0 &&
                    slice.materialIndex < materialCount) {
                    triCountPerMat[static_cast<std::size_t>(slice.materialIndex)] +=
                        static_cast<std::size_t>(slice.indexCount) / 3U;
                }
            }
            for (int mi = 0; mi < materialCount; ++mi) {
                const std::size_t t =
                    triCountPerMat[static_cast<std::size_t>(mi)];
                if (t > bestTriCount) {
                    bestTriCount = t;
                    dominantIdx = mi;
                }
            }
            if (dominantIdx >= 0) {
                *outMainMaterial =
                    (*outAllMaterials)[static_cast<std::size_t>(dominantIdx)];
            } else if (firstMaterialIdx >= 0 && firstMaterialIdx < materialCount) {
                *outMainMaterial =
                    (*outAllMaterials)[static_cast<std::size_t>(firstMaterialIdx)];
            } else {
                *outMainMaterial = render::MaterialLoadDesc {};
            }
        }
    } else if (outMainMaterial != nullptr) {
        *outMainMaterial = render::MaterialLoadDesc {};
        if (firstMaterialIdx >= 0) {
            fill_material(model, firstMaterialIdx, gltfDir, *outMainMaterial);
        }
    }

    if (outGltfMeshCount != nullptr) {
        *outGltfMeshCount = model.meshes.size();
    }

    LUMEN_LOG_DEBUG("glTF 加载成功 {}: {} 顶点, {} 索引", pathStr,
                    outMesh.vertices.size(), outMesh.indices.size());
    return true;
}

} // namespace core
} // namespace lumen
