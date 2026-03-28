/**
 * @file main.cpp
 * @brief glTF PBR 示例：在单文件内完成解析、贴图解码、HDR IBL 与渲染循环。
 *
 * 约定：
 * - glTF：`tinygltf` 直接读盘；`TINYGLTF_IMPLEMENTATION` 在引擎 `gltf_loader.cpp`，此处仅
 *   include 头文件；图像走 `SetImageLoader`（stb）或 URI + `stbi_load`。
 * - 顶点已在 CPU 乘节点世界矩阵，与 `pbr.vert` 中 `normalMatrix = I` 一致。
 * - 描述符：set0 场景 UBO + 辐射度 cubemap + BRDF LUT + 辐照度 cubemap；set1 每材质 UBO +
 *   五张贴图（与 `pbr.frag` 一致）。
 *
 * 代码结构：`anonymous namespace` 内为可复用辅助函数；`run_gltf_pbr` 为线性初始化 +
 * 帧循环；`main` 仅日志生命周期。
 */

#include "engine.hpp"

#include "core/obj_loader.hpp"
#include "core/path.hpp"
#include "core/time.hpp"
#include "platform/event_pump.hpp"
#include "platform/window.hpp"
#include "render/material_texture_mask.hpp"
#include "render/pbr_material_ubo.hpp"
#include "render/resource/descriptor.hpp"
#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/resource/texture.hpp"
#include "scene/light.hpp"
#include "scene/scene_orbit_camera.hpp"

#include <stb_image.h>

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include <ghc/filesystem.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace fs = ghc::filesystem;

using Vertex = lumen::core::ObjVertex;

namespace {

// ---------------------------------------------------------------------------
// 常量与 GPU 对齐结构（与 `pbr.vert` / `pbr.frag` 中 UBO 布局一致，std140）
// ---------------------------------------------------------------------------

constexpr uint32_t kMaxFramesInFlight { 2 };
constexpr const char *kSponzaGltfRel { "assets/model/Sponza/glTF/Sponza.gltf" };
constexpr const char *kHdrEquirectRel {
    "assets/environment_maps/meadow_2_2k.hdr"
};

/// 合并网格后按 glTF primitive / 材质分段的索引范围，用于 `vkCmdDrawIndexed` 分批绑定 set1。
struct SubmeshRange {
    std::uint32_t first_index {};
    std::uint32_t index_count {};
    int material_index { -1 };
};

struct SceneUbo {
    glm::mat4 model { 1.F };
    glm::mat4 mvp { 1.F };
    glm::mat4 normal_matrix { 1.F };
    glm::vec4 camera_world { 0.F, 0.F, 0.F, 0.F };
    lumen::scene::GPULight lights[lumen::scene::kMaxLightsUbo] {};
    glm::vec4 scene_params { 0.F, 0.F, 0.F, 0.F };
    glm::mat4 sky_mvp { 1.F };
    glm::mat4 sky_orient_inv { 1.F };
    glm::vec4 env_params { 1.F, 0.F, 0.F, 1.F };
};

// ---------------------------------------------------------------------------
// tinygltf：扩展名判断与嵌入图 / URI 的 stb 解码（供 `SetImageLoader`）
// ---------------------------------------------------------------------------

bool ends_with_ci(std::string_view s, std::string_view ext) {
    if (s.size() < ext.size()) {
        return false;
    }
    for (size_t i = 0; i < ext.size(); ++i) {
        const auto a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(s[s.size() - ext.size() + i])));
        const auto b =
            static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool tinygltf_load_image_stb(tinygltf::Image *image, const int /*image_idx*/,
                             std::string *err, std::string * /*warn*/,
                             int /*req_width*/, int /*req_height*/,
                             const unsigned char *bytes, int size,
                             void *user_data) {
    auto *gltf_dir = static_cast<fs::path *>(user_data);
    stbi_set_flip_vertically_on_load(0);
    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char *pix = nullptr;
    if (bytes != nullptr && size > 0) {
        pix = stbi_load_from_memory(bytes, size, &w, &h, &comp, STBI_rgb_alpha);
    } else if (!image->uri.empty()) {
        const fs::path abs =
            (*gltf_dir / fs::path(image->uri)).lexically_normal();
        pix = stbi_load(abs.string().c_str(), &w, &h, &comp, STBI_rgb_alpha);
    } else {
        if (err != nullptr) {
            *err = "tinygltf image: 无 buffer 且无 uri";
        }
        return false;
    }
    if (pix == nullptr) {
        if (err != nullptr) {
            *err = std::string("stbi_load: ") + stbi_failure_reason();
        }
        return false;
    }
    image->width = w;
    image->height = h;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    std::memcpy(image->image.data(), pix, image->image.size());
    stbi_image_free(pix);
    return true;
}

// ---------------------------------------------------------------------------
// HDR 等距柱状图 ↔ cubemap：方向与双线性采样（CPU 侧，与常见 equirect 约定一致）
// ---------------------------------------------------------------------------

void face_uv_to_dir(int face, float u, float v, float dir[3]) {
    const float tu = u * 2.F - 1.F;
    const float tv = 1.F - v * 2.F;
    switch (face) {
    case 0:
        dir[0] = 1.F;
        dir[1] = -tv;
        dir[2] = -tu;
        break;
    case 1:
        dir[0] = -1.F;
        dir[1] = -tv;
        dir[2] = tu;
        break;
    case 2:
        dir[0] = tu;
        dir[1] = 1.F;
        dir[2] = tv;
        break;
    case 3:
        dir[0] = tu;
        dir[1] = -1.F;
        dir[2] = -tv;
        break;
    case 4:
        dir[0] = tu;
        dir[1] = -tv;
        dir[2] = 1.F;
        break;
    default:
        dir[0] = -tu;
        dir[1] = -tv;
        dir[2] = -1.F;
        break;
    }
    const float len =
        std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-8F) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

void dir_to_latlong_uv(const float dir[3], float *u, float *v) {
    const float phi = std::atan2(dir[2], dir[0]);
    const float theta = std::acos(std::clamp(dir[1], -1.F, 1.F));
    constexpr float k_two_pi = 2.F * std::numbers::pi_v<float>;
    *u = phi / k_two_pi + 0.5F;
    *v = 1.F - theta / std::numbers::pi_v<float>;
}

void sample_equirect_rgba(const float *hdr, int w, int h, float u, float v,
                          float out[4]) {
    u = u - std::floor(u);
    v = std::clamp(v, 0.F, 1.F);
    if (w <= 1 || h <= 1) {
        out[0] = out[1] = out[2] = out[3] = 0.F;
        return;
    }
    float x = u * static_cast<float>(w) - 0.5F;
    float y = v * static_cast<float>(h) - 0.5F;
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    x0 = (x0 % w + w) % w;
    int x1 = (x0 + 1) % w;
    y0 = std::clamp(y0, 0, h - 1);
    int y1 = std::clamp(y0 + 1, 0, h - 1);
    auto idx = [w](int xi, int yi) {
        return (static_cast<size_t>(yi) * static_cast<size_t>(w) +
                static_cast<size_t>(xi)) *
               4u;
    };
    for (int c = 0; c < 4; ++c) {
        const float s00 = hdr[idx(x0, y0) + c];
        const float s10 = hdr[idx(x1, y0) + c];
        const float s01 = hdr[idx(x0, y1) + c];
        const float s11 = hdr[idx(x1, y1) + c];
        out[c] = (1.F - tx) * (1.F - ty) * s00 + tx * (1.F - ty) * s10 +
                 (1.F - tx) * ty * s01 + tx * ty * s11;
    }
}

glm::vec3 sample_hdr_rgb(const float *hdr, int w, int h, const glm::vec3 &dir) {
    float d[3] = { dir.x, dir.y, dir.z };
    float uu = 0.F;
    float vv = 0.F;
    dir_to_latlong_uv(d, &uu, &vv);
    float c[4];
    sample_equirect_rgba(hdr, w, h, uu, vv, c);
    return glm::vec3(c[0], c[1], c[2]);
}

void fill_cubemap_face_rgba32f_from_equirect(const float *hdr, int ew, int eh,
                                             int face, std::uint32_t face_size,
                                             float *face_rgba_out) {
    for (std::uint32_t y = 0; y < face_size; ++y) {
        for (std::uint32_t x = 0; x < face_size; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5F) / static_cast<float>(face_size);
            const float v =
                (static_cast<float>(y) + 0.5F) / static_cast<float>(face_size);
            float dir[3];
            face_uv_to_dir(face, u, v, dir);
            float uu = 0.F;
            float vv = 0.F;
            dir_to_latlong_uv(dir, &uu, &vv);
            float px[4];
            sample_equirect_rgba(hdr, ew, eh, uu, vv, px);
            const size_t o =
                (static_cast<size_t>(y) * static_cast<size_t>(face_size) +
                 static_cast<size_t>(x)) *
                4u;
            face_rgba_out[o + 0] = px[0];
            face_rgba_out[o + 1] = px[1];
            face_rgba_out[o + 2] = px[2];
            face_rgba_out[o + 3] = px[3];
        }
    }
}

