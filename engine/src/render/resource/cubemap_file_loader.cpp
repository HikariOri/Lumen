/**
 * @file cubemap_file_loader.cpp
 * @brief Cubemap 文件加载与 HDR 等距柱状图转换为环境贴图
 *
 * 功能：
 * 1. 从六张面图加载 Cubemap（px/nx/py/ny/pz/nz）
 * 2. 从 HDR equirectangular 图生成 Cubemap（CPU 采样重建）
 */

#include "render/resource/cubemap_file_loader.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"

#include <ghc/filesystem.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace lumen::render {
namespace {

/// 等距图经度是否再做半圈平移（绕 Y 旋转 180°），修正全景朝向与场景 −Z
/// 朝前不一致的问题。
constexpr bool kEquirectPanoramaHalfTurn { true };
/// false：天顶 +Y
/// 采图像上方行（θ/π）；true：对纬度镜像（1−θ/π），适合「图顶实际是地」或仍觉天地对调的资产。
constexpr bool kEquirectMirrorLatitude { true };
/// true：上传时对调 Vulkan 层 +Y 与 −Y（层 2、3）。仅修正「天顶与脚底两整块对调、侧面赤道仍对」。
constexpr bool kCubemapSwapYAxisFaces { true };

/**
 * ============================================================
 * Cubemap 工具集：HDR (Equirectangular) → Cubemap
 * ============================================================
 *
 * 流程：
 *   face + uv → direction → latlong uv → HDR采样
 *
 * 适用于：
 *   - IBL
 *   - Skybox
 *   - 环境贴图预处理
 */

/**
 * @brief Cubemap face + UV → 单位方向向量
 *
 * @param face 0~5：
 *  0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
 * @param u,v  ∈ [0,1]
 * @param dir  输出单位向量
 */
inline void face_uv_to_dir(int face, float u, float v, float dir[3]) {
    // 1. UV → [-1,1]
    float tu = u * 2.0f - 1.0f;
    float tv = 1.0f - v * 2.0f; // 注意 flip Y

    // 2. 根据 face 构造方向
    switch (face) {
    case 0:
        dir[0] = 1.f;
        dir[1] = -tv;
        dir[2] = -tu;
        break; // +X
    case 1:
        dir[0] = -1.f;
        dir[1] = -tv;
        dir[2] = tu;
        break; // -X
    case 2:
        dir[0] = tu;
        dir[1] = 1.f;
        dir[2] = tv;
        break; // +Y
    case 3:
        dir[0] = tu;
        dir[1] = -1.f;
        dir[2] = -tv;
        break; // -Y
    case 4:
        dir[0] = tu;
        dir[1] = -tv;
        dir[2] = 1.f;
        break; // +Z
    case 5:
        dir[0] = -tu;
        dir[1] = -tv;
        dir[2] = -1.f;
        break; // -Z
    default:
        dir[0] = 0;
        dir[1] = 1;
        dir[2] = 0;
        break;
    }

    // 3. 归一化（非常重要）
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-8f) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

/**
 * @brief 方向向量 → 经纬度 UV
 *
 * @param dir 单位方向
 * @param u,v 输出 [0,1]
 */
inline void dir_to_latlong_uv(const float dir[3], float *u, float *v) {
    constexpr float PI = std::numbers::pi_v<float>;
    constexpr float TWO_PI = 2.f * PI;

    const float phi = std::atan2(dir[2], dir[0]);
    const float theta =
        std::acos(std::clamp(dir[1], -1.f, 1.f)); // [0,π]，+Y 天顶为 0

    float u_lon = phi / TWO_PI + 0.5f;
    if (kEquirectPanoramaHalfTurn) {
        u_lon += 0.5f;
    }
    *u = u_lon - std::floor(u_lon);

    const float v_linear = theta / PI;
    *v = kEquirectMirrorLatitude ? (1.f - v_linear) : v_linear;
}

/**
 * @brief HDR latlong 双线性采样
 *
 * @param hdr RGBA float
 */
inline void sample_equirect_rgba(const float *hdr, int w, int h, float u,
                                 float v, float out[4]) {
    // 1. wrap/clamp
    u = u - std::floor(u);       // 横向循环
    v = std::clamp(v, 0.f, 1.f); // 纵向不循环

    if (w <= 1 || h <= 1) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }

