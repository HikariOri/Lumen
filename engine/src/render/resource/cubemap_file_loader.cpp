/**
 * @file cubemap_file_loader.cpp
 */

#include "render/resource/cubemap_file_loader.hpp"
#include "core/logger.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/sampler.hpp"
#include "render/resource/texture.hpp"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

namespace lumen::render {
namespace {

void face_uv_to_dir(int face, float u, float v, float dir[3]) {
    const float tu = u * 2.f - 1.f;
    const float tv = 1.f - v * 2.f;
    switch (face) {
    case 0:
        dir[0] = 1.f;
        dir[1] = -tv;
        dir[2] = -tu;
        break;
    case 1:
        dir[0] = -1.f;
        dir[1] = -tv;
        dir[2] = tu;
        break;
    case 2:
        dir[0] = tu;
        dir[1] = 1.f;
        dir[2] = tv;
        break;
    case 3:
        dir[0] = tu;
        dir[1] = -1.f;
        dir[2] = -tv;
        break;
    case 4:
        dir[0] = tu;
        dir[1] = -tv;
        dir[2] = 1.f;
        break;
    case 5:
        dir[0] = -tu;
        dir[1] = -tv;
        dir[2] = -1.f;
        break;
    default:
        dir[0] = 0.f;
        dir[1] = 1.f;
        dir[2] = 0.f;
        break;
    }
    const float len =
        std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-12f) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

void dir_to_latlong_uv(const float dir[3], float *u, float *v) {
    const float phi = std::atan2(dir[2], dir[0]);
    const float theta = std::acos(std::clamp(dir[1], -1.f, 1.f));
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kPi = 3.14159265359f;
    *u = phi / kTwoPi + 0.5f;
    // 与 stb 解码后的行顺序及常见 .hdr 约定一致：v=0 对应贴图「上侧」一行（天顶）
    *v = 1.f - theta / kPi;
}

void sample_equirect_rgba(const float *hdr, int w, int h, float u, float v,
                          float out[4]) {
    u = u - std::floor(u);
    v = std::clamp(v, 0.f, 1.f);
    if (w <= 1 || h <= 1) {
        out[0] = out[1] = out[2] = out[3] = 0.f;
        return;
    }
    const float x = u * static_cast<float>(w) - 0.5f;
    const float y = v * static_cast<float>(h) - 0.5f;
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    x0 = ((x0 % w) + w) % w;
    const int x1 = (x0 + 1) % w;
    y0 = std::clamp(y0, 0, h - 1);
    const int y1 = std::clamp(y0 + 1, 0, h - 1);

    const auto idx = [w](int xi, int yi) {
        return (static_cast<size_t>(yi) * static_cast<size_t>(w) +
                static_cast<size_t>(xi)) *
               4;
    };
    for (int c = 0; c < 4; ++c) {
        const float s00 = hdr[idx(x0, y0) + static_cast<size_t>(c)];
        const float s10 = hdr[idx(x1, y0) + static_cast<size_t>(c)];
        const float s01 = hdr[idx(x0, y1) + static_cast<size_t>(c)];
        const float s11 = hdr[idx(x1, y1) + static_cast<size_t>(c)];
        out[static_cast<size_t>(c)] =
            (1.f - tx) * (1.f - ty) * s00 + tx * (1.f - ty) * s10 +
            (1.f - tx) * ty * s01 + tx * ty * s11;
    }
}

} // namespace

bool load_cubemap_from_face_files(const Context &ctx, const std::string &dir,
                                  VkQueue transfer_queue, CommandPool &cmd_pool,
                                  const SamplerConfig &sampler_cfg, Texture &out_tex,
                                  std::string *out_error) {
    namespace fs = std::filesystem;
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
    int face_w = 0;
    int face_h = 0;

    for (size_t f = 0; f < 6; ++f) {
        fs::path chosen;
        for (const char *ext : kExt) {
            fs::path p = root / (std::string(kStem[f]) + ext);
            if (fs::is_regular_file(p, ec)) {
                chosen = std::move(p);
                break;
            }
        }
        if (chosen.empty()) {
            if (out_error) {
                *out_error = "缺少面文件: " + root.string() + "/" + kStem[f] +
                             ".png 或 .jpg";
            }
            return false;
        }

        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        stbi_uc *pix =
            stbi_load(chosen.string().c_str(), &w, &h, &ch, STBI_rgb_alpha);
        if (!pix || w <= 0 || h <= 0) {
            if (pix)
                stbi_image_free(pix);
            if (out_error) {
                *out_error = "无法解码: " + chosen.string();
            }
            return false;
        }
        if (f == 0) {
            face_w = w;
            face_h = h;
        } else if (w != face_w || h != face_h) {
            stbi_image_free(pix);
            if (out_error) {
                *out_error = "六面尺寸不一致: " + chosen.string();
            }
            return false;
        }
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        face_rgba[f].assign(pix, pix + bytes);
        stbi_image_free(pix);
    }

    const void *ptrs[6] = {
        face_rgba[0].data(), face_rgba[1].data(), face_rgba[2].data(),
        face_rgba[3].data(), face_rgba[4].data(), face_rgba[5].data(),
    };
    const std::uint32_t dim = static_cast<std::uint32_t>(face_w);
    if (!out_tex.create_cubemap_from_rgba8_faces(ctx, ptrs, dim, transfer_queue,
                                                 cmd_pool, sampler_cfg)) {
        if (out_error) {
            *out_error = "create_cubemap_from_rgba8_faces 失败";
        }
        LUMEN_LOG_ERROR("立方体贴图 GPU 上传失败: {}", dir);
        return false;
    }
    LUMEN_LOG_INFO("已加载环境立方体: {} ({}px)", dir, dim);
    return true;
}

bool load_cubemap_from_hdr_equirectangular_file(
    const Context &ctx, const std::string &hdr_path, VkQueue transfer_queue,
    CommandPool &cmd_pool, const SamplerConfig &sampler_cfg, Texture &out_tex,
    std::uint32_t face_size, std::string *out_error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(hdr_path), ec)) {
        if (out_error) {
            *out_error = "HDR 路径不是有效文件: " + hdr_path;
        }
        return false;
    }

    stbi_set_flip_vertically_on_load(0);
    int iw = 0;
    int ih = 0;
    int ch = 0;
    float *pix = stbi_loadf(hdr_path.c_str(), &iw, &ih, &ch, STBI_rgb_alpha);
    if (!pix || iw < 4 || ih < 4) {
        if (pix) {
            stbi_image_free(pix);
        }
        if (out_error) {
            *out_error = "无法解码 HDR（需 stb 支持的 .hdr）: " + hdr_path;
        }
        return false;
    }

    const int w = iw;
    const int h = ih;
    std::vector<float> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    std::memcpy(rgba.data(), pix,
                static_cast<size_t>(w) * static_cast<size_t>(h) * 4 *
                    sizeof(float));
    stbi_image_free(pix);

    if (face_size == 0) {
        face_size = static_cast<std::uint32_t>(std::clamp(iw / 4, 128, 1024));
    }
    if (face_size < 8) {
        if (out_error) {
            *out_error = "立方体面分辨率过小";
        }
        return false;
    }

    const size_t face_texels =
        static_cast<size_t>(face_size) * static_cast<size_t>(face_size);
    std::array<std::vector<float>, 6> faces {};
    for (auto &f : faces) {
        f.resize(face_texels * 4);
    }

    for (int face = 0; face < 6; ++face) {
        for (std::uint32_t row = 0; row < face_size; ++row) {
            for (std::uint32_t col = 0; col < face_size; ++col) {
                const float u =
                    (static_cast<float>(col) + 0.5f) /
                    static_cast<float>(face_size);
                const float v =
                    (static_cast<float>(row) + 0.5f) /
                    static_cast<float>(face_size);
                float dir[3];
                face_uv_to_dir(face, u, v, dir);
                float eu = 0.f;
                float ev = 0.f;
                dir_to_latlong_uv(dir, &eu, &ev);
                float s[4];
                sample_equirect_rgba(rgba.data(), w, h, eu, ev, s);
                const size_t o = (static_cast<size_t>(row) * face_size + col) * 4;
                faces[static_cast<size_t>(face)][o + 0] = s[0];
                faces[static_cast<size_t>(face)][o + 1] = s[1];
                faces[static_cast<size_t>(face)][o + 2] = s[2];
                faces[static_cast<size_t>(face)][o + 3] = s[3];
            }
        }
    }

    const void *ptrs[6] = {
        faces[0].data(), faces[1].data(), faces[2].data(),
        faces[3].data(), faces[4].data(), faces[5].data(),
    };
    if (!out_tex.create_cubemap_from_rgba32f_faces(ctx, ptrs, face_size,
                                                   transfer_queue, cmd_pool,
                                                   sampler_cfg)) {
        if (out_error) {
            *out_error = "create_cubemap_from_rgba32f_faces 失败";
        }
        LUMEN_LOG_ERROR("HDR 环境立方体 GPU 上传失败: {}", hdr_path);
        return false;
    }
    LUMEN_LOG_INFO("已加载 HDR 环境: {} ({}px/面)", hdr_path, face_size);
    return true;
}

} // namespace lumen::render