// ---------------------------------------------------------------------------
// IBL 预计算：Hammersley、GGX 重要性采样、BRDF LUT（与 fragment 中 split-sum 配套）
// ---------------------------------------------------------------------------

constexpr float k_pi = 3.14159265358979323846F;

float radical_inverse_vdc(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

glm::vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    return glm::vec2(static_cast<float>(i) / static_cast<float>(n),
                     radical_inverse_vdc(i));
}

float geometry_schlick_ggx(float ndotv, float roughness) {
    const float r = roughness + 1.F;
    const float k = (r * r) / 8.F;
    return ndotv / std::max(ndotv * (1.F - k) + k, 1e-7F);
}

float geometry_smith(const glm::vec3 &N, const glm::vec3 &V, const glm::vec3 &L,
                     float roughness) {
    const float ndotv = glm::clamp(glm::dot(N, V), 0.F, 1.F);
    const float ndotl = glm::clamp(glm::dot(N, L), 0.F, 1.F);
    return geometry_schlick_ggx(ndotv, roughness) *
           geometry_schlick_ggx(ndotl, roughness);
}

glm::vec3 importance_sample_ggx(const glm::vec2 &xi, const glm::vec3 &N,
                                float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.F * k_pi * xi.x;
    const float cos_theta =
        std::sqrt(std::max(0.F, (1.F - xi.y) / (1.F + (a * a - 1.F) * xi.y)));
    const float sin_theta =
        std::sqrt(std::max(0.F, 1.F - cos_theta * cos_theta));
    glm::vec3 Ht;
    Ht.x = std::cos(phi) * sin_theta;
    Ht.y = std::sin(phi) * sin_theta;
    Ht.z = cos_theta;
    const glm::vec3 up = std::abs(N.z) < 0.999F ? glm::vec3(0.F, 0.F, 1.F)
                                                : glm::vec3(1.F, 0.F, 0.F);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    const glm::vec3 bitangent = glm::cross(N, tangent);
    return glm::normalize(tangent * Ht.x + bitangent * Ht.y + N * Ht.z);
}

glm::vec2 integrate_brdf(float ndotv, float roughness) {
    glm::vec3 V {};
    V.x = std::sqrt(std::max(0.F, 1.F - ndotv * ndotv));
    V.y = 0.F;
    V.z = ndotv;
    const glm::vec3 N(0.F, 0.F, 1.F);
    float A = 0.F;
    float B = 0.F;
    constexpr std::uint32_t k_samples = 1024u;
    for (std::uint32_t i = 0; i < k_samples; ++i) {
        const glm::vec2 xi = hammersley(i, k_samples);
        const glm::vec3 H = importance_sample_ggx(xi, N, roughness);
        const glm::vec3 L = glm::normalize(2.F * glm::dot(V, H) * H - V);
        const float ndotl = glm::clamp(L.z, 0.F, 1.F);
        const float ndoth = glm::clamp(H.z, 0.F, 1.F);
        const float vdoth = glm::clamp(glm::dot(V, H), 0.F, 1.F);
        if (ndotl > 0.F) {
            const float G = geometry_smith(N, V, L, roughness);
            const float G_vis = (G * vdoth) / std::max(ndoth * ndotv, 1e-7F);
            const float Fc = std::pow(1.F - vdoth, 5.F);
            A += (1.F - Fc) * G_vis;
            B += Fc * G_vis;
        }
    }
    return glm::vec2(A, B) / static_cast<float>(k_samples);
}

void generate_brdf_lut_rgba8(std::vector<std::uint8_t> &out_rgba,
                             std::uint32_t resolution) {
    out_rgba.resize(static_cast<size_t>(resolution) *
                    static_cast<size_t>(resolution) * 4u);
    for (std::uint32_t y = 0; y < resolution; ++y) {
        const float roughness = std::max(
            static_cast<float>(y) / static_cast<float>(resolution), 1e-4F);
        for (std::uint32_t x = 0; x < resolution; ++x) {
            const float ndotv = std::max(
                static_cast<float>(x) / static_cast<float>(resolution), 1e-4F);
            const glm::vec2 ab = integrate_brdf(ndotv, roughness);
            const size_t o =
                (static_cast<size_t>(y) * static_cast<size_t>(resolution) +
                 static_cast<size_t>(x)) *
                4u;
            out_rgba[o + 0] = static_cast<std::uint8_t>(
                glm::clamp(ab.x, 0.F, 1.F) * 255.F + 0.5F);
            out_rgba[o + 1] = static_cast<std::uint8_t>(
                glm::clamp(ab.y, 0.F, 1.F) * 255.F + 0.5F);
            out_rgba[o + 2] = 0;
            out_rgba[o + 3] = 255;
        }
    }
}

// 辐照度 cubemap：对每个 texel 的法线方向做余弦加权半球积分（采样 HDR equirect）。

