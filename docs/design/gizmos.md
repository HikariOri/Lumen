# 场景 Gizmo（ImGui + ImGuizmo）

> 在编辑器视口中对选中物体做平移 / 旋转 / 缩放的交互轴。正文排版见 [中文文案排版指北](../guides/chinese-typography.md)。

## 1. 目标与边界

- **目标**：在离屏 Scene 纹理贴到 ImGui 窗口后，在同一窗口内叠加绘制操作轴，并写回物体的 **世界变换矩阵**（`glm::mat4`，列主序，与 GLM / 本示例着色器一致）。
- **依赖**：vcpkg `imguizmo`（仓库已接入 `imguizmo::imguizmo`）、现有 `imgui` 后端与 [纹理视口](ui-panels.md) 的屏幕矩形 `TextureViewRect`。
- **本阶段不包含**：物体拾取（射线 / ID Buffer）、多选、骨骼 / 父节点层级、Undo/Redo；选中状态由应用自行维护（Demo3D 中恒为「场景中的单个模型」）。

## 2. 帧序与 ImGuizmo 约定

1. **`ImGuizmo::BeginFrame()`**  
   在 `ImGui::NewFrame()` 之后调用一次。已由 `lumen::ui::ImGuiLayer::begin_frame()`（内部 `imgui_backend_new_frame`）统一调用，应用无需重复。

2. **`SetRect` + `Manipulate`**  
   Gizmo 的命中与绘制基于 **屏幕像素矩形**，须与 Scene `ImGui::Image` 所占区域一致，使用上一帧或本帧 `imgui_texture_view_panel` 写出的 `TextureViewRect`（`minX/minY` 为左上、`maxX/maxY` 为右下）。

3. **绘制列表**  
   `Manipulate` 应在使用 `ImGui::Begin("Scene")` 且已绘制 `Image` 的同一窗口内调用（或由 `imgui_texture_view_panel` 的 `after_image` 回调注入），以便 `GetWindowDrawList()` 与裁剪区域正确。

4. **与 3D 渲染的时序**  
   当前 Demo3D 在 **Scene 离屏 Pass 之后** 的 UI Pass 中执行 `Manipulate`，本帧写入的矩阵在 **下一帧** 写入 UBO 并参与离屏渲染；与「纹理本身滞后一帧」一致，一般可接受。若需同帧对齐，需将视口逻辑前移到离屏 Pass 之前（单独迭代）。

## 3. 引擎 API（`lumen::ui`）

| 符号 | 说明 |
|------|------|
| `imguizmo_manipulate(...)` | 对 `glm::mat4 *object_world` 做 `ImGuizmo::Manipulate`；内部 `SetDrawlist`（注意大小写）/ `SetRect` |
| `imguizmo_is_using()` | 上一帧 `Manipulate` 调用后，用户是否正在拖拽 Gizmo（映射 `ImGuizmo::IsUsing()`） |
| `imguizmo_is_over()` | 鼠标是否悬停在 Gizmo 上（映射 `ImGuizmo::IsOver()`） |
| `imguizmo_reset_interaction_state()` | 本帧不调用 `Manipulate` 时清除 IsUsing/IsOver 缓存（如 Unity 式 Q 仅视图） |
| `imguizmo_view_manipulate(...)` | `ImGuizmo::ViewManipulate`：右上角方向立方体，快速切主轴视角；就地改 `view` |

**缩放下限**：每次 `Manipulate` 之后用 `glm::decompose` 读取各轴缩放，将绝对值钳制到不小于 `1e-2`，避免缩放过小导致矩阵奇异、缩放手柄无法再次拾取从而「缩到最小后放不大」。

**输入互斥**：相机旋转、模型拖拽等应在 `!imguizmo_is_using()`（或对上一帧状态的缓存）时处理，避免与 Gizmo 抢鼠标。与 ImGui 的 `WantCapture` 组合方式见 [imgui-integration.md](imgui-integration.md) 第 4.3 节与第 6 节。Demo3D 中与 Unity Scene 一致用 **Q/W/E/R** 切换视图（无 Gizmo）/移动/旋转/缩放；快捷键应在 **`ImGuiLayer::begin_frame()` 之后** 用 `ImGui::IsKeyPressed(ImGuiKey_*)` 处理——若在 `push_layer` 内对 `EventKeyDown` 一律用 `imgui_wants_keyboard()` 过滤，Dock 获得焦点时 `WantCaptureKeyboard` 常为 true，会导致按键永远不生效。不绘制 Gizmo 的帧须调用 `imguizmo_reset_interaction_state()`；相机俯仰用 **↑/↓** 以免与 **W** 冲突。

**方向立方体**：`imguizmo_view_manipulate` 用前景 DrawList；Demo3D 将位置锚在 **Scene 面板 `TextureViewRect` 右上角内侧**，并预留 `RightBleed`（绘制会略宽于传入矩形），避免贴 Dock 右缘时「出界」；Scene 区域过小时回退到主 `Work` 视口并同样钳位。与离屏 Scene 共用 `view` / `orbit` 半径；滞后一帧与 Gizmo 相同。操作后请调用 `lumen::scene::SceneOrbitController::sync_from_view` 同步轨道参数，并 `apply_to(SceneCamera)`（见 [scene-camera.md](scene-camera.md)）。ViewManipulate 涉及 Autodesk 专利说明，商用请注意合规。

**矩阵**：`view` 与离屏 Scene 渲染一致。`projection` 传入与渲染相同的矩阵（含 Vulkan 常用的 `proj[1][1] *= -1`）；`imguizmo_manipulate` 内部会再抵消该 Y 翻转以匹配 ImGuizmo 的 OpenGL 风格 NDC（见 [glm-vulkan.md](../reference/glm-vulkan.md)、[ImGuizmo#154](https://github.com/CedricGuillemet/ImGuizmo/issues/154)）。另默认调用 `AllowAxisFlip(false)`，避免轴为可读性自动翻面导致「轴向不对」的主观感受。

## 4. 纹理视口扩展

`imgui_texture_view_panel` 支持可选回调 `after_image(rect)`，在 `Image` 之后、`End` 之前调用，用于叠加 Gizmo 或调试绘制；`rect` 与 `outRect` 一致（若调用方需要持久保存仍可同时传入 `outRect`）。

## 5. 参考

- [scene-camera.md](scene-camera.md) — `SceneOrbitController::sync_from_view`、ViewManipulate 后同步  
- [imgui-integration.md](imgui-integration.md) — ImGui 后端与视口矩形  
- [ui-panels.md](ui-panels.md) — `TextureViewRect`、`viewport_mouse_state`  
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
