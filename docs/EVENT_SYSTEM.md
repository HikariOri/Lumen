# 事件与输入系统

> 平台无关的事件模型 + SDL3 实现，支持 ImGui 分层输入，提供键盘、鼠标、窗口事件与输入状态查询。

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           应用层 (Application)                            │
│  EventPump pump; imgui_setup_event_pump(pump); pump.on_key_down(...)     │
│  while (pump.poll()) { if (!imgui_wants_any_input()) game_logic(); }     │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  分层输入：SDL 事件 → [ImGui 处理] → [平台 EventPump] → 回调 + Input       │
│  ui/input_bridge: imgui_process_sdl_event, imgui_wants_mouse/keyboard    │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  platform: EventPump │ Input │ Event (variant)                           │
│  add_sdl_event_handler(imgui_process_sdl_event) → 事件先给 ImGui         │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  SDL3: SDL_PollEvent, SDL_GetKeyboardState, ...                          │
└─────────────────────────────────────────────────────────────────────────┘
```

- **event.hpp**：平台无关的事件类型、KeyCode、Modifier、MouseButton
- **input.hpp**：输入状态（按键、鼠标位置、delta）
- **event_pump.hpp/cpp**：事件轮询、多路 SDL 回调、Input 更新、应用回调分发
- **ui/input_bridge.hpp**：SDL→ImGui 事件转发、`imgui_wants_*` 查询

## 2. 文件结构

```
engine/
├── include/platform/
│   ├── event.hpp       # 事件类型、KeyCode、Modifier、MouseButton
│   ├── event_debug.hpp # 输入调试（add_input_debug_handler）
│   ├── event_pump.hpp  # EventPump（轮询、多路 SDL 回调、分发）
│   └── input.hpp       # Input 状态
├── include/ui/
│   └── input_bridge.hpp  # ImGui 输入桥接
└── src/
    ├── platform/
    │   ├── event_debug.cpp
    │   ├── event_pump.cpp
    │   ├── event_utils.cpp
    │   └── input.cpp
    └── ui/
        └── input_bridge.cpp
```

## 3. 事件类型

| 事件 | 结构体 | 说明 |
|------|--------|------|
| Quit | `EventQuit` | 退出请求（SDL_QUIT / 窗口关闭） |
| 键盘 | `EventKeyDown`, `EventKeyUp` | key, repeat, mods |
| 鼠标按键 | `EventMouseButtonDown`, `EventMouseButtonUp` | button, x, y |
| 鼠标移动 | `EventMouseMove` | x, y, deltaX, deltaY |
| 滚轮 | `EventMouseWheel` | deltaX, deltaY |
| 窗口 | `EventWindowResize` | width, height |

所有事件统一为 `std::variant<...>`，即 `Event` 类型。

## 4. SDL 事件与 ImGui 分层

### 4.1 处理流程

每帧 `poll()` 内部：

1. `SDL_PollEvent` 取事件
2. **先**调用所有 `sdl_event_handlers_`（含 ImGui）→ 事件先到 ImGui
3. 平台转换事件、更新 Input、dispatch 应用回调

因此 ImGui 与 SDL 输入系统是**分层**的：同一条 SDL 事件既给 ImGui，也用于平台 Input 和回调。

### 4.2 注册 ImGui 事件处理

```cpp
#include "ui/input_bridge.hpp"

// 在 imgui_backend_init 之后
lumen::ui::imgui_setup_event_pump(pump);
```

等价于 `pump.add_sdl_event_handler(lumen::ui::imgui_process_sdl_event)`。

### 4.3 游戏输入过滤（WantCapture）

ImGui 有焦点或悬停在控件上时，游戏逻辑应**忽略**对应输入。使用 `imgui_wants_*`：

```cpp
#include "ui/input_bridge.hpp"

// 仅鼠标（相机、拖拽等）
if (!lumen::ui::imgui_wants_mouse()) {
    if (pump.input().is_mouse_button_down(lumen::platform::MouseButton::Right))
        orbit_camera(pump.input().mouse_delta_x(), ...);
}

// 仅键盘（WASD 等）
if (!lumen::ui::imgui_wants_keyboard()) {
    if (pump.input().is_key_down(lumen::platform::Key::W))
        camera.move_forward();
}

// 任一输入
if (!lumen::ui::imgui_wants_any_input()) {
    // 统一跳过游戏逻辑
}
```

**注意**：`imgui_wants_*` 在 `imgui_backend_new_frame()` 之后才有意义，且通常在渲染 ImGui 之前。主循环顺序应为：

```
poll() → imgui_backend_new_frame() → [可选 resize] → 游戏逻辑（检查 wants_*）→ 渲染 3D → 渲染 ImGui → imgui_backend_render()
```

### 4.4 多路 SDL 回调

- `on_sdl_event(f)`：**替换**所有 SDL 回调（仅保留 f）
- `add_sdl_event_handler(f)`：**追加**，不覆盖已有回调

若需 ImGui + 自定义逻辑：

```cpp
lumen::ui::imgui_setup_event_pump(pump);  // 先加 ImGui
pump.add_sdl_event_handler([](const void* ev) {
    // 自定义 SDL 事件处理
});
```

或手动组合：

```cpp
pump.on_sdl_event([](const void* ev) {
    lumen::ui::imgui_process_sdl_event(ev);
    my_sdl_handler(ev);
});
```

## 5. KeyCode 与 Modifier

- **KeyCode**：物理扫描码（与 SDL_Scancode 一致），与键盘布局无关
- **Key 命名空间**：`Key::W`, `Key::A`, `Key::Escape`, `Key::Space` 等
- **Modifier**：`Shift`, `Ctrl`, `Alt`, `Gui`，支持 `|` 组合，`has_modifier(mask, m)` 检查

## 6. 基本用法（无 ImGui）

```cpp
#include "platform/event_pump.hpp"

