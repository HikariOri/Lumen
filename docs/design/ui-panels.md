# UI 面板组件

> 引擎提供的 ImGui 可复用面板，用于 Scene 视口、日志、GPU 信息等常见 UI。本阶段**不包含**控制台命令输入与 Command 系统（见 [editor-ui.md](editor-ui.md) 1.2a）。

## 1. 架构概览

```
engine/include/ui/
├── imgui_backend.hpp       # ImGui 后端封装（Vulkan + SDL3）
├── input_bridge.hpp        # SDL→ImGui 事件、WantCapture 查询（见 ../reference/event-input.md）
├── panel.hpp               # IPanel、PanelManager（集中绘制、可选默认 Dock）
├── editor_selection.hpp    # 当前选中实体（供 Hierarchy / Inspector / Gizmo 共享）
├── scene_hierarchy_panel.hpp
├── scene_inspector_panel.hpp
├── log_panel.hpp           # 日志面板（读 LogViewBuffer）
├── texture_view_panel.hpp  # 纹理预览面板（Scene/Wireframe/Normal/Depth）
└── gpu_capabilities_panel.hpp  # GPU 信息：自由函数 + GpuCapabilitiesPanel

engine/include/scene/       # EnTT 场景（与渲染解耦）
├── components.hpp          # ObjectId、Name、Transform、Light…
├── light.hpp               # GPULight、pack_lights_for_ubo
├── scene_orbit_camera.hpp  # 编辑器轨道相机（见 scene-camera.md）
├── scene_camera_controller.hpp
├── scene.hpp               # Scene 封装 registry、父子、环检测
└── transform.hpp           # world_matrix（层级链）

engine/src/ui/
├── imgui_backend.cpp
├── panel_manager.cpp
├── scene_hierarchy_panel.cpp
├── scene_inspector_panel.cpp
├── log_panel.cpp
├── texture_view_panel.cpp
└── gpu_capabilities_panel.cpp

engine/src/scene/
├── scene.cpp
├── transform.cpp
├── light.cpp
├── scene_orbit_camera.cpp
└── scene_camera_controller.cpp
```

* **自由函数面板**（如 `imgui_texture_view_panel`）：由调用方在帧内直接调用。
* **PanelManager**：统一对实现了 `IPanel` 的面板调用 `on_imgui_render()`，适合日志、GPU 信息、**Hierarchy / Inspector**（数据源为 `lumen::scene::Scene` + `EditorSelection`）等可注册窗口。
* **材质 / 环境（IBL）**：规划中的参数与贴图编辑、环境立方体贴图加载见 [material-system-ibl-pbr.md](material-system-ibl-pbr.md)（可落在 Inspector 折叠块或独立 `IPanel`）。

所有 ImGui 绘制**需在 ImGui 帧内**完成（`imgui_backend_new_frame()` 之后、`imgui_backend_render(cmd)` 之前）。

### 1.1 PanelManager 与 Dock

典型顺序：先 `DockSpaceOverViewport` 得到 `dockspaceId`，再 `panel_manager.set_default_dock_id(dockspaceId)`，然后绘制纹理视口等自定义窗口，最后 `panel_manager.render_all()`。`render_all()` 会在每个面板前设置 `SetNextWindowDockID`（`FirstUseEver`），便于首次布局进同一 Dock 空间。

```cpp
#include "ui/panel.hpp"
#include "ui/log_panel.hpp"
#include "ui/gpu_capabilities_panel.hpp"

lumen::ui::PanelManager panels;
panels.add(std::make_unique<lumen::ui::LogPanel>());
panels.add(std::make_unique<lumen::ui::GpuCapabilitiesPanel>(ctx));

// 每帧 UI 通道内：
ImGuiID dockId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
panels.set_default_dock_id(dockId);
// ... 其他 ImGui 窗口 ...
panels.render_all();
```

### 1.2 日志面板（LogPanel）

日志文本来自 `lumen::core::LogViewBuffer`，由 spdlog 的 UI sink 在 `Logger::init` 时挂载（见 [logging.md](../reference/logging.md)）。`LogPanel` 提供最低级别过滤、自动滚底、清空缓冲等，**不包含**命令行输入。

---

## 2. 纹理预览面板 (texture_view_panel)

用于在 ImGui 窗口中显示离屏渲染纹理，支持输出显示尺寸供调用方调整离屏分辨率（1:1 像素匹配，节省显存）。

### 头文件

```cpp
#include "ui/texture_view_panel.hpp"
```

### API

```cpp
struct TextureViewRect {
    float minX, minY;   // 左上角屏幕坐标
    float maxX, maxY;   // 右下角屏幕坐标
    float width();      // maxX - minX
    float height();     // maxY - minY
};

void imgui_texture_view_panel(
    const char *title, ImTextureID textureId,
    uint32_t *outWidth = nullptr, uint32_t *outHeight = nullptr,
    TextureViewRect *outRect = nullptr, const ImVec2 &uv0 = ImVec2(0, 0),
    const ImVec2 &uv1 = ImVec2(1, 1),
    const std::function<void(const TextureViewRect &)> &after_image = {});
```

