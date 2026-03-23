/**
 * @file gltf_loader.cpp
 * @brief tinygltf + 自定义图像（stb / libktx）加载 glTF 2.0
 */

#include "core/gltf_loader.hpp"
#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"
#include "core/path.hpp"

#include "scene/components.hpp"

#include <stb_image.h>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lumen {
namespace core {

namespace {

namespace fs = std::filesystem;

bool ends_with_ci(std::string_view s, std::string_view ext) {
    if (s.size() < ext.size()) {
        return false;
    }
    for (size_t i = 0; i < ext.size(); ++i) {
        const char a = static_cast<char>(
            std::tolower(static_cast<unsigned char>(s[s.size() - ext.size() + i])));
        const char b = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool tinygltf_load_image(tinygltf::Image *image, const int image_idx,
                         std::string *err, std::string *warn, int /*req_width*/,
                         int /*req_height*/, const unsigned char *bytes, int size,
                         void * /*user*/) {
    (void)image_idx;
    (void)warn;
    if (image == nullptr || bytes == nullptr || size <= 0) {
        if (err != nullptr) {
            *err += "图像数据无效\n";
        }
        return false;
    }

    const bool ext_ktx = ends_with_ci(image->uri, ".ktx") ||
                         ends_with_ci(image->uri, ".ktx2") ||
                         ends_with_ci(image->name, ".ktx") ||
                         ends_with_ci(image->name, ".ktx2");
    const bool magic_ktx =
        size >= 12 && bytes[0] == static_cast<unsigned char>(0xAB) &&
        bytes[1] == 'K' && bytes[2] == 'T' && bytes[3] == 'X' &&
        bytes[4] == ' ';

    if (ext_ktx || magic_ktx) {
        std::string kerr;
        std::vector<std::uint8_t> rgba;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        if (!decode_ktx_memory_to_rgba8(bytes, static_cast<std::size_t>(size),
                                        rgba, w, h, &kerr)) {
            if (err != nullptr) {
                *err += kerr + '\n';
            }
            return false;
        }
        image->width = static_cast<int>(w);
        image->height = static_cast<int>(h);
        image->component = 4;
        image->bits = 8;
        image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        image->image = std::move(rgba);
        return true;
    }

    stbi_set_flip_vertically_on_load(1);
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc *data =
        stbi_load_from_memory(bytes, size, &w, &h, &comp, STBI_rgb_alpha);
    if (data == nullptr) {
        if (err != nullptr) {
            *err += std::string("stbi_load_from_memory: ") +
                    (stbi_failure_reason() ? stbi_failure_reason() : "?") +
                    '\n';
        }
        return false;
    }
    image->width = w;
    image->height = h;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.assign(
        data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    stbi_image_free(data);
    return true;
}

std::string resource_relative_path(const fs::path &absolute_path) {
    std::error_code ec;
    const fs::path root { lumen::core::get_resource_path("") };
    fs::path rel = fs::relative(absolute_path, root, ec);
    if (ec) {
        return absolute_path.generic_string();
    }
    return rel.generic_string();
}

glm::mat4 node_local_matrix(const tinygltf::Node &n) {
    if (n.matrix.size() == 16) {
        glm::dmat4 m(1.0);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                m[c][r] = n.matrix[static_cast<size_t>(c * 4 + r)];
            }
        }
        return glm::mat4(m);
    }
    glm::vec3 t(0.F);
    glm::quat q(1.F, 0.F, 0.F, 0.F);
    glm::vec3 s(1.F);
    if (n.translation.size() >= 3) {
        t = glm::vec3(static_cast<float>(n.translation[0]),
                      static_cast<float>(n.translation[1]),
                      static_cast<float>(n.translation[2]));
    }
    if (n.rotation.size() >= 4) {
        q = glm::quat(static_cast<float>(n.rotation[3]),
                      static_cast<float>(n.rotation[0]),
                      static_cast<float>(n.rotation[1]),
                      static_cast<float>(n.rotation[2]));
    }
    if (n.scale.size() >= 3) {
        s = glm::vec3(static_cast<float>(n.scale[0]),
                      static_cast<float>(n.scale[1]),
                      static_cast<float>(n.scale[2]));
    }
    const glm::mat4 T = glm::translate(glm::mat4(1.F), t);
    const glm::mat4 R = glm::mat4_cast(q);
    const glm::mat4 S = glm::scale(glm::mat4(1.F), s);
    return T * R * S;
}

void traverse_nodes(const tinygltf::Model &model, int node_idx,
                    const glm::mat4 &parent,
                    const std::function<void(int, const glm::mat4 &)> &on_mesh) {
    const tinygltf::Node &node = model.nodes[node_idx];
    const glm::mat4 world = parent * node_local_matrix(node);
    if (node.mesh >= 0) {
        on_mesh(node.mesh, world);
    }
    for (int c : node.children) {
        traverse_nodes(model, c, world, on_mesh);
    }
}

std::uint32_t read_index(const tinygltf::Model &model,
                         const tinygltf::Accessor &acc, size_t i) {
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    const size_t el =
        acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? 2u : 4u;
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        std::uint16_t v = 0;
        std::memcpy(&v, buf.data.data() + off, sizeof(v));
        return v;
    }
    std::uint32_t v = 0;
    std::memcpy(&v, buf.data.data() + off, sizeof(v));
    return v;
}

bool read_float3(const tinygltf::Model &model, int accessor_idx, size_t i,
                 glm::vec3 &out) {
    const tinygltf::Accessor &acc = model.accessors[accessor_idx];
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        acc.type != TINYGLTF_TYPE_VEC3) {
        return false;
    }
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    const size_t el = sizeof(float) * 3u;
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    const float *p = reinterpret_cast<const float *>(buf.data.data() + off);
    out = glm::vec3(p[0], p[1], p[2]);
    return true;
}

bool read_float2(const tinygltf::Model &model, int accessor_idx, size_t i,
                 glm::vec2 &out) {
    const tinygltf::Accessor &acc = model.accessors[accessor_idx];
    if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        acc.type != TINYGLTF_TYPE_VEC2) {
        return false;
    }
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    const size_t el = sizeof(float) * 2u;
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    const float *p = reinterpret_cast<const float *>(buf.data.data() + off);
    out = glm::vec2(p[0], p[1]);
    return true;
}

