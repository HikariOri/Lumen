# SDL3 时间与引擎对照

引擎默认使用 **`lumen::core::steady_seconds()` / `FrameDeltaClock`**（`std::chrono::steady_clock`），与 SDL 计时**互不依赖**。

若需与 SDL 教程或第三方代码对照，可使用 SDL3 自带 API：

| 用途 | SDL3 |
|------|------|
| 毫秒（自 `SDL_Init` 起） | `SDL_GetTicks()` |
| 纳秒 | `SDL_GetTicksNS()` |
| 高精度区间 | `SDL_GetPerformanceCounter()` + `SDL_GetPerformanceFrequency()` |

业务与渲染循环建议仍以 [引擎时间 API](time.md) 为准，避免混用多种时钟导致 dt 不一致。

## 参考

- [SDL3 CategoryTimer（官方 Wiki）](https://wiki.libsdl.org/CategoryTimer)
