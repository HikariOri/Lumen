# 时间与帧间隔（`lumen::core`）

引擎时间基于 **`std::chrono::steady_clock`** 的单调时钟，**不依赖 SDL**，可在未创建窗口前调用（与 [项目注意事项](../guides/project-notes.md) 中路径 API 不同）。

## API

| 名称 | 作用 |
|------|------|
| `anchor_steady_epoch()` | 将全局时间**零点**锚定到当前时刻；若零点已存在则**不修改**。建议在**进入主循环前**调用，避免把漫长初始化算进 `steady_seconds()`。 |
| `steady_seconds()` | 自零点起的秒数（`double`，单调递增）。 |
| `FrameDeltaClock` | 构造时采样当前 `steady_seconds()`；每帧 `tick_seconds()` 返回与上一 tick 的间隔（秒）。`last_steady_seconds()` 为上一次 tick 边界的绝对时间。 |

零点在**首次**调用 `anchor_steady_epoch()` 或 `steady_seconds()` 时建立，之后全进程共用同一零点。

## 典型用法

```cpp
lumen::core::anchor_steady_epoch();
lumen::core::FrameDeltaClock frame_dt;
while (running) {
    const float dt = static_cast<float>(frame_dt.tick_seconds());
    // 物理、动画、相机等使用 dt
}
```

需要「从开始到现在的总时间」时用 `steady_seconds()` 或 `frame_dt.last_steady_seconds()`（在 `tick_seconds()` 之后）。

## 线程与语义

- 当前实现假定在**主线程**使用（与渲染循环一致）；多线程同时**首次**建立零点存在数据竞争，应避免。
- 单调时钟**不受系统时间回拨**影响，适合物理与动画 dt；若需挂钟时间请另用 `system_clock` 等。

## 与 SDL3 的关系

SDL 的 `SDL_GetTicks`、`SDL_GetPerformanceCounter` 等与引擎时钟**独立**；对齐说明见 [SDL3 时间与引擎对照](sdl3-time.md)。