void append_primitive(const tinygltf::Model &model,
                      const tinygltf::Primitive &prim, const glm::mat4 &world,
                      ObjMesh &out, std::vector<GltfSubmeshRange> *ranges) {
    const int mode = prim.mode;
    if (mode != TINYGLTF_MODE_TRIANGLES && mode != -1) {
        return;
    }

    auto pit = prim.attributes.find("POSITION");
    if (pit == prim.attributes.end()) {
        return;
    }
    const std::uint32_t submesh_first_index =
        ranges != nullptr ? static_cast<std::uint32_t>(out.indices.size()) : 0u;
    const int pos_acc = pit->second;
    const tinygltf::Accessor &pos_access = model.accessors[pos_acc];
    const size_t vcount = pos_access.count;

    int nrm_acc = -1;
    auto nit = prim.attributes.find("NORMAL");
    if (nit != prim.attributes.end()) {
        nrm_acc = nit->second;
    }
    int uv_acc = -1;
    auto uit = prim.attributes.find("TEXCOORD_0");
    if (uit != prim.attributes.end()) {
        uv_acc = uit->second;
    }

    const glm::mat3 n_world =
        glm::transpose(glm::inverse(glm::mat3(world)));

    const std::uint32_t vbase =
        static_cast<std::uint32_t>(out.vertices.size());
    const size_t vert_start = out.vertices.size();
    for (size_t i = 0; i < vcount; ++i) {
        glm::vec3 p {};
        if (!read_float3(model, pos_acc, i, p)) {
            while (out.vertices.size() > vert_start) {
                out.vertices.pop_back();
            }
            return;
        }
        const glm::vec3 wp = glm::vec3(world * glm::vec4(p, 1.F));

        glm::vec3 n { 0.F, 1.F, 0.F };
        if (nrm_acc >= 0) {
            glm::vec3 ln {};
            if (read_float3(model, nrm_acc, i, ln)) {
                n = glm::normalize(n_world * ln);
            }
        }

        glm::vec2 uv { 0.F };
        if (uv_acc >= 0) {
            (void)read_float2(model, uv_acc, i, uv);
        }

        out.vertices.push_back(ObjVertex { wp, n, uv });
    }

    if (prim.indices >= 0) {
        const tinygltf::Accessor &ia = model.accessors[prim.indices];
        for (size_t i = 0; i < ia.count; ++i) {
            const std::uint32_t idx = read_index(model, ia, i);
            out.indices.push_back(vbase + idx);
        }
    } else {
        for (size_t i = 0; i + 2 < vcount; i += 3) {
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i));
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i + 1));
            out.indices.push_back(vbase + static_cast<std::uint32_t>(i + 2));
        }
    }

    if (ranges != nullptr) {
        const std::uint32_t total =
            static_cast<std::uint32_t>(out.indices.size());
        if (total > submesh_first_index) {
            ranges->push_back(GltfSubmeshRange { submesh_first_index,
                                                 total - submesh_first_index,
                                                 prim.material });
        }
    }
}

