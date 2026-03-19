/**
 * @file path.cpp
 * @brief 路径工具实现
 */

#include "core/path.hpp"

#include <SDL3/SDL.h>

namespace lumen {
namespace core {

std::string get_base_path() {
    char* base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    std::string result { base };
    SDL_free(base);
    return result;
}

std::string get_resource_path(std::string_view subpath) {
    std::string base = get_base_path();
    if (base.empty()) {
        return std::string { subpath };
    }
    base += subpath;
    return base;
}

} // namespace core
} // namespace lumen
