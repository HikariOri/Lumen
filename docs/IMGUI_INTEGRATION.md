# ImGui Vulkan + SDL3 集成说明

> 引擎 ImGui 后端封装使用说明与注意事项。

## 1. 架构概览

```
engine/
├── include/ui/
│   ├── imgui_backend.hpp         # ImGui 后端封装
│   ├── input_bridge.hpp          # SDL→ImGui 事件、WantCapture 查询
│   ├── texture_view_panel.hpp    # 纹理预览面板（Scene/Wireframe 等）
│   └── gpu_capabilities_panel.hpp# GPU 信息面板
└── src/ui/
    ├── imgui_backend.cpp
    ├── texture_view_panel.cpp
    └── gpu_capabilities_panel.cpp
```

可复用面板详见 [UI_PANELS.md](UI_PANELS.md)。

- **平台**：SDL3 窗口 + 输入
- **渲染**：Vulkan 后端
- **依赖**：vcpkg `imgui`（需启用 `vulkan-binding`、`sdl3-binding`）

## 2. 基本用法

```cpp
#include "ui/imgui_backend.hpp"

// 初始化（在 Swapchain、RenderPass、Framebuffer 创建之后）
lumen::ui::ImGuiBackendInitInfo info;
info.ctx = &ctx;
info.swapchain = &swapchain;
info.renderPass = renderPass.handle();
info.window = window.sdl_window();
if (!lumen::ui::imgui_backend_init(info))
    return -1;

// 转发 SDL 事件给 ImGui（imgui_backend_init 之后）
#include "ui/input_bridge.hpp"
lumen::ui::imgui_setup_event_pump(pump);

// 每帧
lumen::ui::imgui_backend_new_frame();
// ... ImGui::Begin / ImGui::End 等 ...
// 在 RenderPass 内、3D 绘制之后：
lumen::ui::imgui_backend_render(cmd);

// 退出前
ctx.wait_idle();
lumen::ui::imgui_backend_shutdown();
```

## 3. 重要注意事项

### 3.1 SetMinImageCount 与 Swapchain 重建

**问题**：ImGui Vulkan 后端的 `ImGui_ImplVulkan_SetMinImageCount()` 在运行时**不支持**将 `MinImageCount` 从 2 改为 3（或反向）。

**原因**：

- Swapchain 图像数量由驱动决定，常见为 2（双缓冲）或 3（三缓冲）
- ImGui 在初始化时按 `ImageCount` 分配内部资源（每帧缓冲、描述符等）
- 变更 `MinImageCount` 需重新创建这些资源，当前实现中会触发 `IM_ASSERT(0)`，并注释 `FIXME-VIEWPORT: Unsupported. Need to recreate all swap chains!`

**做法**：

- **不要**在 Swapchain 重建后调用 `imgui_backend_set_min_image_count(swapchain.image_count())`
- 保持初始化时的 `MinImageCount=2`、`ImageCount` 不变
- 多数情况下（resize 后图像数量未变）ImGui 可正常工作

### 3.2 SDL 常量与 vcpkg 构建

部分 vcpkg 构建的 SDL3 会重定向 `SDL_TRUE`/`SDL_FALSE` 到不存在的符号，导致编译错误。

**做法**：直接使用 C++ 的 `true` / `false`：

```cpp
SDL_SetWindowRelativeMouseMode(window, true);   // 不要用 SDL_TRUE
SDL_SetWindowRelativeMouseMode(window, false);  // 不要用 SDL_FALSE
```

### 3.3 输入占用与 3D 控制

当 ImGui 窗口获得焦点时，应避免将鼠标/键盘事件用于 3D 控制（相机旋转、模型旋转、缩放等）。

**做法**：使用 `input_bridge.hpp` 的 WantCapture 查询：

```cpp
#include "ui/input_bridge.hpp"

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

详见 [EVENT_SYSTEM.md](EVENT_SYSTEM.md) 第 4 节。

### 3.4 RenderPass 兼容性

ImGui Vulkan 后端使用的 RenderPass 需与主渲染通道的 color attachment 格式兼容（通常为 swapchain 格式）。当前封装复用主通道的 `VkRenderPass`，一般无需额外配置。

### 3.5 相对鼠标模式

在拖拽 3D 时启用 `SDL_SetWindowRelativeMouseMode` 前，应先排除 ImGui 占用：

```cpp
pump.on_mouse_button_down([&](const EventMouseButtonDown& e) {
    if (lumen::ui::imgui_wants_mouse())
        return;
    // 启用相对鼠标模式...
});
```

## 4. 3D 场景渲染到 ImGui 窗口

若需将 3D 渲染结果显示在 ImGui 的 Scene/Viewport 窗口中（可 docking）：

1. **离屏渲染**：创建离屏 framebuffer（颜色 + 深度），`finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
2. **注册纹理**：`imgui_backend_add_texture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)` 获取 `ImTextureID`
3. **渲染流程**：先渲染 3D 到离屏，再渲染到 swapchain（clear + ImGui）
4. **Scene 窗口**：使用 `imgui_texture_view_panel("Scene", sceneTextureId, &outW, &outH)`（见 [UI_PANELS.md](UI_PANELS.md)）；**不要**使用 `ImVec2(0,1), ImVec2(1,0)` 做 Y 翻转（见下方注意事项）
5. **Resize**：先 `imgui_backend_remove_texture(oldId)`，重建离屏资源后再 `imgui_backend_add_texture` 获取新 ID

### 4.1 ImGui::Image 与 Vulkan 纹理方向（避免上下颠倒）

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

## 5. 参考

- [EVENT_SYSTEM.md](EVENT_SYSTEM.md) — 事件与分层输入系统
- [UI_PANELS.md](UI_PANELS.md) — 纹理预览、GPU 信息等面板
- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImGui Vulkan 后端](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_vulkan.cpp)
- [GLM 与 Vulkan 适配](GLM_VULKAN.md)