void append_mesh(const tinygltf::Model &model, int mesh_idx,
                 const glm::mat4 &world, ObjMesh &out,
                 std::vector<GltfSubmeshRange> *ranges) {
    if (mesh_idx < 0 || mesh_idx >= static_cast<int>(model.meshes.size())) {
        return;
    }
    const tinygltf::Mesh &mesh = model.meshes[mesh_idx];
    for (const auto &prim : mesh.primitives) {
        append_primitive(model, prim, world, out, ranges);
    }
}

constexpr const char *kExtPbrSpecularGlossiness =
    "KHR_materials_pbrSpecularGlossiness";

/// glTF TextureInfo：`{ "index": n }`
int texture_index_from_json_object(const tinygltf::Value &obj) {
    if (!obj.IsObject() || !obj.Has("index")) {
        return -1;
    }
    const tinygltf::Value &ix = obj.Get("index");
    if (!ix.IsInt() && !ix.IsNumber()) {
        return -1;
    }
    return ix.GetNumberAsInt();
}

void set_texture_path(const fs::path &gltf_dir, const tinygltf::Model &model,
                      int tex_index, std::string &out_path) {
    if (tex_index < 0 || tex_index >= static_cast<int>(model.textures.size())) {
        return;
    }
    const tinygltf::Texture &tex = model.textures[tex_index];
    if (tex.source < 0 || tex.source >= static_cast<int>(model.images.size())) {
        return;
    }
    const tinygltf::Image &img = model.images[tex.source];
    if (img.uri.empty()) {
        return;
    }
    const fs::path full = fs::absolute(gltf_dir / fs::path(img.uri));
    out_path = resource_relative_path(full.lexically_normal());
}

/// 与 [Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples) 的
/// `parse_material` 一致：从 `ParameterMap` 读取标量因子（可覆盖 struct 默认值）。
void apply_material_factors_from_parameter(const std::string &key,
                                           const tinygltf::Parameter &param,
                                           scene::MaterialComponent &out) {
    if (key == "baseColorFactor") {
        const auto c = param.ColorFactor();
        out.base_color_factor =
            glm::vec4(static_cast<float>(c[0]), static_cast<float>(c[1]),
                      static_cast<float>(c[2]), static_cast<float>(c[3]));
    } else if (key == "metallicFactor" && param.has_number_value) {
        out.metallic_factor = static_cast<float>(param.number_value);
    } else if (key == "roughnessFactor" && param.has_number_value) {
        out.roughness_factor = static_cast<float>(param.number_value);
    } else if (key == "emissiveFactor" && param.number_array.size() >= 3) {
        out.emissive_factor =
            glm::vec3(static_cast<float>(param.number_array[0]),
                      static_cast<float>(param.number_array[1]),
                      static_cast<float>(param.number_array[2]));
    }
}

