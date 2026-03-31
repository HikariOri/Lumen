# ImGui Vulkan + SDL3 集成说明

> 引擎 ImGui 后端封装使用说明与注意事项。

正文排版见 [中文文案排版指北](../guides/chinese-typography.md)；示例代码风格见 [C++ 编码风格参考](../reference/cpp-style.md)（与 `lumen::` API 保持一致）。

## 1. 架构概览

```
engine/
├── include/ui/
│   ├── imgui_backend.hpp          # ImGui 后端封装
│   ├── imgui_layer.hpp            # 对应 Hazel ImGuiLayer：SDL、Overlay、WantCapture
│   ├── panel.hpp                  # IPanel、PanelManager
│   ├── log_panel.hpp              # 日志面板（LogViewBuffer）
│   ├── texture_view_panel.hpp     # 纹理预览面板（Scene/Wireframe 等）
│   ├── gizmo.hpp                  # ImGuizmo 视口操作轴封装
│   └── gpu_capabilities_panel.hpp # GPU 信息（函数 + GpuCapabilitiesPanel）
└── src/ui/
    ├── imgui_backend.cpp
    ├── imgui_layer.cpp
    ├── panel_manager.cpp
    ├── log_panel.cpp
    ├── texture_view_panel.cpp
    ├── gizmo.cpp
    └── gpu_capabilities_panel.cpp
```

可复用面板与 Dock 注册方式详见 [ui-panels.md](ui-panels.md)。日志写入 UI 缓冲见 [logging.md](../reference/logging.md)。

- **平台**：SDL3 窗口 + 输入
- **渲染**：Vulkan 后端
- **依赖**：vcpkg `imgui`（需启用 `vulkan-binding`、`sdl3-binding`）

## 2. 与 Hazel 的对应关系（事件 + 渲染）

