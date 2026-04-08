#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

// STB / VMA 实现见 `stb_impl.cpp`、`vma_impl.cpp`（单编译单元），勿在 pch 中定义
// STB_IMAGE_IMPLEMENTATION / VMA_IMPLEMENTATION，否则 MSVC 下易与业务 .cpp 重复符号。