/// 键名含 `Texture` 且 `TextureIndex()` 有效时，按 glTF 语义写入 `MaterialComponent`。
/// `skip_base_color_texture_from_maps`：KHR spec/gloss 已从扩展写入 diffuse 时，勿让
/// `values` 里重复的 `baseColorTexture` 覆盖反照率路径。
void apply_material_texture_from_key(const std::string &key,
                                     const tinygltf::Parameter &param,
                                     const fs::path &gltf_dir,
                                     const tinygltf::Model &model,
                                     scene::MaterialComponent &out,
                                     bool skip_base_color_texture_from_maps) {
    if (key.find("Texture") == std::string::npos) {
        return;
    }
    const int tix = param.TextureIndex();
    if (tix < 0 || tix >= static_cast<int>(model.textures.size())) {
        return;
    }
    if (key == "baseColorTexture") {
        if (skip_base_color_texture_from_maps) {
            return;
        }
        set_texture_path(gltf_dir, model, tix, out.albedo_path);
    } else if (key == "diffuseTexture") {
        if (out.albedo_path.empty()) {
            set_texture_path(gltf_dir, model, tix, out.albedo_path);
        }
    } else if (key == "metallicRoughnessTexture") {
        out.spec_gloss_texture_in_mr_slot = false;
        set_texture_path(gltf_dir, model, tix, out.metallic_roughness_path);
    } else if (key == "normalTexture") {
        set_texture_path(gltf_dir, model, tix, out.normal_path);
    } else if (key == "occlusionTexture") {
        set_texture_path(gltf_dir, model, tix, out.ao_path);
    } else if (key == "emissiveTexture") {
        set_texture_path(gltf_dir, model, tix, out.emissive_path);
    }
}

void merge_parameter_maps_into_material(const tinygltf::Material &m,
                                        const fs::path &gltf_dir,
                                        const tinygltf::Model &model,
                                        scene::MaterialComponent &out,
                                        bool skip_base_color_texture_from_maps) {
    for (const auto &kv : m.values) {
        apply_material_factors_from_parameter(kv.first, kv.second, out);
        apply_material_texture_from_key(kv.first, kv.second, gltf_dir, model,
                                        out, skip_base_color_texture_from_maps);
    }
    for (const auto &kv : m.additionalValues) {
        apply_material_factors_from_parameter(kv.first, kv.second, out);
        apply_material_texture_from_key(kv.first, kv.second, gltf_dir, model,
                                        out, skip_base_color_texture_from_maps);
    }
}

