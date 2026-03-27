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

/**
 * @brief 计算 cube face + UV → 方向向量
 *
 * @param face 立方体面索引：
 * 0=+X(px), 1=-X(nx), 2=+Y(py), 3=-Y(ny), 4=+Z(pz), 5=-Z(nz)
 *
 * @param u/v 面内 UV [0,1]
 * @param dir 输出方向向量（归一化）
 *
 * @note 用于 HDR → Cubemap 重建采样
 */
void face_uv_to_dir(int face, float u, float v, float dir[3]) {
    const float tu = (u * 2.0F) - 1.F;
    const float tv = 1.F - (v * 2.F);

    switch (face) {
    case 0:
        dir[0] = 1.F;
        dir[1] = -tv;
        dir[2] = -tu;
        break; // +X
    case 1:
        dir[0] = -1.F;
        dir[1] = -tv;
        dir[2] = tu;
        break; // -X
    case 2:
        dir[0] = tu;
        dir[1] = 1.F;
        dir[2] = tv;
        break; // +Y
    case 3:
        dir[0] = tu;
        dir[1] = -1.F;
        dir[2] = -tv;
        break; // -Y
    case 4:
        dir[0] = tu;
        dir[1] = -tv;
        dir[2] = 1.F;
        break; // +Z
    case 5:
        dir[0] = -tu;
        dir[1] = -tv;
        dir[2] = -1.F;
        break; // -Z
    default:
        dir[0] = 0.F;
        dir[1] = 1.F;
        dir[2] = 0.F;
        break;
    }

    // normalize
    const float len =
        std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-12F) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

/**
 * @brief 方向向量 → 经纬度 UV（equirectangular）
 *
 * @param dir 输入方向（单位向量）
 * @param u 输出 [0,1]
 * @param v 输出 [0,1]
 */
void dir_to_latlong_uv(const float dir[3], float *u, float *v) {
    const float phi = std::atan2(dir[2], dir[0]);
    const float theta = std::acos(std::clamp(dir[1], -1.f, 1.f));

    constexpr float kTwoPi = 6.28318530718F;
    constexpr float kPi = std::numbers::pi_v<float>;

    *u = phi / kTwoPi + 0.5F;
    *v = 1.f - theta / kPi;
}

/**
 * @brief HDR equirectangular 图双线性采样
 *
 * @param hdr 输入 HDR 图（RGBA float）
 * @param w/h 分辨率
 * @param u/v UV
 * @param out 输出 RGBA
 */
void sample_equirect_rgba(const float *hdr, int w, int h, float u, float v,
                          float out[4]) {
    u = u - std::floor(u);
    v = std::clamp(v, 0.f, 1.f);

    if (w <= 1 || h <= 1) {
        out[0] = out[1] = out[2] = out[3] = 0.f;
        return;
    }

    const float x = u * w - 0.5f;
    const float y = v * h - 0.5f;

    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);

    const float tx = x - x0;
    const float ty = y - y0;

    x0 = ((x0 % w) + w) % w;
    const int x1 = (x0 + 1) % w;

    y0 = std::clamp(y0, 0, h - 1);
    const int y1 = std::clamp(y0 + 1, 0, h - 1);

    const auto idx = [w](int xi, int yi) {
        return (size_t(yi) * size_t(w) + size_t(xi)) * 4;
    };

    for (int c = 0; c < 4; ++c) {
        const float s00 = hdr[idx(x0, y0) + c];
        const float s10 = hdr[idx(x1, y0) + c];
        const float s01 = hdr[idx(x0, y1) + c];
        const float s11 = hdr[idx(x1, y1) + c];

        out[c] = (1 - tx) * (1 - ty) * s00 + tx * (1 - ty) * s10 +
                 (1 - tx) * ty * s01 + tx * ty * s11;
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

    const void *ptrs[6] { faces[0].data(), faces[1].data(), faces[2].data(),
                          faces[3].data(), faces[4].data(), faces[5].data() };

    return out_tex.create_cubemap_from_rgba32f_faces(
        ctx, ptrs, face_size, transfer_queue, cmd_pool, sampler_cfg);
}

} // namespace lumen::render
