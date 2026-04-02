/**
 * @file asset/decoded_image.cpp
 */

#include "asset/decoded_image.hpp"

#include "core/path.hpp"

#include <stb_image.h>

#include <string>

namespace lumen::asset {

bool decode_image_file_to_rgba8(const char *file_path, DecodedImage &out,
                                  std::string *error_out) {
    out = DecodedImage {};
    if (file_path == nullptr || file_path[0] == '\0') {
        if (error_out != nullptr) {
            *error_out = "empty path";
        }
        return false;
    }
    stbi_set_flip_vertically_on_load(1);
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc *pixels =
        stbi_load(file_path, &w, &h, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        if (error_out != nullptr) {
            *error_out =
                stbi_failure_reason() ? stbi_failure_reason() : "stbi_load failed";
        }
        return false;
    }
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    const std::size_t nbytes =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.rgba.assign(pixels, pixels + nbytes);
    stbi_image_free(pixels);
    return true;
}

bool decode_image_resource_rel_path_to_rgba8(const std::string_view resource_rel_path,
                                              DecodedImage &out,
                                              std::string *error_out) {
    const std::string full =
        lumen::core::get_resource_path(std::string { resource_rel_path });
    return decode_image_file_to_rgba8(full.c_str(), out, error_out);
}

} // namespace lumen::asset