void fill_material(const tinygltf::Model &model, int material_index,
                   const fs::path &gltf_dir, scene::MaterialComponent &out) {
    if (material_index < 0 ||
        material_index >= static_cast<int>(model.materials.size())) {
        return;
    }
    const tinygltf::Material &m = model.materials[material_index];

    if (m.alphaMode == "MASK") {
        out.alpha_mode = scene::MaterialAlphaMode::Mask;
    } else if (m.alphaMode == "BLEND") {
        out.alpha_mode = scene::MaterialAlphaMode::Blend;
    } else {
        out.alpha_mode = scene::MaterialAlphaMode::Opaque;
    }
    out.alpha_cutoff = static_cast<float>(m.alphaCutoff);
    out.double_sided = m.doubleSided;
    out.spec_gloss_texture_in_mr_slot = false;

    const auto &pbr = m.pbrMetallicRoughness;
    const auto sg_it = m.extensions.find(kExtPbrSpecularGlossiness);
    const bool has_sg =
        sg_it != m.extensions.end() && sg_it->second.IsObject();

    if (pbr.baseColorFactor.size() >= 4) {
        out.base_color_factor =
            glm::vec4(static_cast<float>(pbr.baseColorFactor[0]),
                      static_cast<float>(pbr.baseColorFactor[1]),
                      static_cast<float>(pbr.baseColorFactor[2]),
                      static_cast<float>(pbr.baseColorFactor[3]));
    }
    out.metallic_factor = static_cast<float>(pbr.metallicFactor);
    out.roughness_factor = static_cast<float>(pbr.roughnessFactor);
    out.ao_factor = 1.0F;

    if (m.emissiveFactor.size() >= 3) {
        out.emissive_factor =
            glm::vec3(static_cast<float>(m.emissiveFactor[0]),
                      static_cast<float>(m.emissiveFactor[1]),
                      static_cast<float>(m.emissiveFactor[2]));
    }

    bool sg_had_diffuse_texture = false;
    if (has_sg) {
        const tinygltf::Value &sg = sg_it->second;
        if (sg.Has("diffuseFactor") && sg.Get("diffuseFactor").IsArray()) {
            const tinygltf::Value &dfa = sg.Get("diffuseFactor");
            if (dfa.ArrayLen() >= 4) {
                out.base_color_factor =
                    glm::vec4(static_cast<float>(dfa.Get(0).GetNumberAsDouble()),
                              static_cast<float>(dfa.Get(1).GetNumberAsDouble()),
                              static_cast<float>(dfa.Get(2).GetNumberAsDouble()),
                              static_cast<float>(dfa.Get(3).GetNumberAsDouble()));
            }
        }
        if (sg.Has("diffuseTexture")) {
            const int ti =
                texture_index_from_json_object(sg.Get("diffuseTexture"));
            if (ti >= 0) {
                set_texture_path(gltf_dir, model, ti, out.albedo_path);
                sg_had_diffuse_texture = true;
            }
        }
    }

    if (pbr.baseColorTexture.index >= 0 && !sg_had_diffuse_texture) {
        set_texture_path(gltf_dir, model, pbr.baseColorTexture.index,
                         out.albedo_path);
    }
    if (pbr.metallicRoughnessTexture.index >= 0) {
        set_texture_path(gltf_dir, model, pbr.metallicRoughnessTexture.index,
                         out.metallic_roughness_path);
    }
    if (m.normalTexture.index >= 0) {
        set_texture_path(gltf_dir, model, m.normalTexture.index,
                         out.normal_path);
    }
    if (m.occlusionTexture.index >= 0) {
        set_texture_path(gltf_dir, model, m.occlusionTexture.index, out.ao_path);
    }
    if (m.emissiveTexture.index >= 0) {
        set_texture_path(gltf_dir, model, m.emissiveTexture.index,
                         out.emissive_path);
    }

    merge_parameter_maps_into_material(m, gltf_dir, model, out,
                                       sg_had_diffuse_texture);

    if (has_sg) {
        const tinygltf::Value &sg = sg_it->second;

        if (out.metallic_roughness_path.empty()) {
            out.metallic_factor = 0.F;
        }

        if (out.metallic_roughness_path.empty() &&
            sg.Has("specularGlossinessTexture")) {
            const int sgti = texture_index_from_json_object(
                sg.Get("specularGlossinessTexture"));
            if (sgti >= 0) {
                set_texture_path(gltf_dir, model, sgti,
                                 out.metallic_roughness_path);
                out.spec_gloss_texture_in_mr_slot = true;
                float gloss_mul = 1.F;
                if (sg.Has("glossinessFactor") &&
                    sg.Get("glossinessFactor").IsNumber()) {
                    gloss_mul = static_cast<float>(
                        sg.Get("glossinessFactor").GetNumberAsDouble());
                }
                out.roughness_factor = std::clamp(gloss_mul, 0.04F, 1.F);
            }
        }

        if (out.metallic_roughness_path.empty() &&
            !out.spec_gloss_texture_in_mr_slot &&
            sg.Has("glossinessFactor") &&
            sg.Get("glossinessFactor").IsNumber()) {
            const float glossy = static_cast<float>(
                sg.Get("glossinessFactor").GetNumberAsDouble());
            out.roughness_factor = std::clamp(1.F - glossy, 0.04F, 1.F);
        }
    }
}

