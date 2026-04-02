/**
 * @file gltf_scene_mesh.cpp
 * @brief glTF 解析（tinygltf）、mesh 局部空间几何、`import_gltf_scene_mesh`
 */

#include "gltf/gltf_scene_mesh.hpp"

#include "asset/asset_registry.hpp"

#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"

#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/material/material.hpp"
#include "render/pbr_interleaved_vertex.hpp"
#include "render/resource/buffer.hpp"
#include "render/vertex_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include <ghc/filesystem.hpp>
#include <stb_image.h>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

namespace lumen::scene {
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

[[nodiscard]] bool ends_with_ktx_filename(std::string_view s) {
    return s.ends_with(".ktx") || s.ends_with(".KTX") ||
           s.ends_with(".ktx2") || s.ends_with(".KTX2");
}

bool tinygltf_load_image(tinygltf::Image *image, const int imageIdx,
                         std::string *err, std::string * /*warn*/, int /*reqWidth*/,
                         int /*reqHeight*/, const unsigned char *bytes, int size,
                         void * /*user*/) {
    (void)imageIdx;
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
        if (!lumen::core::decode_ktx_memory_to_rgba8(
                reinterpret_cast<const std::uint8_t *>(bytes),
                static_cast<std::size_t>(size), rgba, widthPx, heightPx,
                &ktxErr)) {
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
    const tinygltf::Buffer &buf =
        model.buffers[model.bufferViews[accessor.bufferView].buffer];
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

struct GltfCpuVertex {
    glm::vec3 position {};
    glm::vec3 normal {};
    glm::vec2 uv {};
};

struct BuiltPrimSlice {
    std::uint32_t firstIndex {};
    std::uint32_t indexCount {};
    int materialIndex { -1 };
};

std::optional<BuiltPrimSlice>
append_primitive_local(const tinygltf::Model &model,
                       const tinygltf::Primitive &prim,
                       std::vector<GltfCpuVertex> &outVerts,
                       std::vector<std::uint32_t> &outIndices) {
    const int drawMode = prim.mode;
    if (drawMode != TINYGLTF_MODE_TRIANGLES && drawMode != -1) {
        return std::nullopt;
    }

    const auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) {
        return std::nullopt;
    }
    const std::uint32_t sliceFirstIndex =
        static_cast<std::uint32_t>(outIndices.size());
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

    const std::uint32_t vertexBase =
        static_cast<std::uint32_t>(outVerts.size());
    const std::size_t vertexStart = outVerts.size();
    outVerts.reserve(vertexStart + vertexCount);

    for (std::size_t vi = 0; vi < vertexCount; ++vi) {
        glm::vec3 localPos {};
        if (!read_float3(model, posAccessor, vi, localPos)) {
            outVerts.resize(vertexStart);
            return std::nullopt;
        }

        glm::vec3 normal { 0.F, 1.F, 0.F };
        if (normalAccessor >= 0) {
            glm::vec3 localNormal {};
            if (read_float3(model, normalAccessor, vi, localNormal)) {
                normal = glm::normalize(localNormal);
            }
        }

        glm::vec2 uv { 0.F };
        if (uvAccessor >= 0) {
            (void)read_float2(model, uvAccessor, vi, uv);
            uv.y = 1.0F - uv.y;
        }

        outVerts.push_back(
            GltfCpuVertex { .position = localPos, .normal = normal, .uv = uv });
    }

    if (prim.indices >= 0) {
        const tinygltf::Accessor &indexAccessor =
            model.accessors[prim.indices];
        for (std::size_t ii = 0; ii < indexAccessor.count; ++ii) {
            const std::uint32_t localIdx = read_index(model, indexAccessor, ii);
            outIndices.push_back(vertexBase + localIdx);
        }
    } else {
        for (std::size_t ii = 0; ii + 2 < vertexCount; ii += 3) {
            outIndices.push_back(vertexBase +
                                 static_cast<std::uint32_t>(ii));
            outIndices.push_back(vertexBase +
                                 static_cast<std::uint32_t>(ii + 1));
            outIndices.push_back(vertexBase +
                                 static_cast<std::uint32_t>(ii + 2));
        }
    }

    const std::uint32_t indexTotal =
        static_cast<std::uint32_t>(outIndices.size());
    if (indexTotal <= sliceFirstIndex) {
        outVerts.resize(vertexStart);
        outIndices.resize(sliceFirstIndex);
        return std::nullopt;
    }
    return BuiltPrimSlice{ .firstIndex = sliceFirstIndex,
                           .indexCount = indexTotal - sliceFirstIndex,
                           .materialIndex = prim.material };
}

void dfs_collect_scene_node(const tinygltf::Model &model, int gltfNodeIdx,
                            int parentSceneIdx, std::vector<SceneNode> &out) {
    const tinygltf::Node &node = model.nodes[gltfNodeIdx];
    SceneNode sn;
    sn.parent_index = parentSceneIdx;
    sn.name = node.name;
    sn.local_transform = node_local_matrix(node);
    sn.mesh_index = node.mesh;
    const int myIdx = static_cast<int>(out.size());
    out.push_back(std::move(sn));
    for (int ch : node.children) {
        dfs_collect_scene_node(model, ch, myIdx, out);
    }
}

void collect_default_scene_nodes(const tinygltf::Model &model, int sceneIndex,
                                 std::vector<SceneNode> &out) {
    out.clear();
    if (model.scenes.empty()) {
        return;
    }
    const int si =
        sceneIndex >= 0 ? sceneIndex : (model.defaultScene >= 0 ? model.defaultScene : 0);
    if (si < 0 || si >= static_cast<int>(model.scenes.size())) {
        return;
    }
    const tinygltf::Scene &sc = model.scenes[si];
    for (int root : sc.nodes) {
        dfs_collect_scene_node(model, root, -1, out);
    }
}

void recenter_and_scale_cpu_vertices(std::vector<GltfCpuVertex> &verts,
                                     const SceneMeshLoadOptions &opts) {
    if (verts.empty()) {
        return;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (const auto &v : verts) {
        bmin = glm::min(bmin, v.position);
        bmax = glm::max(bmax, v.position);
    }
    if (opts.recenterToOrigin) {
        const glm::vec3 center { 0.5F * (bmin + bmax) };
        for (auto &v : verts) {
            v.position -= center;
        }
        bmin -= center;
        bmax -= center;
    }
    if (opts.uniformScaleMaxAxis > 0.F) {
        const glm::vec3 extent { bmax - bmin };
        const float max_axis = std::max({ extent.x, extent.y, extent.z });
        const float s =
            max_axis > 1e-8F ? (opts.uniformScaleMaxAxis / max_axis) : 1.0F;
        for (auto &v : verts) {
            v.position *= s;
        }
    }
}

struct PrimitiveLocalAabb {
    glm::vec3 center { 0.0F, 0.0F, 0.0F };
    glm::vec3 half_extent { 0.0F, 0.0F, 0.0F };
    bool valid { false };
};

[[nodiscard]] PrimitiveLocalAabb
primitive_index_local_aabb(
    const std::vector<lumen::render::PbrInterleavedVertex> &verts,
                           const std::vector<std::uint32_t> &indices,
                           std::uint32_t first_index,
                           std::uint32_t index_count) {
    PrimitiveLocalAabb out {};
    if (index_count == 0U) {
        return out;
    }
    const std::size_t end = static_cast<std::size_t>(first_index) +
                            static_cast<std::size_t>(index_count);
    if (end > indices.size()) {
        return out;
    }
    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    std::uint32_t hit_count { 0U };
    for (std::uint32_t i = 0; i < index_count; ++i) {
        const std::uint32_t vi = indices[first_index + i];
        if (vi >= verts.size()) {
            continue;
        }
        const glm::vec3 &p = verts[vi].position;
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
        ++hit_count;
    }
    if (hit_count == 0U) {
        return out;
    }
    out.center = 0.5F * (bmin + bmax);
    out.half_extent = 0.5F * (bmax - bmin);
    out.valid = true;
    return out;
}

void fail(std::string *err, const char *msg) {
    if (err != nullptr) {
        err->append(msg);
    }
}

} // namespace

bool import_gltf_scene_mesh(lumen::render::Context &ctx, VkQueue transfer_queue,
                          lumen::render::CommandPool &cmd_pool,
                          const std::string_view gltf_path, SceneMeshAsset &out,
                          const SceneMeshLoadOptions &opts,
                          std::string *error_message) {
    out = SceneMeshAsset {};

    const std::string path_str { gltf_path };
    const fs::path gltfPathAbs = fs::absolute(fs::path(path_str));
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
        isGlb ? loader.LoadBinaryFromFile(&model, &loadErr, &loadWarn, path_str)
              : loader.LoadASCIIFromFile(&model, &loadErr, &loadWarn, path_str);

    if (!loadWarn.empty()) {
        LUMEN_LOG_WARN("glTF 警告 {}: {}", path_str, loadWarn);
    }
    if (!loadOk) {
        fail(error_message, "glTF 文件解析失败");
        LUMEN_LOG_ERROR("glTF 加载失败 {}: {}", path_str, loadErr);
        return false;
    }

    if (model.scenes.empty()) {
        fail(error_message, "glTF 无场景");
        return false;
    }

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    collect_default_scene_nodes(model, sceneIndex, out.scene_nodes);

    std::vector<GltfCpuVertex> cpuVerts;
    std::vector<std::uint32_t> cpuIndices;
    std::vector<std::vector<BuiltPrimSlice>> perMeshSlices;
    perMeshSlices.resize(model.meshes.size());

    for (std::size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
        const tinygltf::Mesh &mesh = model.meshes[meshIdx];
        for (const auto &prim : mesh.primitives) {
            if (const auto slice =
                    append_primitive_local(model, prim, cpuVerts, cpuIndices)) {
                perMeshSlices[meshIdx].push_back(*slice);
            }
        }
    }

    if (cpuVerts.empty() || cpuIndices.empty()) {
        fail(error_message, "glTF 几何为空");
        return false;
    }

    // 多 mesh 时顶点分布在各自 mesh 局部空间，对整块 VB 做统一平移/缩放会破坏与
    // `scene_nodes` 变换的复合关系（表现为场景部件散开）。单 mesh（如头盔）仍可安全预处理。
    if (model.meshes.size() <= 1) {
        recenter_and_scale_cpu_vertices(cpuVerts, opts);
    } else if (opts.recenterToOrigin || opts.uniformScaleMaxAxis > 0.F) {
        LUMEN_LOG_DEBUG(
            "import_gltf_scene_mesh: {} 个 glTF mesh，跳过顶点 recenter/scale（"
            "请用相机或根节点变换适配场景）",
            static_cast<unsigned>(model.meshes.size()));
    }

    std::vector<render::MaterialLoadDesc> mat_descs;
    mat_descs.resize(model.materials.size());
    for (std::size_t mi = 0; mi < model.materials.size(); ++mi) {
        fill_material(model, static_cast<int>(mi), gltfDir, mat_descs[mi]);
    }
    if (mat_descs.empty()) {
        mat_descs.emplace_back();
    }

    std::vector<lumen::render::PbrInterleavedVertex> verts;
    verts.reserve(cpuVerts.size());
    for (const auto &cv : cpuVerts) {
        verts.push_back(lumen::render::PbrInterleavedVertex{
            .position = cv.position,
            .normal = cv.normal,
            .uv = cv.uv,
            .tangent = { 1.0F, 0.0F, 0.0F, 1.0F } });
    }
    lumen::render::compute_pbr_mesh_tangents(verts, cpuIndices);

    auto vbuf = std::make_unique<lumen::render::VertexBuffer>();
    if (!vbuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, verts.data(),
            verts.size() * sizeof(lumen::render::PbrInterleavedVertex))) {
        fail(error_message, "顶点缓冲上传失败");
        return false;
    }
    auto ibuf = std::make_unique<lumen::render::IndexBuffer>();
    ibuf->set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    if (!ibuf->create_device_local_and_upload(
            ctx, transfer_queue, cmd_pool, cpuIndices.data(),
            cpuIndices.size() * sizeof(std::uint32_t))) {
        fail(error_message, "索引缓冲上传失败");
        return false;
    }

    lumen::asset::AssetRegistry &assets =
        lumen::asset::AssetRegistry::instance();
    lumen::asset::MaterialRegistry &mat_registry = assets.materials();

    out.materials.resize(mat_descs.size());
    for (std::size_t i = 0; i < mat_descs.size(); ++i) {
        out.materials[i] = mat_registry.get_or_create(
            assets.textures(), ctx, transfer_queue, cmd_pool, mat_descs[i]);
    }

    const lumen::render::VertexLayout layout =
        lumen::render::make_vertex_layout_pbr_forward_tangent();

    out.model.assign(model.meshes.size(), lumen::asset::geometry::Mesh {});
    for (std::size_t meshIdx = 0; meshIdx < perMeshSlices.size(); ++meshIdx) {
        for (const BuiltPrimSlice &sl : perMeshSlices[meshIdx]) {
            int mi = sl.materialIndex;
            if (mi < 0 || mi >= static_cast<int>(out.materials.size())) {
                mi = 0;
            }
            const PrimitiveLocalAabb prim_aabb = primitive_index_local_aabb(
                verts, cpuIndices, sl.firstIndex, sl.indexCount);
            out.model[meshIdx].primitives.push_back(
                lumen::asset::geometry::Primitive {
                    .vertexByteOffset = 0,
                    .firstIndex = sl.firstIndex,
                    .indexCount = sl.indexCount,
                    .baseVertex = 0,
                    .layout = layout,
                    .material =
                        &out.materials[static_cast<std::size_t>(mi)]->material,
                    .localPivot =
                        prim_aabb.valid ? prim_aabb.center : glm::vec3(0.0F),
                    .localAabbHalfExtent =
                        prim_aabb.valid ? prim_aabb.half_extent : glm::vec3(0.0F),
                });
        }
    }

    out.vertexBuffer = std::move(vbuf);
    out.indexBuffer = std::move(ibuf);
    out.statsVertexCount = verts.size();
    out.statsIndexCount = cpuIndices.size();

    std::size_t prim_total = 0;
    for (const lumen::asset::geometry::Mesh &m : out.model) {
        prim_total += m.primitives.size();
    }
    LUMEN_LOG_DEBUG("import_gltf_scene_mesh: {} 顶点, {} 索引, {} mesh, {} "
                    "primitive, {} 材质, {} 场景节点",
                    out.statsVertexCount, out.statsIndexCount, out.model.size(),
                    prim_total, out.materials.size(), out.scene_nodes.size());
    return true;
}

} // namespace lumen::scene