    // 2. 像素坐标（texel center）
    float x = u * w - 0.5f;
    float y = v * h - 0.5f;

    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);

    float tx = x - x0;
    float ty = y - y0;

    // wrap X
    x0 = (x0 % w + w) % w;
    int x1 = (x0 + 1) % w;

    // clamp Y
    y0 = std::clamp(y0, 0, h - 1);
    int y1 = std::clamp(y0 + 1, 0, h - 1);

    auto idx = [w](int xi, int yi) { return (size_t(yi) * w + xi) * 4; };

    // 3. bilinear
    for (int c = 0; c < 4; ++c) {
        float s00 = hdr[idx(x0, y0) + c];
        float s10 = hdr[idx(x1, y0) + c];
        float s01 = hdr[idx(x0, y1) + c];
        float s11 = hdr[idx(x1, y1) + c];

        out[c] = (1 - tx) * (1 - ty) * s00 + tx * (1 - ty) * s10 +
                 (1 - tx) * ty * s01 + tx * ty * s11;
    }
}

/**
 * ============================================================
 * 核心函数：HDR → Cubemap
 * ============================================================
 *
 * @param hdr 输入HDR
 * @param face_size cubemap每面分辨率
 * @param out 6个face输出（连续内存）
 */
inline void
convert_equirect_to_cubemap(const float *hdr, int w, int h, int face_size,
                            float *out // size = 6 * face_size * face_size * 4
) {
    float dir[3];
    float uv_u, uv_v;
    float color[4];

    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < face_size; ++y) {
            for (int x = 0; x < face_size; ++x) {

                float u = (x + 0.5f) / face_size;
                float v = (y + 0.5f) / face_size;

                // 1. face uv → dir
                face_uv_to_dir(face, u, v, dir);

                // 2. dir → latlong uv
                dir_to_latlong_uv(dir, &uv_u, &uv_v);

                // 3. HDR采样
                sample_equirect_rgba(hdr, w, h, uv_u, uv_v, color);

                // 4. 写入
                size_t idx =
                    ((size_t)face * face_size * face_size + y * face_size + x) *
                    4;

                for (int c = 0; c < 4; ++c)
                    out[idx + c] = color[c];
            }
        }
    }
}

} // namespace

// ============================================================
// 1. 六面文件加载 Cubemap
// ============================================================

/**
 * @brief 从 px/nx/py/ny/pz/nz 六张图片构建 Cubemap
 *
 * 流程：
 * 1. 读取六张 face 图
 * 2. 检查尺寸一致
 * 3. 上传 GPU cubemap
 */
bool load_cubemap_from_face_files(const Context &ctx, const std::string &dir,
                                  VkQueue transfer_queue, CommandPool &cmd_pool,
                                  const SamplerConfig &sampler_cfg,
                                  Texture &out_tex, std::string *out_error) {
    namespace fs = ghc::filesystem;

    std::error_code ec;
    fs::path root = fs::absolute(dir, ec);

    if (ec || !fs::is_directory(root, ec)) {
        if (out_error) {
            *out_error = "不是有效目录: " + dir;
        }
        return false;
    }

    static constexpr std::array<const char *, 6> kStem = { "px", "nx", "py",
                                                           "ny", "pz", "nz" };

    static constexpr std::array<const char *, 2> kExt = { ".png", ".jpg" };

    std::array<std::vector<std::uint8_t>, 6> face_rgba;

    int face_w {};
    int face_h {};

    // 读取六张图
    for (size_t f = 0; f < 6; ++f) {

        fs::path chosen;

        for (auto ext : kExt) {
            fs::path p = root / (std::string(kStem[f]) + ext);
            if (fs::is_regular_file(p, ec)) {
                chosen = p;
                break;
            }
        }

        if (chosen.empty()) {
            if (out_error) {
                *out_error = "缺少 face: " + std::string(kStem[f]);
            }
            return false;
        }

        int w {};
        int h {};
        int ch {};
        stbi_set_flip_vertically_on_load(0);

        stbi_uc *pix =
            stbi_load(chosen.string().c_str(), &w, &h, &ch, STBI_rgb_alpha);

        if (!pix) {
            if (out_error) {
                *out_error = "加载失败: " + chosen.string();
            }
            return false;
        }

        if (f == 0) {
            face_w = w;
            face_h = h;
        } else if (w != face_w || h != face_h) {
            stbi_image_free(pix);
            if (out_error) {
                *out_error = "六面尺寸不一致";
            }
            return false;
        }

        face_rgba[f].assign(pix, pix + w * h * 4);
        stbi_image_free(pix);
    }

    const void *ptrs[6] { face_rgba[0].data(), face_rgba[1].data(),
                          face_rgba[2].data(), face_rgba[3].data(),
                          face_rgba[4].data(), face_rgba[5].data() };

    const auto dim = (uint32_t)face_w;

    // GPU 上传 Cubemap
    return out_tex.create_cubemap_from_rgba8_faces(
        ctx, ptrs, dim, transfer_queue, cmd_pool, sampler_cfg);
}

