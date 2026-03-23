/**
 * @file ktx_texture_rgba8.cpp
 * @brief libktx：KTX2 Basis 转 RGBA8；未压缩 RGBA8；行 pitch 收紧为紧密缓冲
 */

#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"

#include <ktx.h>

#include <cstring>
#include <string>

namespace lumen {
namespace core {

namespace {

bool ensure_tex_rgba8_cpu(ktxTexture *tex, std::string *err_out) {
    if (tex == nullptr) {
        if (err_out) {
            *err_out = "ktx: null texture";
        }
        return false;
    }
    if (tex->numFaces != 1 || tex->numLayers != 1 || tex->isCubemap) {
        if (err_out) {
            *err_out = "ktx: 仅支持 2D 单层非立方体贴图";
        }
        return false;
    }
    if (tex->classId == ktxTexture2_c) {
        auto *t2 = reinterpret_cast<ktxTexture2 *>(tex);
        if (ktxTexture2_NeedsTranscoding(t2)) {
            const KTX_error_code tr =
                ktxTexture2_TranscodeBasis(t2, KTX_TTF_RGBA32, 0);
            if (tr != KTX_SUCCESS) {
                if (err_out) {
                    *err_out = ktxErrorString(tr);
                }
                return false;
            }
        }
    } else if (tex->classId == ktxTexture1_c) {
        if (tex->isCompressed) {
            if (err_out) {
                *err_out = "ktx1: 压缩格式暂不支持，请使用 KTX2 或未压缩 KTX";
            }
            return false;
        }
    } else {
        if (err_out) {
            *err_out = "ktx: 未知纹理类";
        }
        return false;
    }

    const ktx_uint32_t elem = ktxTexture_GetElementSize(tex);
    if (elem != 4) {
        if (err_out) {
            *err_out = "ktx: 解码后需为 RGBA8（4 字节/像素）";
        }
        return false;
    }
    return true;
}

bool ensure_ktx2_payload_in_memory(ktxTexture *tex, std::string *err_out) {
    if (tex->classId != ktxTexture2_c) {
        return true;
    }
    auto *t2 = reinterpret_cast<ktxTexture2 *>(tex);
    if (t2->pData != nullptr) {
        return true;
    }
    const ktx_error_code_e ld = ktxTexture2_LoadImageData(t2, nullptr, 0);
    if (ld != KTX_SUCCESS) {
        if (err_out) {
            *err_out = ktxErrorString(static_cast<KTX_error_code>(ld));
        }
        return false;
    }
    return true;
}

bool copy_level0_rgba8(ktxTexture *tex, std::vector<std::uint8_t> &out_rgba,
                       std::uint32_t &out_width, std::uint32_t &out_height) {
    out_width = tex->baseWidth;
    out_height = tex->baseHeight;
    ktx_size_t offset = 0;
    const KTX_error_code e = ktxTexture_GetImageOffset(tex, 0, 0, 0, &offset);
    if (e != KTX_SUCCESS) {
        LUMEN_LOG_ERROR("ktxTexture_GetImageOffset: {}", ktxErrorString(e));
        return false;
    }
    const ktx_uint8_t *base = ktxTexture_GetData(tex);
    if (base == nullptr) {
        LUMEN_LOG_ERROR("ktx: GetData 为空");
        return false;
    }
    const ktx_uint8_t *src = base + offset;
    const ktx_uint32_t row_pitch = ktxTexture_GetRowPitch(tex, 0);
    const std::size_t tight = static_cast<std::size_t>(out_width) * 4u;
    if (row_pitch == tight) {
        out_rgba.resize(static_cast<std::size_t>(out_width) * out_height * 4u);
        std::memcpy(out_rgba.data(), src, out_rgba.size());
        return true;
    }
    out_rgba.resize(static_cast<std::size_t>(out_width) * out_height * 4u);
    for (std::uint32_t y = 0; y < out_height; ++y) {
        std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * tight,
                    src + static_cast<std::size_t>(y) * row_pitch, tight);
    }
    return true;
}

bool decode_common(ktxTexture *tex, std::vector<std::uint8_t> &out_rgba,
                   std::uint32_t &out_width, std::uint32_t &out_height,
                   std::string *err_out) {
    if (!ensure_ktx2_payload_in_memory(tex, err_out)) {
        ktxTexture_Destroy(tex);
        return false;
    }
    if (!ensure_tex_rgba8_cpu(tex, err_out)) {
        ktxTexture_Destroy(tex);
        return false;
    }
    const bool ok = copy_level0_rgba8(tex, out_rgba, out_width, out_height);
    ktxTexture_Destroy(tex);
    return ok;
}

} // namespace

bool decode_ktx_file_to_rgba8(const char *path,
                              std::vector<std::uint8_t> &out_rgba,
                              std::uint32_t &out_width,
                              std::uint32_t &out_height, std::string *err_out) {
    out_rgba.clear();
    out_width = 0;
    out_height = 0;
    if (path == nullptr || path[0] == '\0') {
        if (err_out) {
            *err_out = "空路径";
        }
        return false;
    }
    ktxTexture *tex = nullptr;
    const KTX_error_code e = ktxTexture_CreateFromNamedFile(
        path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
    if (e != KTX_SUCCESS || tex == nullptr) {
        if (err_out) {
            *err_out = ktxErrorString(e);
        }
        if (tex) {
            ktxTexture_Destroy(tex);
        }
        return false;
    }
    return decode_common(tex, out_rgba, out_width, out_height, err_out);
}

bool decode_ktx_memory_to_rgba8(const std::uint8_t *bytes, std::size_t size,
                                std::vector<std::uint8_t> &out_rgba,
                                std::uint32_t &out_width,
                                std::uint32_t &out_height,
                                std::string *err_out) {
    out_rgba.clear();
    out_width = 0;
    out_height = 0;
    if (bytes == nullptr || size == 0) {
        if (err_out) {
            *err_out = "空缓冲";
        }
        return false;
    }
    ktxTexture *tex = nullptr;
    const KTX_error_code e = ktxTexture_CreateFromMemory(
        bytes, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
    if (e != KTX_SUCCESS || tex == nullptr) {
        if (err_out) {
            *err_out = ktxErrorString(e);
        }
        if (tex) {
            ktxTexture_Destroy(tex);
        }
        return false;
    }
    return decode_common(tex, out_rgba, out_width, out_height, err_out);
}

} // namespace core
} // namespace lumen