lumen::platform::EventPump pump;
bool running = true;

pump.on_quit([&] { running = false; });
pump.on_key_down([&](const lumen::platform::EventKeyDown& e) {
    if (e.key == lumen::platform::Key::Escape)
        running = false;
});
pump.on_window_resize([&](const lumen::platform::EventWindowResize& r) {
    fbWidth = r.width;
    fbHeight = r.height;
    needRecreateSwapchain = true;
});

while (running && pump.poll()) {
    const auto& input = pump.input();
    if (input.is_key_down(lumen::platform::Key::W))
        camera.move_forward();
    // 渲染...
}
```

## 7. 基本用法（含 ImGui）

```cpp
#include "platform/event_pump.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/input_bridge.hpp"

// ImGui 初始化
lumen::ui::imgui_backend_init(imguiInfo);
lumen::ui::imgui_setup_event_pump(pump);

pump.on_quit([&] { running = false; });
pump.on_mouse_wheel([&](const lumen::platform::EventMouseWheel& e) {
    if (!lumen::ui::imgui_wants_mouse())
        zoom(e.deltaY);
});

while (running && pump.poll()) {
    lumen::ui::imgui_backend_new_frame();
    // ...
    const auto& inp = pump.input();
    if (!lumen::ui::imgui_wants_any_input()) {
        if (inp.is_key_down(lumen::platform::Key::W)) move_forward();
        if (inp.is_mouse_button_down(lumen::platform::MouseButton::Right))
            orbit(inp.mouse_delta_x(), inp.mouse_delta_y());
    }
    // 渲染 3D + ImGui
    lumen::ui::imgui_backend_render(cmd);
}
```

## 8. Input 状态查询

| 方法 | 说明 |
|------|------|
| `is_key_down(KeyCode)` | 按键是否按下 |
| `mouse_x()`, `mouse_y()` | 鼠标位置（窗口坐标） |
| `mouse_delta_x()`, `mouse_delta_y()` | 本帧鼠标位移 |
| `is_mouse_button_down(MouseButton)` | 鼠标按钮是否按下 |
| `modifiers()` | 当前修饰键 |
| `has_shift()`, `has_ctrl()`, `has_alt()` | 修饰键快捷检查 |

**注意**：Input 仅在 `poll()` 之后有效，每帧由 EventPump 更新。

## 9. 名称查询

```cpp
lumen::platform::key_name(Key::W);
lumen::platform::mouse_button_name(MouseButton::Left);
lumen::platform::event_type_name(someEvent);
lumen::platform::modifier_name(Modifier::Shift);
```

## 10. 注意事项

### 10.1 调用顺序

- **必须在 Window 创建之后** 调用 `poll()`
- 每帧**只调用一次** `poll()`，在渲染逻辑之前

### 10.2 回调执行时机

- `on_quit`：收到 Quit 时立即调用，随后 `poll()` 返回 false
- 其余回调：在 `poll()` 内部、事件处理完成后统一 `dispatch_` 调用

### 10.3 线程安全

- EventPump 非线程安全，应在主线程使用

### 10.4 键重复 (Key Repeat)

- `EventKeyDown` 的 `repeat` 为 true 表示系统按键重复，非首次按下

### 10.5 相对鼠标模式

启用 `SDL_SetWindowRelativeMouseMode` 前，应先检查 `imgui_wants_mouse()`，避免在 ImGui 控件上拖拽时误启相对模式。详见 [IMGUI_INTEGRATION.md](IMGUI_INTEGRATION.md)。

## 11. Window::poll_events 兼容

`Window::poll_events()` 内部创建临时 EventPump 并调用 `poll()`，仅用于“轮询直到退出”的简单场景。需要回调或 Input 查询时，应直接使用 EventPump。

## 12. 输入调试

需要观察鼠标键盘事件及 ImGui 捕获状态时，可使用 `event_debug.hpp`：

```cpp
#include "platform/event_debug.hpp"

// 需在 imgui_setup_event_pump 之后调用（若使用 ImGui）
lumen::platform::add_input_debug_handler(pump);
```

输出示例：

- `Input KeyDown: W repeat=0 [ImGui: game]` — 键盘事件，由游戏处理
- `Input MouseButtonDown: Left at (120.0, 340.0) [ImGui: capture]` — 鼠标在 ImGui 控件上，被 ImGui 捕获
- `Input MouseWheel: delta=(0.0, 1.0) [ImGui: game]` — 滚轮在 3D 视口，由游戏处理

`[ImGui: capture]` 表示该帧 WantCapture 为 true，游戏逻辑应忽略该输入；`[ImGui: game]` 表示由游戏处理。日志输出到 `logs/engine.log`，Release 下无输出。

## 13. 扩展

- 新增事件类型：在 `event.hpp` 增加结构体，并加入 `Event` variant、`dispatch_` 和 `event_type_name`
- 新增回调：在 EventPump 增加 `on_xxx` 与对应 `xxxFn` 类型
- 多后端：若需支持其他窗口库，可新增对应 `event_pump_xxx.cpp`