[Hazel `Application`](https://github.com/TheCherno/Hazel) 中：

- **`OnEvent`**：先用 `EventDispatcher` 处理窗口关闭、resize 等，再自栈顶向栈底遍历各层 `OnEvent`（`Handled` 为 true 则停止）。
- **`Run`**：`Layer::OnUpdate` → **`ImGuiLayer::Begin`** → 各层 **`OnImGuiRender`** → **`ImGuiLayer::End`** → `Window::OnUpdate`（交换缓冲等）。

Lumen 中：

| Hazel | Lumen |
|--------|--------|
| `Application::OnEvent` 前缀 `Dispatch` | `EventPump::set_on_application_event` |
| `ImGuiLayer::OnEvent` + GLFW 转发 | `ImGuiLayer::attach`（SDL 转发 + `on_event` / `WantCapture*` → `handled`） |
| 其他层 `OnEvent` | `EventPump::push_layer` |
| `ImGuiLayer::Begin` | `ImGuiLayer::begin_frame()`（内部 `imgui_backend_new_frame`） |
| 各层 `OnImGuiRender` | 你在 `begin_frame` 与 `end_frame` 之间写的 `ImGui::Begin`/`End` |
| `ImGuiLayer::End` | `ImGuiLayer::end_frame(cmd)`（内部 `imgui_backend_render`） |

底层 Vulkan/SDL 细节仍由 **`imgui_backend`** 实现；**`ImGuiLayer`** 负责与 Hazel 一致的生命周期与事件挂接。

## 3. 基本用法

```cpp
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"
#include "platform/event_pump.hpp"

// 初始化（在 Swapchain、RenderPass、Framebuffer 创建之后）
lumen::ui::ImGuiBackendInitInfo info;
info.ctx = &ctx;
info.swapchain = &swapchain;
info.renderPass = renderPass.handle();
info.window = window.sdl_window();
if (!lumen::ui::imgui_backend_init(info)) {
    return -1;
}

lumen::platform::EventPump pump;
lumen::ui::ImGuiLayer imgui_layer;
imgui_layer.attach(pump);
// 可选：pump.set_on_application_event(...);  // 窗口关闭、resize，见 event-input.md

while (running && pump.poll()) {
    // 建议：先更新游戏状态（对应 Hazel OnUpdate），再 Begin
    imgui_layer.begin_frame();
    // … ImGui::Begin / ImGui::End …
    // 在 RenderPass 内、3D 绘制之后：
    imgui_layer.end_frame(cmd);
}

// 退出前
ctx.wait_idle();
lumen::ui::imgui_backend_shutdown();
```

### 3.1 界面中文（CJK）显示

ImGui 自带默认字体**不含**中文，直接 `Text(u8"日志")` 会出现缺字、方框或乱码。做法：

1. **初始化时指定含 CJK 的字体文件**（在 `imgui_backend_init` 之前写入 `ImGuiBackendInitInfo`）：
   - `cjk_font_ttf_path`：`.ttf` / `.otf` / `.ttc` 路径，字符串为 **UTF-8**（Windows 下可用正斜杠，如 `C:/Windows/Fonts/msyh.ttc` 微软雅黑）。
   - `cjk_font_size_pixels`：字号，默认 18；仅在与 `cjk_font_ttf_path` 同时使用时生效。
2. 引擎会使用 `GetGlyphRangesChineseSimplifiedCommon()` 预烘焙常用简体字形，图集比全量汉字小，一般足够做调试 UI。
3. **源码字符串**：请保存为 **UTF-8**（建议带 BOM 以便 MSVC 正确识别）；或使用 `u8"..."` 字面量并确保编译单元为 UTF-8。
4. 未设置 `cjk_font_ttf_path` 时仍使用 ImGui 内置字体，中文无法显示属预期行为。

### 3.2 Docking（全屏 Dock 宿主）

- `ImGuiBackendInitInfo::enable_docking`（默认 `true`）：启用 `ImGuiConfigFlags_DockingEnable`，并由 `ImGuiLayer::begin_frame()`（即 `imgui_backend_new_frame()`）在 `NewFrame` / `ImGuizmo::BeginFrame` 之后插入全屏 `##LumenDockHost` + `DockSpace("LumenMainDock")`（`PassthruCentralNode`）。
- 设为 `false` 时不设 Docking 标志，也不绘制宿主窗口。
- `imgui_backend_docking_enabled()`、`imgui_backend_main_dockspace_id()` 供应用或 `PanelManager::set_default_dock_id` 查询当前主 Dock ID（未启用或未建时为 `0`）。

## 4. 重要注意事项

### 4.1 SetMinImageCount 与 Swapchain 重建

**问题**：ImGui Vulkan 后端的 `ImGui_ImplVulkan_SetMinImageCount()` 在运行时**不支持**将 `MinImageCount` 从 2 改为 3（或反向）。

**原因**：

- Swapchain 图像数量由驱动决定，常见为 2（双缓冲）或 3（三缓冲）
- ImGui 在初始化时按 `ImageCount` 分配内部资源（每帧缓冲、描述符等）
- 变更 `MinImageCount` 需重新创建这些资源，当前实现中会触发 `IM_ASSERT(0)`，并注释 `FIXME-VIEWPORT: Unsupported. Need to recreate all swap chains!`

**做法**：

- **不要**在 Swapchain 重建后调用 `imgui_backend_set_min_image_count(swapchain.image_count())`
- 保持初始化时的 `MinImageCount=2`、`ImageCount` 不变
- 多数情况下（resize 后图像数量未变）ImGui 可正常工作

### 4.2 SDL 常量与 vcpkg 构建

部分 vcpkg 构建的 SDL3 会重定向 `SDL_TRUE`/`SDL_FALSE` 到不存在的符号，导致编译错误。

**做法**：优先使用 `lumen::platform::Window::set_relative_mouse_mode`（内部用 C++ `bool` 调 SDL）；若直接调 SDL，使用 `true` / `false`，不要用 `SDL_TRUE` / `SDL_FALSE`。

### 4.3 输入占用与 3D 控制

当 ImGui 窗口获得焦点时，应避免将鼠标/键盘事件用于 3D 控制（相机旋转、模型旋转、缩放等）。

**做法**：使用 `imgui_layer.hpp` 的 WantCapture 查询：

```cpp
#include "ui/imgui_layer.hpp"

if (!lumen::ui::imgui_wants_mouse()) {
    // 处理相机/模型旋转、滚轮缩放等
}
if (!lumen::ui::imgui_wants_keyboard()) {
    // 处理 WASD 等
}
// 或统一判断：
if (!lumen::ui::imgui_wants_any_input()) {
    // 游戏输入逻辑
}
```

详见 [../reference/event-input.md](../reference/event-input.md) 第 4 节。

### 4.4 RenderPass 兼容性

ImGui Vulkan 后端使用的 RenderPass 需与主渲染通道的 color attachment 格式兼容（通常为 swapchain 格式）。当前封装复用主通道的 `VkRenderPass`，一般无需额外配置。

### 4.5 相对鼠标模式

在拖拽 3D 时启用 `SDL_SetWindowRelativeMouseMode` 前，应先排除 ImGui 占用：

```cpp
pump.push_layer([&](lumen::platform::DispatchableEvent& de) {
    lumen::platform::EventDispatcher d(de);
    d.dispatch<lumen::platform::EventMouseButtonDown>(
        [&](lumen::platform::EventMouseButtonDown&) {
            if (lumen::ui::imgui_wants_mouse())
                return false;
            // 启用相对鼠标模式…
            return false;
        });
});
```

## 5. 3D 场景渲染到 ImGui 窗口

若需将 3D 渲染结果显示在 ImGui 的 Scene/Viewport 窗口中（可 docking）：

1. **离屏渲染**：创建离屏 framebuffer（颜色 + 深度），`finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
2. **注册纹理**：`imgui_backend_add_texture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)` 获取 `ImTextureID`
3. **渲染流程**：先渲染 3D 到离屏，再渲染到 swapchain（clear + ImGui）
4. **Scene 窗口**：使用 `imgui_texture_view_panel("Scene", sceneTextureId, &outW, &outH)`（见 [ui-panels.md](ui-panels.md)）；**不要**使用 `ImVec2(0,1), ImVec2(1,0)` 做 Y 翻转（见下方注意事项）
5. **Resize**：先 `imgui_backend_remove_texture(oldId)`，重建离屏资源后再 `imgui_backend_add_texture` 获取新 ID

### 5.1 ImGui::Image 与 Vulkan 纹理方向（避免上下颠倒）

**问题**：在 Vulkan 离屏 framebuffer 渲染 3D 后，用 `ImGui::Image` 显示时画面可能上下颠倒。

**原因**：

- Vulkan framebuffer 的 (0,0) 在左上角，Y 向下为正
- 若投影矩阵已做 `proj[1][1] *= -1` 适配 Vulkan NDC，则离屏渲染出的 3D 场景**已是正朝向**（顶部在纹理顶部）
- `ImVec2(0,1), ImVec2(1,0)` 会翻转纹理 V 坐标，相当于额外做了一次 Y 翻转，导致画面颠倒

**做法**：使用默认 UV，不翻转：

```cpp
ImGui::Image(sceneTextureId, size);  // uv0=(0,0), uv1=(1,1)，不翻转
```

若仍颠倒，再检查离屏 RenderPass、viewport 或投影矩阵的 Y 方向。

## 6. 视口 Gizmo（ImGuizmo）

离屏 Scene 上叠加平移 / 旋转 / 缩放轴时，使用 `ImGuizmo`，并由 `ImGuiLayer::begin_frame()` 在每帧调用 `ImGuizmo::BeginFrame()`。视口矩形、`Manipulate` 调用顺序及与 3D 渲染的时序见 [gizmos.md](gizmos.md)。

与相机 / 模型鼠标拖拽并存时，请结合 `imguizmo_is_using()`（或应用侧缓存的上一帧状态）与 `viewport_mouse_state`、`imgui_wants_mouse()` 做互斥。

## 7. 参考

- [../reference/event-input.md](../reference/event-input.md) — 事件与分层输入系统
- [ui-panels.md](ui-panels.md) — 纹理预览、GPU 信息等面板
- [gizmos.md](gizmos.md) — ImGuizmo 集成与 API
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImGui Vulkan 后端](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_vulkan.cpp)
- [GLM 与 Vulkan 适配](../reference/glm-vulkan.md)

