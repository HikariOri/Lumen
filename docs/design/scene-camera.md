# 场景轨道相机与控制器（SceneOrbitCamera）

正文排版遵循 [guides/chinese-typography.md](../guides/chinese-typography.md)；文内 C++ 命名与示例对齐 [reference/cpp-style.md](../reference/cpp-style.md)。

---

## 1. 定位与职责划分

---

`lumen::scene::SceneOrbitCamera` 与 `lumen::scene::SceneCameraController` 提供 **编辑器风格** 的透视相机：绕 **枢轴（pivot）** 球坐标轨道观察，输出与 Vulkan 一致的 **视图矩阵** 与 **投影矩阵**。

* **Scene 容器**（`Scene` / EnTT）仍只表示世界对象与层级；相机 **不是** ECS 组件（当前阶段），仅为视口逻辑封装。
* **控制器** 不依赖 SDL、ImGui：只接收应用层传入的鼠标增量、按键布尔与 `delta_seconds`，便于单测与换输入后端。
* **投影** 在 `projection_matrix` 内完成 `glm::perspective` 与 **`proj[1][1] *= -1`**，与 [reference/glm-vulkan.md](../reference/glm-vulkan.md) 及 demo3d 管线一致。

---

## 2. 头文件与类型

---

| 头文件 | 内容 |
|--------|------|
| `engine/include/scene/scene_orbit_camera.hpp` | `SceneOrbitCameraLimits`、`SceneOrbitCamera`、`frame_orbit_on_drawable` |
| `engine/include/scene/scene_camera_controller.hpp` | `SceneCameraControllerSettings`、`SceneCameraFlyInput`、`SceneCameraController` |

---

## 3. 数学约定

---

### 3.1 轨道模型

* **枢轴** `pivot`：世界空间中 `lookAt` 的目标点。
* **偏航 / 俯仰** `yaw`、`pitch`（弧度）：在竖直轴与水平面内描述从枢轴指向相机的方向；默认俯仰限制在 `[0.1, 1.4]`（与历史 demo 行为一致，避免万向锁附近翻转）。
* **半径** `radius`：枢轴到相机的距离，默认限制在 `[0.8, 20]`（可调 `SceneOrbitCameraLimits`）。

相机世界位置：

```text
eye = pivot + radius * orbit_direction(yaw, pitch)
```

其中 `orbit_direction` 与原先 demo3d 中 `sin/cos` 组合一致。

### 3.2 投影

* 竖直视场角默认 **42°**（`set_fov_y_degrees` 可改）。
* `projection_matrix(aspect)` 中 `aspect = width / height`（与视口像素宽高比一致）。

---

## 4. API 要点

---

### 4.1 `SceneOrbitCamera`

* `view_matrix()`：`glm::lookAt(eye, pivot, world_up)`，默认 `world_up = (0,1,0)`。
* `eye_position()`：用于 UBO 中相机位置、调试绘制等。
* `apply_scroll_zoom(delta_y, zoom_speed)`：滚轮缩放半径（与 demo 中「滚轮拉近/远」一致）。
* `apply_radius_scale_drag(mouse_delta_y, sensitivity)`：**Alt + 右键拖拽** 的倍率缩放。
* `sync_orbit_from_view(view)`：在 **ImGuizmo::ViewManipulate**（`imguizmo_view_manipulate`）改写视图矩阵之后调用，在 **保持当前 pivot 不变** 的前提下反推 `yaw` / `pitch` / `radius`。详见 [gizmos.md](gizmos.md)。

### 4.2 `SceneCameraController`

默认灵敏度与飞行速度与 refactor 前 `demo3d` 一致，可通过 `set_settings` 覆盖。

| 方法 | 典型绑定 |
|------|----------|
| `apply_alt_orbit` | Alt + 左键拖拽 |
| `apply_alt_pan` | Alt + 中键拖拽 |
| `apply_alt_zoom_drag` | Alt + 右键拖拽 |
| `apply_rmb_look` | 右键拖拽（非 Alt，飞行模式外环视） |
| `apply_fly_pan` | 右键按下时 WASD + E/Q，配合 `SceneCameraFlyInput` |

### 4.3 `frame_orbit_on_drawable`

将枢轴设为世界空间中 **网格局部 AABB 中心**，并按包围盒半尺寸与世界缩放估算合适 `radius`。应用需提供 **模型空间** 的 `mesh_center_local` 与 `mesh_half_extents_local`（与 OBJ 顶点坐标同空间）。`demo3d` 在启动与按 **F** 取景时调用。

---

## 5. 与示例 demo3d 的对应关系

---

* 每帧在应用输入之后：`scene_view = scene_cam.view_matrix()`，`scene_proj = scene_cam.projection_matrix(aspect)`，供离屏 Pass 与 ImGuizmo 使用。
* 主界面 Dock 内 ImGui 渲染阶段：`imguizmo_view_manipulate` 后紧跟 `scene_cam.sync_orbit_from_view(scene_view)`。
* 滚轮在 Scene 视口悬停时：`scene_cam.apply_scroll_zoom`（仍注意与 `imgui_wants_mouse` 的优先级，由应用层判断）。

---

## 6. 扩展建议

---

* 将相机做成 **ECS 组件** 或多视口相机列表时，可保留本类为「轨道模式」实现，再增加 FPS、路径动画等策略对象。
* **旋转枢轴跟手**：当前枢轴由用户平移 / 取景（F）显式更新；若需「物体移动后枢轴自动跟随」，可在 Transform 变更时调用 `frame_orbit_on_drawable` 或单独维护偏移量（避免与 Alt+中键平移语义冲突）。
* **拾取求交后设 pivot**：可在射线与网格交点处 `set_pivot`，实现 Maya 式旋转中心；需渲染侧提供拾取或深度反投影。

---

## 7. 相关文档

---

* [gizmos.md](gizmos.md) — ImGuizmo、ViewManipulate、帧序
* [imgui-integration.md](imgui-integration.md) — ImGui Vulkan 后端
* [scene-ecs.md](scene-ecs.md) — Scene 职责边界（相机非 Scene 本体职责）
* [glm-vulkan.md](../reference/glm-vulkan.md) — 投影与 NDC