可选参数 `after_image(rect)`：在 `Image` 之后、`End` 之前调用，用于 ImGuizmo 等叠加绘制，见 [gizmos.md](gizmos.md)。

| 参数 | 说明 |
|------|------|
| `title` | 窗口标题，如 `"Scene"`, `"Wireframe"`, `"Normal"`, `"Depth"` |
| `textureId` | `ImTextureID`，来自 `imgui_backend_add_texture()` |
| `outWidth` | 输出本帧显示宽度，供下一帧 resize 离屏目标；`nullptr` 则不输出。引擎内已处理 ImGui 可用区域为负/非有限等情况，**应用侧不必再钳位** |
| `outHeight` | 输出本帧显示高度；`nullptr` 则不输出（同上，由引擎保证可安全用于 `resize`） |
| `outRect` | 输出 Image 屏幕坐标矩形（左上 min、右下 max），用于射线拾取等；`nullptr` 则不输出 |
| `uv0`, `uv1` | 纹理 UV 范围，默认 `(0,0)-(1,1)`，Vulkan 离屏通常不需 Y 翻转 |
| `after_image` | 可选；在 `Image` 之后调用，参数为与 `outRect` 相同的 `TextureViewRect`（见 [gizmos.md](gizmos.md)） |

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

详见 [ImGui 3D 场景渲染到窗口](imgui-integration.md#4-3d-场景渲染到-imgui-窗口)。

### 视口鼠标状态（ViewportMouseState）

用于射线拾取、坐标转换等，提供鼠标在视口内的局部坐标与归一化坐标。

```cpp
struct ViewportMouseState {
    bool inViewport;   // 鼠标是否在视口内
    float localX;      // 局部 X，以视口左上角为 0
    float localY;      // 局部 Y
    float normX;       // 归一化 [0,1]，用于射线拾取
    float normY;       // 归一化 [0,1]
};

// 计算
ViewportMouseState state = lumen::ui::viewport_mouse_state(
    sceneRect, pump.input().mouse_x(), pump.input().mouse_y());

if (state.inViewport) {
    // 射线拾取：state.normX, state.normY 转 NDC
    // 或使用 state.localX, state.localY 像素坐标
}
```

### 视口调试显示

```cpp
// 需在 ImGui::Begin 之后
const auto state = lumen::ui::viewport_mouse_state(
    sceneRect, pump.input().mouse_x(), pump.input().mouse_y());
lumen::ui::imgui_viewport_mouse_debug(sceneRect, state, "Scene");
```

### 仅显示纹理（不关心尺寸）

```cpp
lumen::ui::imgui_texture_view_panel("Preview", textureId);
// outWidth/outHeight/outRect 传 nullptr 即可
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
                                  const char *title = "GPU Capabilities",
                                  bool *p_open = nullptr);

class GpuCapabilitiesPanel final : public IPanel {
public:
    explicit GpuCapabilitiesPanel(const render::Context &ctx);
    void on_imgui_render() override;
};
```

| 参数 | 说明 |
|------|------|
| `ctx` | 已初始化 Device 的 `Context` |
| `title` | 窗口标题，默认 `"GPU Capabilities"` |
| `p_open` | 非空时显示关闭按钮；为 `false` 时不再展开内容 |

### 显示内容

- 设备名称、类型、Vendor ID、Device ID
- API 版本、驱动版本
- VRAM 大小
- `maxImageDimension2D`、`maxUniformBufferRange`、`maxStorageBufferRange`、`maxPushConstantsSize`

### 用法示例

与 `PanelManager` 一起使用时，一般由 `GpuCapabilitiesPanel` 负责首次窗口大小；仅需 `set_default_dock_id` + `render_all()`。

仍可直接调用自由函数：

```cpp
ImGui::SetNextWindowDockID(dockspaceId, ImGuiCond_FirstUseEver);
ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
lumen::ui::imgui_gpu_capabilities_panel(ctx);
```

---

## 4. 与 ImGui 后端的配合

1. **初始化**：`imgui_backend_init(ImGuiBackendInitInfo)`
2. **每帧**：`imgui_backend_new_frame()` → 绘制各面板 → `imgui_backend_render(cmd)`（在 RenderPass 内）
3. **纹理注册**：离屏目标创建/重建后，`imgui_backend_add_texture()` 获取 `ImTextureID`；resize 前需 `imgui_backend_remove_texture(oldId)`

详见 [ImGui Vulkan + SDL3 集成说明](imgui-integration.md)。

---

## 5. 参考

- [imgui-integration.md](imgui-integration.md) — 后端与 3D 渲染到 ImGui
- [Dear ImGui](https://github.com/ocornut/imgui)

