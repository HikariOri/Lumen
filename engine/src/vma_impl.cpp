/**
 * @file vma_impl.cpp
 * @brief 单独编译单元承载 VMA 实现，避免与预编译头或其它 TU 重复定义。
 */

#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
