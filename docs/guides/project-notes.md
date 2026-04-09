# 开发注意事项

> 使用 Lumen 引擎时需要注意的事项与常见坑点。

## 一、初始化顺序

### 1.1 路径工具 `core::get_base_path()` / `get_resource_path()`

- **依赖**：必须等 `SDL_Init` 执行后再调用
- **建议**：在 `Window::create(config)` 成功返回之后使用（`Window` 内部会调用 `SDL_Init`）
- **失败时**：`get_base_path()` 返回空字符串；`get_resource_path()` 会退化为仅返回 `subpath`

### 1.2 时间工具（`core/time.hpp`）

- **实现**：`std::chrono::steady_clock`，不依赖 SDL；详见 [reference/time.md](../reference/time.md)。
- **推荐**：进入主循环前调用 `anchor_steady_epoch()`，帧内用 `FrameDeltaClock::tick_seconds()` 取 dt；总时间用 `steady_seconds()`。

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

### 3.5 GPU 内存（VMA）

- **依赖**：`Buffer` / `Image` / `Texture` 创建需要 **`Context::init_device` 已成功**，此时 `vma_allocator()` 非空；仅 `init_instance` 时无法分配这些资源。
- **实现**：使用 [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)（vcpkg：`vulkan-memory-allocator`）；实现编译单元为 `engine/src/render/vma.cpp`（`VMA_IMPLEMENTATION`）。
- **生命周期**：`Context` 析构时先 **`vmaDestroyAllocator`**，再销毁 `VkDevice`，顺序勿颠倒。
- **范围**：Swapchain 图像等仍由 Vulkan swapchain 分配，不经 VMA。

---

## 四、Shadertoy 项目

Shadertoy 已提取为独立可执行项目 `shadertoy/`：

- **运行**：构建后执行 `shadertoy` 可执行文件
- **着色器**：`shadertoy/shaders/shadertoy.vert`、`shadertoy.frag`
- **Uniform**：`ShadertoyUBO` 含 `iResolution`、`iTime`、`iTimeDelta`、`iFrame`、`iMouse`
- **复用代码**：将 Shadertoy 的 `mainImage` 实现粘贴到 `shadertoy.frag` 中

---

## 五、事件系统

### 5.1 `std::variant` + `emplace_back`

- `EventList` 为 `std::vector<std::variant<...>>`
- 需用 `emplace_back(std::in_place_type<EventType>, args...)` 就地构造
- 不要用 `push_back(EventType { ... })`，避免多余临时对象

---

## 六、Pipeline 配置

### 6.1 聚合类型与 `emplace_back`

- `ShaderStage`、`VertexInputBinding`、`VertexInputAttribute` 为聚合类型，无显式构造函数
- 使用 `push_back({ a, b, c })`，不要用 `emplace_back(a, b, c)`（会匹配 `initializer_list` 导致编译错误）

---

## 七、资源销毁顺序

- **Swapchain** 需在 **Surface** 之前销毁
- 其他 Vulkan 资源：先销毁依赖方（如 Framebuffer），再销毁被依赖方（如 Swapchain）

---

## 八、Vulkan NDC 坐标

### 8.1 与 OpenGL 的区别

Vulkan 的 **NDC（Normalized Device Coordinates）** 与 OpenGL 不同：

| 轴 | Vulkan | OpenGL |
| --- | --- | --- |
| X | 右为正 | 右为正 |
| Y | **下为正** | 上为正 |
| Z | 近为 0，远为 1（右手系） | 近为 -1，远为 1 |

### 8.2 常见注意点

- **Y 轴向下**：屏幕坐标系中“向上”对应 NDC 中 **Y 减小**
- 做平移、方向控制时：
  - 希望物体向上移 → `position.y -= delta`
  - 希望物体向下移 → `position.y += delta`
- 若从 OpenGL 迁移，需对 Y 分量取反或调整变换矩阵
