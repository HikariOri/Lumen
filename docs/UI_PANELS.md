# UI 面板组件

> 引擎提供的 ImGui 可复用面板，用于 Scene 视口、GPU 信息等常见 UI。

## 1. 架构概览

```
engine/include/ui/
├── imgui_backend.hpp      # ImGui 后端封装（Vulkan + SDL3）
├── texture_view_panel.hpp # 纹理预览面板（Scene/Wireframe/Normal/Depth）
└── gpu_capabilities_panel.hpp  # GPU 信息面板

engine/src/ui/
├── imgui_backend.cpp
├── texture_view_panel.cpp
└── gpu_capabilities_panel.cpp
```

所有面板**需在 ImGui 帧内调用**（`imgui_backend_new_frame()` 之后、`imgui_backend_render(cmd)` 之前）。

---

## 2. 纹理预览面板 (texture_view_panel)

用于在 ImGui 窗口中显示离屏渲染纹理，支持输出显示尺寸供调用方调整离屏分辨率（1:1 像素匹配，节省显存）。

### 头文件

```cpp
#include "ui/texture_view_panel.hpp"
```

### API

```cpp
void imgui_texture_view_panel(const char *title, ImTextureID textureId,
                              uint32_t *outWidth = nullptr,
                              uint32_t *outHeight = nullptr,
                              const ImVec2 &uv0 = ImVec2(0, 0),
                              const ImVec2 &uv1 = ImVec2(1, 1));
```

| 参数 | 说明 |
|------|------|
| `title` | 窗口标题，如 `"Scene"`, `"Wireframe"`, `"Normal"`, `"Depth"` |
| `textureId` | `ImTextureID`，来自 `imgui_backend_add_texture()` |
| `outWidth` | 输出本帧显示宽度，供下一帧 resize 离屏目标；`nullptr` 则不输出 |
| `outHeight` | 输出本帧显示高度；`nullptr` 则不输出 |
| `uv0`, `uv1` | 纹理 UV 范围，默认 `(0,0)-(1,1)`，Vulkan 离屏通常不需 Y 翻转 |

### 典型用法：Scene / 多视口

```cpp
uint32_t nextSceneW { 0 }, nextSceneH { 0 };
// ...

ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
lumen::ui::imgui_texture_view_panel("Scene", sceneTextureId,
                                    &nextSceneW, &nextSceneH);

// 另一视口（如 Wireframe）
ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
lumen::ui::imgui_texture_view_panel("Wireframe", wireframeTextureId,
                                    &nextWireframeW, &nextWireframeH);
```

### 离屏 resize 流程

1. **本帧**：`imgui_texture_view_panel` 输出 `nextSceneW` / `nextSceneH`
2. **帧间**：检查 `sceneTarget.extent()` 与 `nextSceneW/nextSceneH` 是否一致
3. **若需 resize**：`ctx.wait_idle()` → `sceneTarget.resize(ctx, nextSceneW, nextSceneH)` → `imgui_backend_remove_texture(oldId)` → `imgui_backend_add_texture(...)` 获取新 ID

详见 [ImGui 3D 场景渲染到窗口](IMGUI_INTEGRATION.md#4-3d-场景渲染到-imgui-窗口)。

### 仅显示纹理（不关心尺寸）

```cpp
lumen::ui::imgui_texture_view_panel("Preview", textureId);
// outWidth/outHeight 传 nullptr 即可
```

---

## 3. GPU Capabilities 面板

显示物理设备信息与 Vulkan 限制，用于调试和工具界面。

### 头文件

```cpp
#include "ui/gpu_capabilities_panel.hpp"
```

### API

```cpp
void imgui_gpu_capabilities_panel(const render::Context &ctx,
                                  const char *title = "GPU Capabilities");
```

| 参数 | 说明 |
|------|------|
| `ctx` | 已初始化 Device 的 `Context` |
| `title` | 窗口标题，默认 `"GPU Capabilities"` |

### 显示内容

- 设备名称、类型、Vendor ID、Device ID
- API 版本、驱动版本
- VRAM 大小
- `maxImageDimension2D`、`maxUniformBufferRange`、`maxStorageBufferRange`、`maxPushConstantsSize`

### 用法示例

```cpp
ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_FirstUseEver);
ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
lumen::ui::imgui_gpu_capabilities_panel(ctx);
```

---

## 4. 与 ImGui 后端的配合

1. **初始化**：`imgui_backend_init(ImGuiBackendInitInfo)`
2. **每帧**：`imgui_backend_new_frame()` → 绘制各面板 → `imgui_backend_render(cmd)`（在 RenderPass 内）
3. **纹理注册**：离屏目标创建/重建后，`imgui_backend_add_texture()` 获取 `ImTextureID`；resize 前需 `imgui_backend_remove_texture(oldId)`

详见 [ImGui Vulkan + SDL3 集成说明](IMGUI_INTEGRATION.md)。

---

## 5. 参考

- [IMGUI_INTEGRATION.md](IMGUI_INTEGRATION.md) — 后端与 3D 渲染到 ImGui
- [Dear ImGui](https://github.com/ocornut/imgui)
