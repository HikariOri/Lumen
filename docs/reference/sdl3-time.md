# SDL3 与时间（Lumen）

## 本仓库推荐

应用与示例中请优先使用引擎封装：

| 用途 | API |
|------|-----|
| 帧计时 / 运行时间（秒） | `lumen::core::get_time_seconds()` |

路径与用法见 [快速参考](../guides/quick-reference.md)。

## SDL3 原生 API 速查

以下为 SDL3 常见时间 API，便于与第三方示例对照；**不必在业务代码中混用多种时钟**，以免 delta 不一致。

### 毫秒时钟（自 `SDL_Init` 起）

```cpp
#include <SDL3/SDL.h>

Uint64 now_ms = SDL_GetTicks();
```

适合简单动画与粗粒度计时。

### 高精度性能计数器

```cpp
Uint64 start = SDL_GetPerformanceCounter();
// ...
Uint64 end = SDL_GetPerformanceCounter();
double elapsed_ms =
    (end - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
```

适合 profiler、细粒度区间计时。

### 纳秒级 ticks（SDL3）

```cpp
Uint64 ns = SDL_GetTicksNS();
```

### 线程休眠

```cpp
SDL_Delay(16);           // 毫秒
SDL_DelayNS(1'000'000);  // 纳秒（示例为 1ms）
```

## 参考

- [SDL3 CategoryTimer（官方 Wiki）](https://wiki.libsdl.org/CategoryTimer)
