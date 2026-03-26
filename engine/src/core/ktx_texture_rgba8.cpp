/**
 * @file ktx_texture_rgba8.cpp
 * @brief 基于 libktx 的 KTX/KTX2 → RGBA8 解码实现
 *
 * 功能：
 * - 支持 KTX1 / KTX2
 * - 自动处理 Basis 压缩（KTX2）
 * - 输出 CPU 可用 RGBA8（紧密排列）
 *
 * 核心流程：
 * ```
 * KTX/KTX2
 *   ↓
 * libktx 解析 container
 *   ↓
 * （必要时）转码 / 解压
 *   ↓
 * 提取 mip0
 *   ↓
 * 行对齐处理（row pitch → tight）
 *   ↓
 * RGBA8 buffer
 * ```
 */

#include "core/ktx_texture_rgba8.hpp"
#include "core/logger.hpp"

#include <ktx.h>

#include <cstring>
#include <string>

namespace lumen {
namespace core {

namespace {

/**
 * @brief 确保纹理在 CPU 侧为 RGBA8 可读格式
 *
 * @param tex ktxTexture 对象
 * @param err_out 错误输出
 * @return 是否成功
 *
 * @details
 * 处理逻辑：
 * 1. 限制类型（只支持 2D 非 cubemap）
 * 2. KTX2：
 *    - 若为 Basis 压缩 → 转码为 RGBA32
 * 3. KTX1：
 *    - 若为压缩格式 → 拒绝（未实现）
 * 4. 校验元素大小必须为 4 bytes（RGBA8）
 *
 * @note
 * - KTX2 可包含 Basis Universal 压缩，需要 runtime transcode
 * - element size = 每像素字节数
 */
bool ensure_tex_rgba8_cpu(ktxTexture *tex, std::string *err_out) {
    if (tex == nullptr) {
        if (err_out) {
            *err_out = "ktx: null texture";
        }
        return false;
    }

    // 只支持普通 2D 纹理
    if (tex->numFaces != 1 || tex->numLayers != 1 || tex->isCubemap) {
        if (err_out) {
            *err_out = "ktx: 仅支持 2D 单层非立方体贴图";
        }
        return false;
    }

    // -------- KTX2 --------
    if (tex->classId == ktxTexture2_c) {
        auto *t2 = reinterpret_cast<ktxTexture2 *>(tex);

        // 是否需要转码（Basis → RGBA）
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
    }
    // -------- KTX1 --------
    else if (tex->classId == ktxTexture1_c) {
        if (tex->isCompressed) {
            if (err_out) {
                *err_out = "ktx1: 压缩格式暂不支持";
            }
            return false;
        }
    } else {
        if (err_out) {
            *err_out = "ktx: 未知纹理类";
        }
        return false;
    }

    // 必须是 RGBA8
    const ktx_uint32_t elem = ktxTexture_GetElementSize(tex);
    if (elem != 4) {
        if (err_out) {
            *err_out = "ktx: 需要 RGBA8（4字节/像素）";
        }
        return false;
    }

    return true;
}

/**
 * @brief 确保 KTX2 图像数据已加载到内存
 *
 * @details
 * - KTX2 可能是 lazy load（pData == nullptr）
 * - 必须显式加载 image data
 */
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

/**
 * @brief 拷贝 mip0 图像并转为紧密 RGBA8 buffer
 *
 * @details
 * 关键问题：row pitch
 *
 * libktx 中：
 * - 每一行可能有 padding（对齐）
 * - rowPitch != width * 4
 *
 * 官方说明：
 * > row pitch = 行之间的字节间距（可能包含对齐填充）
 *
 * 因此需要：
 * - 如果已紧密 → 直接 memcpy
 * - 否则 → 按行拷贝（去 padding）
 */
bool copy_level0_rgba8(ktxTexture *tex, std::vector<std::uint8_t> &out_rgba,
                       std::uint32_t &out_width, std::uint32_t &out_height) {

    out_width = tex->baseWidth;
    out_height = tex->baseHeight;

    // 获取 mip0 offset
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

    // 行跨度（可能带 padding）
    const ktx_uint32_t row_pitch = ktxTexture_GetRowPitch(tex, 0);

    // 理想紧密大小
    const std::size_t tight = static_cast<std::size_t>(out_width) * 4U;

    // -------- 情况1：已经紧密 --------
    if (row_pitch == tight) {
        out_rgba.resize(static_cast<std::size_t>(out_width) * out_height * 4U);

        std::memcpy(out_rgba.data(), src, out_rgba.size());
        return true;
    }

    // -------- 情况2：有 padding，需要逐行 copy --------
    out_rgba.resize(static_cast<std::size_t>(out_width) * out_height * 4U);

    for (std::uint32_t y = 0; y < out_height; ++y) {
        std::memcpy(out_rgba.data() + static_cast<std::size_t>(y) * tight,
                    src + static_cast<std::size_t>(y) * row_pitch, tight);
    }

    return true;
}

/**
 * @brief 解码公共逻辑（文件 / 内存共用）
 */
bool decode_common(ktxTexture *tex, std::vector<std::uint8_t> &out_rgba,
                   std::uint32_t &out_width, std::uint32_t &out_height,
                   std::string *err_out) {

    // 确保数据存在
    if (!ensure_ktx2_payload_in_memory(tex, err_out)) {
        ktxTexture_Destroy(tex);
        return false;
    }

    // 转换为 RGBA8
    if (!ensure_tex_rgba8_cpu(tex, err_out)) {
        ktxTexture_Destroy(tex);
        return false;
    }

    // 拷贝 mip0
    const bool ok = copy_level0_rgba8(tex, out_rgba, out_width, out_height);

    ktxTexture_Destroy(tex);
    return ok;
}

} // namespace

/**
 * @brief 从文件加载 KTX 并解码
 */
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

/**
 * @brief 从内存加载 KTX 并解码
 */
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
