/**
 * @file path.cpp
 * @brief 路径工具实现
 */

#include "core/path.hpp"

#include <SDL3/SDL.h>

namespace lumen {
namespace core {

std::string get_base_path() {
    static std::string cached;
    if (!cached.empty()) {
        return cached;
    }
    const char* base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    cached = base;
    // SDL3 遵循 GetStringRule，返回值由 SDL 管理，切勿 SDL_free
    return cached;
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