// ============================================================
// 2. HDR → Cubemap（CPU 重建采样版）
// ============================================================

/**
 * @brief 从 HDR equirectangular 生成 Cubemap
 *
 * 核心流程：
 * 1. 读取 HDR
 * 2. 对每个 face 每个像素：
 *    - face UV → direction
 *    - direction → latlong UV
 *    - HDR 采样
 * 3. GPU 上传 float cubemap
 */
bool load_cubemap_from_hdr_equirectangular_file(
    const Context &ctx, const std::string &hdr_path, VkQueue transfer_queue,
    CommandPool &cmd_pool, const SamplerConfig &sampler_cfg, Texture &out_tex,
    std::uint32_t face_size, std::string *out_error) {
    namespace fs = ghc::filesystem;

    std::error_code ec;
    if (!fs::is_regular_file(fs::path(hdr_path), ec)) {
        if (out_error) {
            *out_error = "HDR 无效路径";
        }
        return false;
    }

    stbi_set_flip_vertically_on_load(0);

    int iw = 0;
    int ih = 0;
    int ch = 0;
    float *pix = stbi_loadf(hdr_path.c_str(), &iw, &ih, &ch, STBI_rgb_alpha);

    if (!pix) {
        if (out_error) {
            *out_error = "HDR 解析失败";
        }
        return false;
    }

    std::vector<float> hdr(pix, pix + iw * ih * 4);
    stbi_image_free(pix);

    if (face_size == 0) {
        face_size = std::clamp(iw / 4, 128, 1024);
    }

    std::array<std::vector<float>, 6> faces;
    for (auto &f : faces) {
        f.resize(face_size * face_size * 4);
    }

    // 重建 cubemap
    for (int face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < face_size; ++y) {
            for (uint32_t x = 0; x < face_size; ++x) {

                float u = (x + 0.5f) / face_size;
                float v = (y + 0.5f) / face_size;

                float dir[3];
                face_uv_to_dir(face, u, v, dir);

                float eu;
                float ev;
                dir_to_latlong_uv(dir, &eu, &ev);

                float s[4];
                sample_equirect_rgba(hdr.data(), iw, ih, eu, ev, s);

                size_t idx = (y * face_size + x) * 4;
                faces[face][idx + 0] = s[0];
                faces[face][idx + 1] = s[1];
                faces[face][idx + 2] = s[2];
                faces[face][idx + 3] = s[3];
            }
        }
    }

    const float *face_py = faces[2].data();
    const float *face_ny = faces[3].data();
    const void *ptrs[6] {
        faces[0].data(),
        faces[1].data(),
        kCubemapSwapYAxisFaces ? face_ny : face_py,
        kCubemapSwapYAxisFaces ? face_py : face_ny,
        faces[4].data(),
        faces[5].data(),
    };

    return out_tex.create_cubemap_from_rgba32f_faces(
        ctx, ptrs, face_size, transfer_queue, cmd_pool, sampler_cfg);
}

} // namespace lumen::render