glm::vec3 cubemap_texel_dir(std::uint32_t face, std::uint32_t px,
                            std::uint32_t py, std::uint32_t face_size) {
    const float u =
        (static_cast<float>(px) + 0.5F) / static_cast<float>(face_size) * 2.F -
        1.F;
    const float v =
        (static_cast<float>(py) + 0.5F) / static_cast<float>(face_size) * 2.F -
        1.F;
    glm::vec3 d {};
    switch (face) {
    case 0: d = glm::vec3(1.F, -v, -u); break;
    case 1: d = glm::vec3(-1.F, -v, u); break;
    case 2: d = glm::vec3(u, 1.F, v); break;
    case 3: d = glm::vec3(u, -1.F, -v); break;
    case 4: d = glm::vec3(u, -v, 1.F); break;
    default: d = glm::vec3(-u, -v, -1.F); break;
    }
    return glm::normalize(d);
}

glm::vec3 cosine_sample_hemisphere(const glm::vec3 &N, const glm::vec2 &xi) {
    const float r = std::sqrt(xi.x);
    const float phi = 2.F * k_pi * xi.y;
    const glm::vec3 hl(r * std::cos(phi), r * std::sin(phi),
                       std::sqrt(std::max(0.F, 1.F - xi.x)));
    const glm::vec3 up = std::abs(N.y) < 0.999F ? glm::vec3(0.F, 1.F, 0.F)
                                                : glm::vec3(1.F, 0.F, 0.F);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    const glm::vec3 bitangent = glm::cross(N, tangent);
    return glm::normalize(tangent * hl.x + bitangent * hl.y + N * hl.z);
}

glm::vec3 irradiance_at_hdr(const float *hdr, int w, int h, const glm::vec3 &N,
                            std::uint32_t samples) {
    glm::vec3 acc(0.F);
    for (std::uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 xi = hammersley(i, samples);
        const glm::vec3 wi = cosine_sample_hemisphere(N, xi);
        const float ndotw = glm::max(glm::dot(N, wi), 0.F);
        acc += sample_hdr_rgb(hdr, w, h, wi) * ndotw;
    }
    return acc * (k_pi / static_cast<float>(samples));
}

// ---------------------------------------------------------------------------
// glTF 网格：节点变换、accessor 读取、合并为单 `ObjMesh` + `SubmeshRange` 列表
// ---------------------------------------------------------------------------

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

std::uint32_t read_index(const tinygltf::Model &model,
                         const tinygltf::Accessor &acc, size_t i) {
    const tinygltf::BufferView &view = model.bufferViews[acc.bufferView];
    const tinygltf::Buffer &buf = model.buffers[view.buffer];
    const size_t stride = static_cast<size_t>(acc.ByteStride(view));
    size_t el = 4u;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        el = 2u;
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        el = 1u;
    }
    const size_t eff_stride = stride != 0 ? stride : el;
    const size_t off =
        static_cast<size_t>(view.byteOffset + acc.byteOffset) + i * eff_stride;
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        std::uint8_t v = 0;
        std::memcpy(&v, buf.data.data() + off, sizeof(v));
        return v;
    }
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
    const auto stride = static_cast<size_t>(acc.ByteStride(view));
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
                      lumen::core::ObjMesh &out,
                      std::vector<SubmeshRange> *submeshes) {
    const int mode = prim.mode;
    if (mode != TINYGLTF_MODE_TRIANGLES && mode != -1) {
        return;
    }
    auto pit = prim.attributes.find("POSITION");
    if (pit == prim.attributes.end()) {
        return;
    }
    const std::uint32_t submesh_first_index =
        submeshes != nullptr ? static_cast<std::uint32_t>(out.indices.size())
                             : 0u;
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
    const glm::mat3 n_world = glm::transpose(glm::inverse(glm::mat3(world)));
    const std::uint32_t vbase = static_cast<std::uint32_t>(out.vertices.size());
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
        out.vertices.push_back(Vertex { wp, n, uv });
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
    if (submeshes != nullptr) {
        const std::uint32_t total =
            static_cast<std::uint32_t>(out.indices.size());
        if (total > submesh_first_index) {
            submeshes->push_back(SubmeshRange { submesh_first_index,
                                                total - submesh_first_index,
                                                prim.material });
        }
    }
}

void append_mesh(const tinygltf::Model &model, int mesh_idx,
                 const glm::mat4 &world, lumen::core::ObjMesh &out,
                 std::vector<SubmeshRange> *submeshes) {
    if (mesh_idx < 0 || mesh_idx >= static_cast<int>(model.meshes.size())) {
        return;
    }
    const tinygltf::Mesh &mesh = model.meshes[static_cast<size_t>(mesh_idx)];
    for (const auto &prim : mesh.primitives) {
        append_primitive(model, prim, world, out, submeshes);
    }
}

void traverse_nodes_to_mesh(const tinygltf::Model &model, int node_idx,
                            const glm::mat4 &parent, lumen::core::ObjMesh &out,
                            std::vector<SubmeshRange> *submeshes) {
    if (node_idx < 0 || node_idx >= static_cast<int>(model.nodes.size())) {
        return;
    }
    const tinygltf::Node &node = model.nodes[static_cast<size_t>(node_idx)];
    const glm::mat4 world = parent * node_local_matrix(node);
    if (node.mesh >= 0) {
        append_mesh(model, node.mesh, world, out, submeshes);
    }
    for (int c : node.children) {
        traverse_nodes_to_mesh(model, c, world, out, submeshes);
    }
}

glm::vec4 gltf_base_color_factor(const tinygltf::Material &m) {
    const auto &p = m.pbrMetallicRoughness.baseColorFactor;
    if (p.size() >= 4) {
        return glm::vec4(static_cast<float>(p[0]), static_cast<float>(p[1]),
                         static_cast<float>(p[2]), static_cast<float>(p[3]));
    }
    return glm::vec4(1.F, 1.F, 1.F, 1.F);
}

// ---------------------------------------------------------------------------
// 材质贴图：`tinygltf::Image` 内存或磁盘 URI → `Texture::create_from_memory`（stb 读盘）
// ---------------------------------------------------------------------------

bool load_gltf_image_to_texture(
    const tinygltf::Model &model, const fs::path &gltf_dir, int image_idx,
    const lumen::render::Context &ctx, VkQueue transfer_queue,
    lumen::render::CommandPool &cmd_pool, lumen::render::Texture &out_tex,
    VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB) {
    if (image_idx < 0 || image_idx >= static_cast<int>(model.images.size())) {
        return false;
    }
    const tinygltf::Image &im = model.images[static_cast<size_t>(image_idx)];
    if (!im.image.empty() && im.width > 0 && im.height > 0) {
        return out_tex.create_from_memory(ctx, im.image.data(), im.image.size(),
                                          static_cast<std::uint32_t>(im.width),
                                          static_cast<std::uint32_t>(im.height),
                                          transfer_queue, cmd_pool,
                                          image_format, {}, true);
    }
    if (!im.uri.empty()) {
        const fs::path abs = (gltf_dir / fs::path(im.uri)).lexically_normal();
        std::error_code ec;
        if (!fs::exists(abs, ec)) {
            return false;
        }
        int w = 0;
        int h = 0;
        int comp = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char *pix =
            stbi_load(abs.string().c_str(), &w, &h, &comp, STBI_rgb_alpha);
        if (pix == nullptr) {
            return false;
        }
        const size_t bytes =
            static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
        const bool ok = out_tex.create_from_memory(
            ctx, pix, bytes, static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), transfer_queue, cmd_pool,
            image_format, {}, true);
        stbi_image_free(pix);
        return ok;
    }
    return false;
}

