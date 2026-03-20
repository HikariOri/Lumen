# 开发注意事项

> 使用 Lumen 引擎时需要注意的事项与常见坑点。

## 一、初始化顺序

### 1.1 路径工具 `core::get_base_path()` / `get_resource_path()`

- **依赖**：必须等 `SDL_Init` 执行后再调用
- **建议**：在 `Window::create()` 之后使用（Window 内部会调用 SDL_Init）
- **失败时**：`get_base_path()` 返回空字符串；`get_resource_path()` 会退化为仅返回 `subpath`

### 1.2 时间工具 `core::get_time_seconds()`

- **无依赖**：使用 `std::chrono::steady_clock`，不依赖 SDL
- 可在程序任意位置调用

---

## 二、日志

### 2.1 引擎日志 vs 应用日志

| 宏 | 输出到 | 典型用途 |
| --- | --- | --- |
| `LUMEN_LOG_*` | engine logger | 引擎内部（render、platform 等） |
| `LUMEN_APP_LOG_*` | app logger | 应用层（main、game logic 等） |

- **应用层代码**应使用 `LUMEN_APP_LOG_*`，便于区分来源
- 两套 logger 的级别可分别配置：`logConfig.engine.level`、`logConfig.app.level`

---

## 三、Vulkan 渲染循环

### 3.1 Fence 等待

- **只等待 `currentFrame` 的 fence**，不要等待 `prevFrame`
- 否则会误重置从未被 `vkQueueSubmit` 使用过的 fence，导致下一帧永久阻塞

### 3.2 Acquire Semaphore

- 使用 **`image_available(currentFrame)`**，不要固定用 `image_available(0)`
- 多帧并发时，若复用同一个 semaphore，会触发 `VUID-vkAcquireNextImageKHR-semaphore-01779`（semaphore 仍有未完成操作）

### 3.3 Swapchain 重建

- **窗口 resize**：收到 `EventWindowResize` 后设置 `needRecreateSwapchain = true`
- **Present 返回 `VK_ERROR_OUT_OF_DATE_KHR`**：同样需要重建
- 重建前需 `vkDeviceWaitIdle`，然后依次：`swapchain.resize` → 重建 framebuffers → 重建 FrameSync

### 3.4 Per-frame UBO

- 有多帧并发（`framesInFlight > 1`）时，应为每帧使用独立的 UBO 和 DescriptorSet
- 否则前一帧 GPU 仍在读时，当前帧会覆盖同一 buffer，造成闪烁或错误

---

## 四、事件系统

### 4.1 `std::variant` + `emplace_back`

- `EventList` 为 `std::vector<std::variant<...>>`
- 需用 `emplace_back(std::in_place_type<EventType>, args...)` 就地构造
- 不要用 `push_back(EventType { ... })`，避免多余临时对象

---

## 五、Pipeline 配置

### 5.1 聚合类型与 `emplace_back`

- `ShaderStage`、`VertexInputBinding`、`VertexInputAttribute` 为聚合类型，无显式构造函数
- 使用 `push_back({ a, b, c })`，不要用 `emplace_back(a, b, c)`（会匹配 `initializer_list` 导致编译错误）

---

## 六、资源销毁顺序

- **Swapchain** 需在 **Surface** 之前销毁
- 其他 Vulkan 资源：先销毁依赖方（如 Framebuffer），再销毁被依赖方（如 Swapchain）

---

## 七、Vulkan NDC 坐标

### 7.1 与 OpenGL 的区别

Vulkan 的 **NDC（Normalized Device Coordinates）** 与 OpenGL 不同：

| 轴 | Vulkan | OpenGL |
| --- | --- | --- |
| X | 右为正 | 右为正 |
| Y | **下为正** | 上为正 |
| Z | 近为 0，远为 1（右手系） | 近为 -1，远为 1 |

### 7.2 常见注意点

- **Y 轴向下**：屏幕坐标系中“向上”对应 NDC 中 **Y 减小**
- 做平移、方向控制时：
  - 希望物体向上移 → `position.y -= delta`
  - 希望物体向下移 → `position.y += delta`
- 若从 OpenGL 迁移，需对 Y 分量取反或调整变换矩阵
