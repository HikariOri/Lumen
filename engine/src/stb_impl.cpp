/**
 * @file stb_impl.cpp
 * @brief 单独编译单元承载 `stb_image` 实现，避免与预编译头或其它 TU 重复定义。
 */

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