bool load_gltf_texture_index(const tinygltf::Model &model,
                             const fs::path &gltf_dir, int tex_index,
                             const lumen::render::Context &ctx,
                             VkQueue transfer_queue,
                             lumen::render::CommandPool &cmd_pool,
                             lumen::render::Texture &out_tex,
                             VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB) {
    if (tex_index < 0 || tex_index >= static_cast<int>(model.textures.size())) {
        return false;
    }
    const int src = model.textures[static_cast<size_t>(tex_index)].source;
    return load_gltf_image_to_texture(model, gltf_dir, src, ctx, transfer_queue,
                                      cmd_pool, out_tex, image_format);
}

lumen::render::PbrMaterialUbo
pbr_ubo_from_gltf_material(const tinygltf::Material &m,
                           std::uint32_t tex_mask) {
    lumen::render::PbrMaterialUbo u {};
    const auto &pbr = m.pbrMetallicRoughness;
    u.base_color_factor = gltf_base_color_factor(m);
    u.mr_ao_factors =
        glm::vec4(static_cast<float>(pbr.metallicFactor),
                  static_cast<float>(pbr.roughnessFactor),
                  static_cast<float>(m.occlusionTexture.strength), 0.F);
    if (m.emissiveFactor.size() >= 3) {
        u.emissive_factor =
            glm::vec4(static_cast<float>(m.emissiveFactor[0]),
                      static_cast<float>(m.emissiveFactor[1]),
                      static_cast<float>(m.emissiveFactor[2]), 0.F);
    }
    if (m.alphaMode == "MASK") {
        u.shader_params.x = 1.F;
    } else if (m.alphaMode == "BLEND") {
        u.shader_params.x = 2.F;
    }
    u.shader_params.y = static_cast<float>(m.alphaCutoff);
    u.shader_params.z = lumen::render::uint_bits_to_float(tex_mask);
    return u;
}

void write_material_descriptor_images(
    VkDevice dev, VkDescriptorSet set,
    const lumen::render::PbrPlaceholderTextures &ph,
    const lumen::render::Texture *albedo_override,
    const lumen::render::Texture *normal_override,
    const lumen::render::Texture *mr_override,
    const lumen::render::Texture *ao_override,
    const lumen::render::Texture *emissive_override) {
    auto one = [&](const lumen::render::Texture *ov,
                   const lumen::render::Texture &fallback,
                   std::uint32_t binding) {
        const lumen::render::Texture &use =
            (ov && ov->is_valid()) ? *ov : fallback;
        lumen::render::write_descriptor_image(dev, set, binding, use.view(),
                                              use.sampler());
    };
    one(albedo_override, ph.albedo(), 1);
    one(normal_override, ph.normal(), 2);
    one(mr_override, ph.metallic_roughness(), 3);
    one(ao_override, ph.ao(), 4);
    one(emissive_override, ph.emissive(), 5);
}

} // namespace

/**
 * @brief 创建窗口 → 加载 glTF 与 HDR → 初始化 Vulkan 与 IBL → 主循环直出 swapchain。
 */
