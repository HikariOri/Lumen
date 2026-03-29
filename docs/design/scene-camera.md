# 场景相机与轨道控制器（SceneCamera / SceneOrbitController）

正文排版遵循 [guides/chinese-typography.md](../guides/chinese-typography.md)；文内 C++ 命名与示例对齐 [reference/cpp-style.md](../reference/cpp-style.md)。

---

## 1. 定位与职责划分

---

* **`SceneCamera`**（`scene_camera.hpp`）：仅表示 **透视或正交投影** 与 **`lookAt(eye, target, world_up)`** 视图；不负责轨道、飞行等导航语义。
* **`ISceneCameraController`**（`scene_camera_controller.hpp`）：**导航控制器接口**，仅纯虚 `apply_to(SceneCamera &)`。
* **`SceneOrbitController`**（`scene_orbit_controller.hpp`）：继承 `ISceneCameraController`；**编辑器式轨道**（枢轴 + yaw / pitch / radius），`apply_to` 写入 lookAt；嵌套类型 `Settings` / `Limits` / `FlyInput`；`apply_alt_orbit` 等 **直接修改本对象**，不依赖 SDL、ImGui。另有 **`apply_per_frame_editor_navigation`**：用 `platform::Input` + 视口悬停与 ImGui 抢鼠标标志，在 **每帧** 内一次性调用上述 `apply_*`（供 ImGui 场景视口示例使用）。

* **Scene 容器**（`Scene` / EnTT）仍只表示世界对象与层级；相机 **不是** ECS 组件（当前阶段），仅为视口逻辑封装。
* **投影** 在 `SceneCamera::projection_matrix` 内完成 `glm::perspective` / `glm::ortho` 与 **`proj[1][1] *= -1`**，与 [reference/glm-vulkan.md](../reference/glm-vulkan.md) 及示例管线一致。

---

## 2. 头文件与类型

---

| 头文件 | 内容 |
|--------|------|
| `engine/include/scene/scene_camera.hpp` | `SceneCameraProjection`、`SceneCamera` |
| `engine/include/scene/scene_camera_controller.hpp` | `ISceneCameraController` |
| `engine/include/scene/scene_orbit_controller.hpp` | `SceneOrbitController`（含嵌套 `Settings`、`Limits`、`FlyInput`）、`frame_orbit_on_drawable` |

---

## 3. 数学约定

---

### 3.1 轨道模型（`SceneOrbitController`）

* **枢轴** `pivot`：世界空间中 `lookAt` 的目标点。
* **偏航 / 俯仰** `yaw`、`pitch`（弧度）：默认俯仰限制在 `[0.1, 1.4]`（与历史 demo 行为一致）。
* **半径** `radius`：枢轴到相机的距离，默认限制在 `[0.8, 20]`（可调 `SceneOrbitController::Limits`）。

相机世界位置：

```text
eye = pivot + radius * orbit_direction(yaw, pitch)
```

### 3.2 投影（`SceneCamera`）

* **透视**：`set_projection_perspective(fov_y_degrees, z_near, z_far)`；默认竖直视场角 **42°**（构造默认值，可在应用中覆盖）。
* **正交**：`set_projection_orthographic(half_height, z_near, z_far)`，`half_height` 为竖直半高（世界单位），水平范围由 `aspect` 推导。
* `projection_matrix(aspect)` 中 `aspect = width / height`。

---

## 4. API 要点

---

### 4.1 `SceneCamera`

* `set_look_at(eye, target, world_up)`、`view_matrix()`、`eye_position()`、`target()`。
* `projection_matrix(aspect)`：按当前模式返回矩阵。

### 4.2 `ISceneCameraController` / `SceneOrbitController`

* 接口约定：`apply_to(SceneCamera &)` 将导航状态同步到相机。
* `SceneOrbitController::apply_to`：用当前轨道解算 `cam.set_look_at(eye, pivot, world_up)`；宜在每帧取 `view_matrix` 之前调用（或在每次输入后调用）。
* `apply_scroll_zoom`、`apply_radius_scale_drag`：滚轮与 Alt + 右键拖拽缩放半径。
* `sync_from_view(view)`：在 **ImGuizmo::ViewManipulate**（`imguizmo_view_manipulate`）改写视图矩阵之后调用，在 **保持当前 pivot 不变** 的前提下反推 `yaw` / `pitch` / `radius`。详见 [gizmos.md](gizmos.md)。
* 视口输入（默认灵敏度与 refactor 前 `demo3d` 一致，由 `SceneOrbitController::set_settings`（`SceneOrbitController::Settings`）配置）。`mouse_smooth_time_seconds`、`fly_velocity_smooth_time_seconds` 控制鼠标增量与 WASD 目标速度的指数平滑（置 `0` 可关闭对应平滑）。

| 方法 | 典型绑定 |
|------|----------|
| `apply_alt_orbit` | Alt + 左键拖拽 |
| `apply_alt_pan` | Alt + 中键拖拽 |
| `apply_alt_zoom_drag` | Alt + 右键拖拽 |
| `apply_rmb_look` | 右键拖拽（非 Alt，飞行模式外环视） |
| `apply_fly_pan` | 右键按下时 WASD + E/Q，配合 `SceneOrbitController::FlyInput` |
| `apply_per_frame_editor_navigation` | 聚合 Alt 轨道 + 右键飞行/环视；参数含 `viewport_hovered`、`imgui_blocks_scene_mouse`（如 `imgui_wants_mouse() && !inViewport`）、`delta_seconds`；返回是否应开相对鼠标 |

### 4.3 `frame_orbit_on_drawable`

将枢轴设为世界空间中 **网格局部 AABB 中心**，并按包围盒半尺寸与世界缩放估算合适 `radius`。第一个参数为 **`SceneOrbitController &`**。应用需提供 **模型空间** 的 `mesh_center_local` 与 `mesh_half_extents_local`。

---

## 5. 与示例的对应关系

---

* 每帧：`orbit.apply_per_frame_editor_navigation(...)`（或手写各 `apply_*`），再 `orbit.apply_to(scene_cam)`，然后 `scene_view = scene_cam.view_matrix()`，`scene_proj = scene_cam.projection_matrix(aspect)`。
* `imguizmo_view_manipulate` 后：`orbit.sync_from_view(scene_view)`，再 `orbit.apply_to(scene_cam)`。
* 滚轮在 Scene 视口悬停时：`orbit.apply_scroll_zoom`（与 `imgui_wants_mouse` 的优先级由应用层判断）。

---

## 6. 扩展建议

---

* 将相机做成 **ECS 组件** 或多视口相机列表时，可保留 `SceneOrbitController` 为「轨道」实现，再增加其它 **`ISceneCameraController`** 子类（如 FPS），统一通过 `apply_to` 向 `SceneCamera` 写入视图。
* **旋转枢轴跟手**：可在 Transform 变更时调用 `frame_orbit_on_drawable` 或单独维护偏移量。

---

## 7. 相关文档

---

* [gizmos.md](gizmos.md) — ImGuizmo、ViewManipulate、帧序
* [imgui-integration.md](imgui-integration.md) — ImGui Vulkan 后端
* [scene-ecs.md](scene-ecs.md) — Scene 职责边界（相机非 Scene 本体职责）
* [glm-vulkan.md](../reference/glm-vulkan.md) — 投影与 NDC