int find_first_material_index(const tinygltf::Model &model, int node_idx) {
    const tinygltf::Node &node = model.nodes[node_idx];
    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
        const tinygltf::Mesh &mesh = model.meshes[node.mesh];
        for (const auto &prim : mesh.primitives) {
            if (prim.material >= 0) {
                return prim.material;
            }
        }
    }
    for (int c : node.children) {
        const int r = find_first_material_index(model, c);
        if (r >= 0) {
            return r;
        }
    }
    return -1;
}

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
    const fs::path gltf_path = fs::absolute(fs::path(path));
    const fs::path gltf_dir = gltf_path.parent_path();

    tinygltf::TinyGLTF loader {};
    loader.SetImageLoader(tinygltf_load_image, nullptr);

    tinygltf::Model model {};
    std::string err;
    std::string warn;
    bool ok = false;
    const std::string ext = gltf_path.extension().string();
    if (ext == ".glb" || ext == ".GLB") {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!warn.empty()) {
        LUMEN_LOG_WARN("glTF 警告 {}: {}", path, warn);
    }
    if (!ok) {
        LUMEN_LOG_ERROR("glTF 加载失败 {}: {}", path, err);
        return false;
    }

    if (model.scenes.empty()) {
        LUMEN_LOG_ERROR("glTF 无场景: {}", path);
        return false;
    }

    const int scene_index =
        model.defaultScene >= 0 ? model.defaultScene : 0;
    const tinygltf::Scene &scene = model.scenes[scene_index];

    int first_material = -1;
    for (int root : scene.nodes) {
        first_material = find_first_material_index(model, root);
        if (first_material >= 0) {
            break;
        }
    }

    for (int root : scene.nodes) {
        traverse_nodes(model, root, glm::mat4(1.F),
                       [&](int mesh_idx, const glm::mat4 &world) {
                           append_mesh(model, mesh_idx, world, outMesh,
                                       outSubmeshes);
                       });
    }

    if (outMesh.vertices.empty() || outMesh.indices.empty()) {
        LUMEN_LOG_ERROR("glTF 网格为空: {}", path);
        return false;
    }

    outMaterial = scene::MaterialComponent {};
    if (outSubmeshes != nullptr && outAllMaterials != nullptr) {
        outAllMaterials->clear();
        outAllMaterials->resize(model.materials.size());
        for (size_t i = 0; i < model.materials.size(); ++i) {
            fill_material(model, static_cast<int>(i), gltf_dir,
                          (*outAllMaterials)[i]);
        }
        int dominant = -1;
        size_t best_tri = 0;
        std::vector<size_t> tri_per_mat(model.materials.size(), 0);
        for (const auto &r : *outSubmeshes) {
            if (r.material_index >= 0 &&
                r.material_index < static_cast<int>(model.materials.size())) {
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
                   first_material < static_cast<int>(model.materials.size())) {
            outMaterial =
                (*outAllMaterials)[static_cast<size_t>(first_material)];
        }
    } else if (first_material >= 0) {
        fill_material(model, first_material, gltf_dir, outMaterial);
    }

    LUMEN_LOG_DEBUG("glTF 加载成功 {}: {} 顶点, {} 索引", path,
                    outMesh.vertices.size(), outMesh.indices.size());
    return true;
}

} // namespace core
} // namespace lumen