static int run_gltf_pbr() {
    // ---------- 窗口（须先于 Vulkan Instance / Surface）----------
    lumen::platform::Window window;
    lumen::platform::WindowConfig win_cfg;

    win_cfg.title = "gltf_pbr — tinygltf + stb HDR IBL";
    win_cfg.width = 1280;
    win_cfg.height = 720;
    win_cfg.icon_path =
        lumen::core::get_resource_path("assets/icons/哈士奇.png");

    if (!window.create(win_cfg)) {
        LUMEN_APP_LOG_ERROR("窗口创建失败");
        return -1;
    }

    // ---------- glTF：tinygltf 解析，合并场景为单网格 + 按材质子网格 ----------
    const std::string model_path =
        lumen::core::get_resource_path(kSponzaGltfRel);

    const std::string hdr_path =
        lumen::core::get_resource_path(kHdrEquirectRel);

    LUMEN_APP_LOG_INFO("模型: {}", model_path);
    LUMEN_APP_LOG_INFO("HDR: {}", hdr_path);

    fs::path gltf_dir_for_loader = fs::path(model_path).parent_path();
    tinygltf::Model model {};
    tinygltf::TinyGLTF loader {};
    loader.SetImageLoader(tinygltf_load_image_stb,
                          static_cast<void *>(&gltf_dir_for_loader));
    std::string err;
    std::string warn;
    const bool is_glb = ends_with_ci(model_path, ".glb");
    const bool load_ok =
        is_glb ? loader.LoadBinaryFromFile(&model, &err, &warn, model_path)
               : loader.LoadASCIIFromFile(&model, &err, &warn, model_path);

    if (!warn.empty()) {
        LUMEN_APP_LOG_WARN("tinygltf: {}", warn);
    }
    if (!err.empty()) {
        LUMEN_APP_LOG_ERROR("tinygltf: {}", err);
    }
    if (!load_ok) {
        LUMEN_APP_LOG_ERROR("加载 glTF 失败");
        return -1;
    }

    int scene_index = model.defaultScene;
    if (scene_index < 0 ||
        scene_index >= static_cast<int>(model.scenes.size())) {
        scene_index = model.scenes.empty() ? -1 : 0;
    }
    if (scene_index < 0) {
        LUMEN_APP_LOG_ERROR("无场景");
        return -1;
    }

    lumen::core::ObjMesh mesh {};
    std::vector<SubmeshRange> submeshes {};
    const tinygltf::Scene &scene =
        model.scenes[static_cast<size_t>(scene_index)];
    const glm::mat4 root(1.F);
    for (int root_node : scene.nodes) {
        traverse_nodes_to_mesh(model, root_node, root, mesh, &submeshes);
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        LUMEN_APP_LOG_ERROR("网格为空");
        return -1;
    }
    if (submeshes.empty()) {
        submeshes.push_back(SubmeshRange {
            0, static_cast<std::uint32_t>(mesh.indices.size()), -1 });
    }

    glm::vec3 mesh_center { 0.F };
    glm::vec3 mesh_half_extents { 1.F };
    {
        glm::vec3 mn = mesh.vertices[0].position;
        glm::vec3 mx = mn;
        for (const Vertex &v : mesh.vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        mesh_center = 0.5f * (mn + mx);
        mesh_half_extents = 0.5f * (mx - mn);
    }

    // ---------- HDR 环境：stb 浮点读入，供 CPU 生成辐射度 / 辐照度 cubemap ----------
    int hdr_w = 0;
    int hdr_h = 0;
    int hdr_c = 0;
    stbi_set_flip_vertically_on_load(0);
    float *hdr_floats = stbi_loadf(hdr_path.c_str(), &hdr_w, &hdr_h, &hdr_c, 4);
    if (hdr_floats == nullptr || hdr_w < 2 || hdr_h < 2) {
        LUMEN_APP_LOG_ERROR("stbi_loadf HDR 失败: {}", hdr_path);
        if (hdr_floats != nullptr) {
            stbi_image_free(hdr_floats);
        }
        return -1;
    }

    // ---------- Vulkan：Instance / Device / Swapchain / RenderPass / 深度 / Framebuffer ----------
    lumen::render::ContextConfig ctx_cfg;
    lumen::render::Context ctx;
    if (!ctx.init_instance(ctx_cfg, window)) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("Vulkan Instance 初始化失败");
        return -1;
    }
    lumen::render::Surface surface(ctx, window);
    if (!surface.is_valid() || !ctx.init_device(surface.handle())) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("Device 初始化失败");
        return -1;
    }

    int w { 0 }, h { 0 };
    window.get_framebuffer_size(&w, &h);
    lumen::render::Swapchain swapchain;
    if (!swapchain.create(ctx, surface.handle(), static_cast<uint32_t>(w),
                          static_cast<uint32_t>(h))) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("Swapchain 创建失败");
        return -1;
    }

    lumen::render::RenderPassConfig rp_cfg;
    rp_cfg.useDepth = true;
    rp_cfg.colorAttachment.format = swapchain.image_format();
    lumen::render::RenderPass render_pass;
    if (!render_pass.create(ctx.device(), rp_cfg)) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("RenderPass 创建失败");
        return -1;
    }

    lumen::render::Image depth_image;
    if (!depth_image.create_depth_attachment(ctx, static_cast<uint32_t>(w),
                                             static_cast<uint32_t>(h))) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("深度附件创建失败");
        return -1;
    }

    lumen::render::Framebuffer framebuffers;
    if (!framebuffers.create(ctx.device(), render_pass.handle(), swapchain,
                             depth_image.view())) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("Framebuffer 创建失败");
        return -1;
    }

    std::string vert_spv =
        lumen::core::get_resource_path("shaders/pbr.vert.spv");
    std::string frag_spv =
        lumen::core::get_resource_path("shaders/pbr.frag.spv");
    lumen::render::ShaderModule vert_shader;
    lumen::render::ShaderModule frag_shader;
    if (!vert_shader.create_from_file(ctx.device(), vert_spv.c_str()) ||
        !frag_shader.create_from_file(ctx.device(), frag_spv.c_str())) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("着色器加载失败 vert={} frag={}", vert_spv,
                            frag_spv);
        return -1;
    }

    // ---------- 几何上传（顶点布局：position, uv, normal → 与 `pbr.vert` location 对应）----------
    const size_t v_bytes = mesh.vertices.size() * sizeof(Vertex);
    const size_t i_bytes = mesh.indices.size() * sizeof(std::uint32_t);
    lumen::render::VertexBuffer vertex_buffer;
    lumen::render::IndexBuffer index_buffer;
    if (!vertex_buffer.create(ctx, v_bytes) ||
        !index_buffer.create(ctx, i_bytes)) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("几何 Buffer 创建失败");
        return -1;
    }
    index_buffer.set_index_type(lumen::render::IndexBuffer::IndexType::Uint32);
    vertex_buffer.upload(mesh.vertices.data(), v_bytes);
    index_buffer.upload(mesh.indices.data(), i_bytes);

    lumen::render::CommandPool cmd_pool;
    if (!cmd_pool.create(ctx, ctx.graphics_queue_family())) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("CommandPool 创建失败");
        return -1;
    }

    lumen::render::PbrPlaceholderTextures placeholder_textures;
    if (!placeholder_textures.create(ctx, ctx.graphics_queue(), cmd_pool)) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("PBR 占位纹理创建失败");
        return -1;
    }

    // ---------- IBL GPU 资源：辐射度 RGBA32F cubemap（引擎侧生成 mip，供 `textureLod` 粗糙度）----------
    constexpr std::uint32_t k_radiance_face { 256 };
    std::array<std::vector<float>, 6> rad_faces {};
    const size_t face_floats =
        static_cast<size_t>(k_radiance_face) * k_radiance_face * 4u;
    for (auto &f : rad_faces) {
        f.resize(face_floats);
    }
    for (int fi = 0; fi < 6; ++fi) {
        fill_cubemap_face_rgba32f_from_equirect(hdr_floats, hdr_w, hdr_h, fi,
                                                k_radiance_face,
                                                rad_faces[fi].data());
    }

    const void *rad_ptrs[6] = { rad_faces[0].data(), rad_faces[1].data(),
                                rad_faces[2].data(), rad_faces[3].data(),
                                rad_faces[4].data(), rad_faces[5].data() };
    lumen::render::Texture env_cubemap;
    lumen::render::SamplerConfig env_sampler_cfg {};
    env_sampler_cfg.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    env_sampler_cfg.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!env_cubemap.create_cubemap_from_rgba32f_faces(
            ctx, rad_ptrs, k_radiance_face, ctx.graphics_queue(), cmd_pool,
            env_sampler_cfg)) {
        stbi_image_free(hdr_floats);
        LUMEN_APP_LOG_ERROR("辐射度环境立方体创建失败");
        return -1;
    }

    // ---------- 辐照度 cubemap（CPU 积分，面分辨率与样本数可权衡速度/质量）----------
    constexpr std::uint32_t k_irr_face { 32 };
    constexpr std::uint32_t k_irr_samples { 128 };
    std::array<std::vector<float>, 6> irr_faces {};
    const size_t irr_floats = static_cast<size_t>(k_irr_face) * k_irr_face * 4u;
    for (auto &f : irr_faces) {
        f.resize(irr_floats);
    }
    for (std::uint32_t face = 0; face < 6u; ++face) {
        for (std::uint32_t py = 0; py < k_irr_face; ++py) {
            for (std::uint32_t px = 0; px < k_irr_face; ++px) {
                const glm::vec3 N = cubemap_texel_dir(face, px, py, k_irr_face);
                const glm::vec3 e = irradiance_at_hdr(hdr_floats, hdr_w, hdr_h,
                                                      N, k_irr_samples);
                const size_t o =
                    (static_cast<size_t>(py) * static_cast<size_t>(k_irr_face) +
                     static_cast<size_t>(px)) *
                    4u;
                irr_faces[face][o + 0] = e.r;
                irr_faces[face][o + 1] = e.g;
                irr_faces[face][o + 2] = e.b;
                irr_faces[face][o + 3] = 1.F;
            }
        }
    }
    stbi_image_free(hdr_floats);
    hdr_floats = nullptr;

    const void *irr_ptrs[6] = { irr_faces[0].data(), irr_faces[1].data(),
                                irr_faces[2].data(), irr_faces[3].data(),
                                irr_faces[4].data(), irr_faces[5].data() };
    lumen::render::Texture irradiance_cubemap;
    if (!irradiance_cubemap.create_cubemap_from_rgba32f_faces(
            ctx, irr_ptrs, k_irr_face, ctx.graphics_queue(), cmd_pool,
            env_sampler_cfg)) {
        LUMEN_APP_LOG_ERROR("辐照度立方体创建失败");
        return -1;
    }

    // ---------- BRDF 积分 LUT（R8G8 存 scale/bias，与 fragment `texture(uBrdfLut)` 一致）----------
    std::vector<std::uint8_t> brdf_lut_rgba;
    generate_brdf_lut_rgba8(brdf_lut_rgba, 256);
    lumen::render::Texture brdf_lut_tex;
    if (!brdf_lut_tex.create_from_memory(
            ctx, brdf_lut_rgba.data(), brdf_lut_rgba.size(), 256, 256,
            ctx.graphics_queue(), cmd_pool, VK_FORMAT_R8G8B8A8_UNORM,
            lumen::render::SamplerConfig {}, false)) {
        LUMEN_APP_LOG_ERROR("BRDF LUT 创建失败");
        return -1;
    }

    // ---------- 按 glTF 材质索引加载贴图并写 `PbrMaterialUbo` + 占位纹理回退 ----------
    const fs::path gltf_dir { fs::path(model_path).parent_path() };
    const size_t nmat = model.materials.size();
    const size_t n_tex_sets = std::max(nmat + 1, size_t { 1 });

    std::vector<lumen::render::Texture> mat_albedo(nmat);
    std::vector<lumen::render::Texture> mat_normal(nmat);
    std::vector<lumen::render::Texture> mat_mr(nmat);
    std::vector<lumen::render::Texture> mat_ao(nmat);
    std::vector<lumen::render::Texture> mat_emissive(nmat);
    std::vector<lumen::render::PbrMaterialUbo> mat_ubo_cpu(n_tex_sets);

    for (size_t mi = 0; mi < nmat; ++mi) {
        const tinygltf::Material &tm = model.materials[mi];
        std::uint32_t mask = 0;
        if (load_gltf_texture_index(
                model, gltf_dir, tm.pbrMetallicRoughness.baseColorTexture.index,
                ctx, ctx.graphics_queue(), cmd_pool, mat_albedo[mi],
                VK_FORMAT_R8G8B8A8_SRGB)) {
            mask |= lumen::render::kMatTexBitAlbedo;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.normalTexture.index,
                                    ctx, ctx.graphics_queue(), cmd_pool,
                                    mat_normal[mi], VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitNormal;
        }
        if (load_gltf_texture_index(
                model, gltf_dir,
                tm.pbrMetallicRoughness.metallicRoughnessTexture.index, ctx,
                ctx.graphics_queue(), cmd_pool, mat_mr[mi],
                VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitMetallicRoughness;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.occlusionTexture.index,
                                    ctx, ctx.graphics_queue(), cmd_pool,
                                    mat_ao[mi], VK_FORMAT_R8G8B8A8_UNORM)) {
            mask |= lumen::render::kMatTexBitOcclusion;
        }
        if (load_gltf_texture_index(model, gltf_dir, tm.emissiveTexture.index,
                                    ctx, ctx.graphics_queue(), cmd_pool,
                                    mat_emissive[mi],
                                    VK_FORMAT_R8G8B8A8_SRGB)) {
            mask |= lumen::render::kMatTexBitEmissive;
        }
        mat_ubo_cpu[mi] = pbr_ubo_from_gltf_material(tm, mask);
    }
    mat_ubo_cpu[n_tex_sets - 1] = lumen::render::PbrMaterialUbo {};

    std::vector<lumen::render::UniformBuffer> mat_uniform_buffers(n_tex_sets);
    for (size_t si = 0; si < n_tex_sets; ++si) {
        if (!mat_uniform_buffers[si].create(
                ctx, sizeof(lumen::render::PbrMaterialUbo))) {
            LUMEN_APP_LOG_ERROR("材质 UniformBuffer 创建失败");
            return -1;
        }
        mat_uniform_buffers[si].update(mat_ubo_cpu[si]);
    }

    // ---------- 描述符：双 layout（场景 IBL + 材质）、pool 按「帧数 × 场景 + 材质套数」分配 ----------
    std::array<lumen::render::UniformBuffer, kMaxFramesInFlight>
        scene_uniform_buffers;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!scene_uniform_buffers[i].create(ctx, sizeof(SceneUbo))) {
            LUMEN_APP_LOG_ERROR("场景 UniformBuffer 创建失败");
            return -1;
        }
    }

    lumen::render::DescriptorSetLayout scene_desc_layout;
    if (!scene_desc_layout.create(
            ctx,
            { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
              { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT },
              { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        LUMEN_APP_LOG_ERROR("场景 DescriptorSetLayout 创建失败");
        return -1;
    }

    lumen::render::DescriptorSetLayout material_desc_layout;
    if (!material_desc_layout.create(
            ctx, { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT },
                   { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT } })) {
        LUMEN_APP_LOG_ERROR("材质 DescriptorSetLayout 创建失败");
        return -1;
    }

    const auto n_mat_sets_u32 = static_cast<uint32_t>(n_tex_sets);
    lumen::render::DescriptorPool desc_pool;
    if (!desc_pool.create(ctx,
                          { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              kMaxFramesInFlight + n_mat_sets_u32 },
                            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              kMaxFramesInFlight * 3U + n_mat_sets_u32 * 5U } },
                          kMaxFramesInFlight + n_mat_sets_u32)) {
        LUMEN_APP_LOG_ERROR("DescriptorPool 创建失败");
        return -1;
    }

    std::array<VkDescriptorSet, kMaxFramesInFlight> scene_descriptor_sets {};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!desc_pool.allocate(ctx.device(), scene_desc_layout.handle(),
                                scene_descriptor_sets[i])) {
            LUMEN_APP_LOG_ERROR("场景 DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_set(
            ctx.device(), scene_descriptor_sets[i],
            { { .binding = 0,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .buffer = scene_uniform_buffers[i].handle(),
                .offset = 0,
                .range = sizeof(SceneUbo) } },
            { { .binding = 1,
                .imageView = env_cubemap.view(),
                .sampler = env_cubemap.sampler() },
              { .binding = 2,
                .imageView = brdf_lut_tex.view(),
                .sampler = brdf_lut_tex.sampler() },
              { .binding = 3,
                .imageView = irradiance_cubemap.view(),
                .sampler = irradiance_cubemap.sampler() } });
    }

    std::vector<VkDescriptorSet> mat_descriptor_sets(n_tex_sets);
    for (size_t si = 0; si < n_tex_sets; ++si) {
        if (!desc_pool.allocate(ctx.device(), material_desc_layout.handle(),
                                mat_descriptor_sets[si])) {
            LUMEN_APP_LOG_ERROR("材质 DescriptorSet 分配失败");
            return -1;
        }
        lumen::render::write_descriptor_buffer(
            ctx.device(), mat_descriptor_sets[si], 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mat_uniform_buffers[si].handle(),
            0, sizeof(lumen::render::PbrMaterialUbo));
        const lumen::render::Texture *pa =
            (si < nmat) ? &mat_albedo[si] : nullptr;
        const lumen::render::Texture *pn =
            (si < nmat) ? &mat_normal[si] : nullptr;
        const lumen::render::Texture *pmr = (si < nmat) ? &mat_mr[si] : nullptr;
        const lumen::render::Texture *pao = (si < nmat) ? &mat_ao[si] : nullptr;
        const lumen::render::Texture *pem =
            (si < nmat) ? &mat_emissive[si] : nullptr;
        write_material_descriptor_images(ctx.device(), mat_descriptor_sets[si],
                                         placeholder_textures, pa, pn, pmr, pao,
                                         pem);
    }

    // ---------- 图形管线：背面剔除，深度测写；与 `ObjVertex` 成员顺序及 shader location 对齐 ----------
    lumen::render::PipelineLayout pipeline_layout;
    if (!pipeline_layout.create(
            ctx, { scene_desc_layout.handle(), material_desc_layout.handle() },
            {})) {
        LUMEN_APP_LOG_ERROR("PipelineLayout 创建失败");
        return -1;
    }

    lumen::render::GraphicsPipelineConfig pipe_cfg;
    pipe_cfg.shaderStages.push_back(
        { vert_shader.handle(), VK_SHADER_STAGE_VERTEX_BIT, "main" });
    pipe_cfg.shaderStages.push_back(
        { frag_shader.handle(), VK_SHADER_STAGE_FRAGMENT_BIT, "main" });
    pipe_cfg.vertexBindings.push_back(
        { 0, sizeof(Vertex), lumen::render::VertexInputRate::PerVertex });
    pipe_cfg.vertexAttributes.push_back(
        { 0, 0, lumen::render::VertexAttributeKind::F32Vec3,
          offsetof(Vertex, position) });
    pipe_cfg.vertexAttributes.push_back(
        { 1, 0, lumen::render::VertexAttributeKind::F32Vec2,
          offsetof(Vertex, uv) });
    pipe_cfg.vertexAttributes.push_back(
        { 2, 0, lumen::render::VertexAttributeKind::F32Vec3,
          offsetof(Vertex, normal) });
    pipe_cfg.depthTest = true;
    pipe_cfg.depthWrite = true;
    pipe_cfg.cullMode = VK_CULL_MODE_BACK_BIT;
    pipe_cfg.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    lumen::render::GraphicsPipeline pipeline;
    if (!pipeline.create(ctx, pipeline_layout, render_pass, 0, pipe_cfg)) {
        LUMEN_APP_LOG_ERROR("GraphicsPipeline 创建失败");
        return -1;
    }

    auto cmd_buffers = cmd_pool.allocate(kMaxFramesInFlight);
    if (cmd_buffers.size() != kMaxFramesInFlight) {
        LUMEN_APP_LOG_ERROR("CommandBuffer 分配失败");
        return -1;
    }

    lumen::render::FrameSync frame_sync;
    if (!frame_sync.create(ctx.device(), swapchain.image_count(),
                           kMaxFramesInFlight)) {
        LUMEN_APP_LOG_ERROR("FrameSync 创建失败");
        return -1;
    }

    // ---------- 相机与输入：枢轴在包围盒中心，半径按场景尺度适配 ----------
    lumen::scene::SceneOrbitCamera cam;
    cam.set_pivot(mesh_center);
    lumen::scene::SceneOrbitCameraLimits lim = cam.limits();
    lim.max_radius = 800.F;
    lim.min_radius = 0.2F;
    cam.set_limits(lim);
    const float fit_r = glm::max(1.F, glm::length(mesh_half_extents) * 2.8F);
    cam.set_radius(fit_r);
    cam.set_yaw(0.9F);
    cam.set_pitch(0.45F);
    cam.set_depth_range(0.05F, 2000.F);

    lumen::core::anchor_steady_epoch();
    lumen::core::FrameDeltaClock frame_dt;
    lumen::platform::EventPump pump;
    uint32_t current_frame { 0 };
    bool running { true };
    int fb_width { w }, fb_height { h };
    bool need_recreate_swapchain { false };
    float mouse_wheel_dy { 0.F };

    pump.on_quit([&] { running = false; });
    pump.on_key_down([&](const lumen::platform::EventKeyDown &e) {
        if (e.key == lumen::platform::Key::Escape) {
            running = false;
        }
    });
    pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel &e) {
        mouse_wheel_dy += e.deltaY;
    });
    pump.on_window_resize([&](const lumen::platform::EventWindowResize &r) {
        fb_width = r.width;
        fb_height = r.height;
        need_recreate_swapchain = true;
    });

    constexpr uint64_t k_acquire_timeout_ns = 100'000'000;
    constexpr uint64_t k_fence_wait_timeout_ns = 16'000'000;

    LUMEN_APP_LOG_INFO("WASD 平移枢轴，QE 旋转 yaw，滚轮缩放；ESC 退出");

    // ---------- 主循环：`kMaxFramesInFlight` 轮转 fence / CB；acquire → 录 Pass → submit → present ----------
    while (running) {
        if (!pump.poll()) {
            break;
        }
        // 窗口尺寸变化时重建 swapchain、深度与 framebuffer（与 `FrameSync` 约定一致）
        if (need_recreate_swapchain) {
            window.get_framebuffer_size(&fb_width, &fb_height);
            if (fb_width > 0 && fb_height > 0) {
                lumen::render::Image new_depth;
                if (new_depth.create_depth_attachment(
                        ctx, static_cast<uint32_t>(fb_width),
                        static_cast<uint32_t>(fb_height)) &&
                    lumen::render::recreate_swapchain_resources(
                        ctx, swapchain, framebuffers, frame_sync, render_pass,
                        static_cast<uint32_t>(fb_width),
                        static_cast<uint32_t>(fb_height), kMaxFramesInFlight,
                        new_depth.view())) {
                    depth_image = std::move(new_depth);
                }
            }
            need_recreate_swapchain = false;
            continue;
        }

        while (!frame_sync.wait_fence(current_frame, k_fence_wait_timeout_ns)) {
            if (!pump.poll()) {
                running = false;
                break;
            }
            SDL_Delay(1);
        }
        if (!running) {
            break;
        }

        const auto dt = static_cast<float>(frame_dt.tick_seconds());
        const auto &inp = pump.input();
        const float move_s = 2.F * dt;
        glm::vec3 pivot = cam.pivot();
        if (inp.is_key_down(lumen::platform::Key::W)) {
            pivot.z -= move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::S)) {
            pivot.z += move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::A)) {
            pivot.x -= move_s;
        }
        if (inp.is_key_down(lumen::platform::Key::D)) {
            pivot.x += move_s;
        }
        cam.set_pivot(pivot);
        if (inp.is_key_down(lumen::platform::Key::Q)) {
            cam.set_yaw(cam.yaw() - 1.2F * dt);
        }
        if (inp.is_key_down(lumen::platform::Key::E)) {
            cam.set_yaw(cam.yaw() + 1.2F * dt);
        }
        cam.apply_scroll_zoom(mouse_wheel_dy, 2.F);
        mouse_wheel_dy = 0.F;

        const float aspect =
            static_cast<float>(swapchain.extent().width) /
            std::max(1.F, static_cast<float>(swapchain.extent().height));
        const glm::mat4 proj = cam.projection_matrix(aspect);
        const glm::mat4 view = cam.view_matrix();
        const glm::mat4 view_proj = proj * view;

        uint32_t image_index = swapchain.acquire_next_image(
            frame_sync.image_available(current_frame), VK_NULL_HANDLE,
            k_acquire_timeout_ns);
        if (image_index == UINT32_MAX) {
            continue;
        }

        vkResetCommandBuffer(cmd_buffers[current_frame], 0);
        VkCommandBufferBeginInfo begin_info {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd_buffers[current_frame], &begin_info) !=
            VK_SUCCESS) {
            continue;
        }

        VkClearValue clear_values[2];
        clear_values[0].color = { { 0.08f, 0.09f, 0.12f, 1.0f } };
        clear_values[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp_begin {
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rp_begin.renderPass = render_pass.handle();
        rp_begin.framebuffer = framebuffers.get(image_index);
        rp_begin.renderArea.offset = { 0, 0 };
        rp_begin.renderArea.extent = swapchain.extent();
        rp_begin.clearValueCount = 2;
        rp_begin.pClearValues = clear_values;

        vkCmdBeginRenderPass(cmd_buffers[current_frame], &rp_begin,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport {};
        viewport.x = 0.F;
        viewport.y = 0.F;
        viewport.width = static_cast<float>(swapchain.extent().width);
        viewport.height = static_cast<float>(swapchain.extent().height);
        viewport.minDepth = 0.F;
        viewport.maxDepth = 1.F;
        vkCmdSetViewport(cmd_buffers[current_frame], 0, 1, &viewport);

        VkRect2D scissor {};
        scissor.offset = { 0, 0 };
        scissor.extent = swapchain.extent();
        vkCmdSetScissor(cmd_buffers[current_frame], 0, 1, &scissor);

        vkCmdBindPipeline(cmd_buffers[current_frame],
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());

        // 场景 UBO：`GPULight` 的 `position.w` 为 0/1/2（方向光 / 点光 / 聚光）；`env_params` 为曝光、辐射度 max mip、IBL 强度
        SceneUbo scene_ubo {};
        scene_ubo.model = glm::mat4(1.F);
        scene_ubo.mvp = view_proj;
        scene_ubo.normal_matrix = glm::mat4(1.F);
        scene_ubo.camera_world = glm::vec4(cam.eye_position(), 0.F);

        std::uint32_t li = 0;
        scene_ubo.lights[li].position = glm::vec4(0.F, 0.F, 0.F, 0.F);
        scene_ubo.lights[li].direction =
            glm::vec4(glm::normalize(glm::vec3(0.35F, 1.F, 0.28F)), 0.F);
        scene_ubo.lights[li].color = glm::vec4(1.F, 0.97F, 0.93F, 5.F);
        scene_ubo.lights[li].params = glm::vec4(0.F);
        ++li;

        scene_ubo.lights[li].position = glm::vec4(0.F, 0.F, 0.F, 0.F);
        scene_ubo.lights[li].direction =
            glm::vec4(glm::normalize(glm::vec3(-0.55F, 0.25F, -0.45F)), 0.F);
        scene_ubo.lights[li].color = glm::vec4(0.55F, 0.62F, 0.85F, 1.4F);
        scene_ubo.lights[li].params = glm::vec4(0.F);
        ++li;

        scene_ubo.lights[li].position =
            glm::vec4(mesh_center + glm::vec3(3.F, 5.F, 2.F), 1.F);
        scene_ubo.lights[li].direction = glm::vec4(0.F);
        scene_ubo.lights[li].color = glm::vec4(1.F, 0.88F, 0.72F, 380.F);
        scene_ubo.lights[li].params = glm::vec4(28.F, 0.F, 0.F, 0.F);
        ++li;

        scene_ubo.lights[li].position =
            glm::vec4(mesh_center + glm::vec3(-4.F, 7.F, -1.F), 2.F);
        scene_ubo.lights[li].direction =
            glm::vec4(glm::normalize(glm::vec3(0.35F, -0.82F, 0.2F)), 0.F);
        scene_ubo.lights[li].color = glm::vec4(0.75F, 0.9F, 1.F, 520.F);
        scene_ubo.lights[li].params =
            glm::vec4(32.F, glm::cos(glm::radians(28.F)),
                      glm::cos(glm::radians(18.F)), 0.F);
        ++li;

        scene_ubo.scene_params =
            glm::vec4(static_cast<float>(li), 0.F, 0.F, 0.F);
        const glm::mat4 view_rot = glm::mat4(glm::mat3(view));
        scene_ubo.sky_mvp = proj * view_rot;
        scene_ubo.sky_orient_inv = glm::inverse(view_rot);
        const auto max_mip = static_cast<float>(
            env_cubemap.mip_levels() > 0 ? env_cubemap.mip_levels() - 1 : 0);
        scene_ubo.env_params = glm::vec4(1.15F, max_mip, 0.F, 1.35F);
        scene_uniform_buffers[current_frame].update(scene_ubo);

        VkBuffer vb = vertex_buffer.handle();
        VkDeviceSize vb_off { 0 };
        vkCmdBindVertexBuffers(cmd_buffers[current_frame], 0, 1, &vb, &vb_off);
        vkCmdBindIndexBuffer(cmd_buffers[current_frame], index_buffer.handle(),
                             0, index_buffer.vk_index_type());

        vkCmdBindDescriptorSets(
            cmd_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout.handle(), 0, 1,
            &scene_descriptor_sets[current_frame], 0, nullptr);

        // 按子网格材质切换 set1，默认最后一套为「空材质」占位
        for (const SubmeshRange &sub : submeshes) {
            const int mid = sub.material_index;
            size_t tex_set_i = n_tex_sets - 1;
            if (mid >= 0 && static_cast<size_t>(mid) < nmat) {
                tex_set_i = static_cast<size_t>(mid);
            }
            vkCmdBindDescriptorSets(
                cmd_buffers[current_frame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_layout.handle(), 1, 1, &mat_descriptor_sets[tex_set_i],
                0, nullptr);
            vkCmdDrawIndexed(cmd_buffers[current_frame], sub.index_count, 1,
                             sub.first_index, 0, 0);
        }

        vkCmdEndRenderPass(cmd_buffers[current_frame]);
        if (vkEndCommandBuffer(cmd_buffers[current_frame]) != VK_SUCCESS) {
            continue;
        }

        VkSubmitInfo submit_info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphore wait_sem = frame_sync.image_available(current_frame);
        VkSemaphore signal_sem = frame_sync.render_finished(image_index);
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &wait_sem;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffers[current_frame];
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &signal_sem;

        if (vkQueueSubmit(ctx.graphics_queue(), 1, &submit_info,
                          frame_sync.in_flight_fence(current_frame)) !=
            VK_SUCCESS) {
            continue;
        }

        VkResult pr =
            swapchain.present(ctx.present_queue(), image_index, signal_sem);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
            need_recreate_swapchain = true;
        }

        current_frame = (current_frame + 1) % kMaxFramesInFlight;
    }

    ctx.wait_idle();
    LUMEN_APP_LOG_INFO("gltf_pbr 退出");
    return 0;
}

/// 日志初始化 / 关闭由应用入口负责，便于其它示例复用同一模式。
int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }
    const int r = run_gltf_pbr();
    lumen::core::Logger::shutdown();
    return r;
}
