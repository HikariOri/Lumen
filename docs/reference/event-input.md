# 事件与输入系统

> 平台无关的事件负载（`std::variant`）+ SDL3 轮询；对齐 **Hazel** 的 `EventDispatcher`、`Handled`、Overlay / Layer 顺序与 ImGui 阻塞规则。

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│  应用层：set_on_application_event → ImGuiLayer::attach → push_layer；poll │
│  层内：EventDispatcher d(de); d.dispatch<EventKeyDown>(...);              │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  每条平台 Event：SDL 钩子 → 转 Event + 更新 Input → 应用回调 → Overlay 链   │
│  若 handled，不再向后续 Overlay/Layer 传递（对齐 Hazel）                   │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  ui/ImGuiLayer：SDL→ImGui、OnEvent（WantCapture → handled）                 │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
┌───────────────────────────────▼─────────────────────────────────────────┐
│  SDL3：SDL_PollEvent、SDL_GetKeyboardState、…                              │
└─────────────────────────────────────────────────────────────────────────┘
```

- **event.hpp**：`Event`（variant）、KeyCode、Modifier、MouseButton
- **event_dispatcher.hpp / .cpp**：`DispatchableEvent`、`EventDispatcher`、`EventCategory`、`event_categories`
- **event_pump.hpp / .cpp**：`EventPump`（`poll`、`set_on_application_event`、`push_overlay`、`push_layer`、`Input`）
- **ui/imgui_layer.hpp**：`ImGuiLayer`（`attach`、`begin_frame`、`end_frame`、`on_event`）、`imgui_wants_*`

## 2. 文件结构

```
engine/
├── include/platform/
│   ├── event.hpp              # 事件类型、KeyCode、Modifier、MouseButton
│   ├── event_dispatcher.hpp   # DispatchableEvent、EventDispatcher、分类掩码
│   ├── event_debug.hpp        # add_input_debug_handler
│   ├── event_pump.hpp         # EventPump
│   └── input.hpp              # Input
├── include/ui/
│   └── imgui_layer.hpp
└── src/
    ├── platform/
    │   ├── event_debug.cpp
    │   ├── event_dispatcher.cpp
    │   ├── event_pump.cpp
    │   ├── event_utils.cpp
    │   └── input.cpp
    └── ui/
        └── imgui_layer.cpp
```

## 3. 事件类型

| 事件 | 结构体 | 说明 |
|------|--------|------|
| Quit | `EventQuit` | 退出（`SDL_QUIT` / 窗口关闭请求） |
| 键盘 | `EventKeyDown`, `EventKeyUp` | key, repeat, mods |
| 鼠标按键 | `EventMouseButtonDown`, `EventMouseButtonUp` | button, x, y |
| 鼠标移动 | `EventMouseMove` | x, y, deltaX, deltaY |
| 滚轮 | `EventMouseWheel` | deltaX, deltaY |
| 窗口 | `EventWindowResize` 等 | width / height 或状态 |

负载类型为 `Event`（`std::variant<...>`）。

## 4. Hazel 式层链与 ImGui

### 4.1 `poll()` 内顺序

1. `SDL_PollEvent`
2. 依次调用 `sdl_event_handlers_`（含 `ImGui_ImplSDL3_ProcessEvent`）
3. 将 SDL 事件转为 `Event`，更新 `Input`
4. 对每条 `Event` 构造 `DispatchableEvent`，先调用 **`set_on_application_event`**（对齐 Hazel `Application::OnEvent` 里先于 Layer 栈的 `Dispatch`）
5. 若仍未 `handled`，再自前向后遍历 **Overlay / Layer** 栈；任一层置 `handled` 后停止向下传递

### 4.2 Overlay / Layer 顺序

- `push_overlay(fn)`：插在**队首**，最先收到事件（对应 Hazel **Overlay**；`ImGuiLayer::attach` 会注册 ImGui 的 Overlay）。
- `push_layer(fn)`：追加在**队尾**（对应 **Layer**）。

推荐顺序：`pump.set_on_application_event(...)`（窗口关闭、resize 等）→ `imgui_layer.attach(pump)` → `pump.push_layer(...)`（游戏输入）。

### 4.3 `ImGuiLayer`（对齐 Hazel `ImGuiLayer`）

```cpp
#include "ui/imgui_layer.hpp"

// 在 imgui_backend_init 之后
lumen::ui::ImGuiLayer imgui_layer;
imgui_layer.attach(pump);
```

`attach` 会：`add_sdl_event_handler(imgui_process_sdl_event)` + `push_overlay`（`on_event` 内按 `WantCapture*` 写 `handled`）。同一 `ImGuiLayer` 实例勿重复 `attach`。`set_block_events(false)` 对应 Hazel `m_BlockEvents == false`。

渲染帧范围（对齐 Hazel `Begin` / `End`）：

```
imgui_layer.begin_frame();
// 此处写 ImGui::Begin / End（等同各层 OnImGuiRender）
imgui_layer.end_frame(cmd);  // 须在 swapchain RenderPass 内
```

### 4.4 `set_on_application_event`（对齐 Hazel `Application::OnEvent` 前缀）

```cpp
pump.set_on_application_event([&](lumen::platform::DispatchableEvent& de) {
    lumen::platform::EventDispatcher d(de);
    d.dispatch<lumen::platform::EventWindowResize>(
        [&](lumen::platform::EventWindowResize& r) {
            fbWidth = r.width;
            fbHeight = r.height;
            needRecreateSwapchain = true;
            return false;
        });
    d.dispatch<lumen::platform::EventQuit>([&](lumen::platform::EventQuit&) {
        running = false;
        return true;
    });
});
```

`dispatch` 仅在与 `de.event` 中实际类型匹配时调用处理器；处理器返回 `bool` 时会 **`|=`** 合并到 `handled`。

### 4.5 每帧 `Input` 与 `imgui_wants_*`

ImGui 占用鼠标/键盘时，Overlay 可能已将对应平台事件标为 `handled`，游戏 **Layer 可能收不到**。对直接读 `pump.input()` 的逻辑，仍可用 `imgui_wants_mouse()` 等。

`imgui_wants_*` 在 **`ImGuiLayer::begin_frame()`**（内部 `NewFrame`）之后才有完整语义。主循环顺序建议对齐 Hazel `Run`：

```
poll() → 游戏 OnUpdate（逻辑、相机）→ begin_frame() → 构建 UI → 3D 渲染 → RenderPass 内 end_frame(cmd)
```

### 4.6 多路 SDL 回调

- `on_sdl_event(f)`：清空后只保留 `f`
- `add_sdl_event_handler(f)`：追加

`ImGuiLayer::attach` 已 `add_sdl_event_handler`；自定义 SDL 处理请在 **`attach` 之后** 再 `add_sdl_event_handler`。

## 5. KeyCode 与 Modifier

- **KeyCode**：物理扫描码（与 SDL_Scancode 一致）
- **Key 命名空间**：`Key::W`、`Key::Escape` 等
- **Modifier**：`Shift`、`Ctrl`、`Alt`、`Gui`，`has_modifier` 等

## 6. 基本用法（无 ImGui）

```cpp
#include "platform/event_pump.hpp"

lumen::platform::EventPump pump;
bool running = true;

pump.push_layer([&](lumen::platform::DispatchableEvent& de) {
    lumen::platform::EventDispatcher d(de);
    d.dispatch<lumen::platform::EventQuit>([&](lumen::platform::EventQuit&) {
        running = false;
        return false;
    });
    d.dispatch<lumen::platform::EventKeyDown>([&](lumen::platform::EventKeyDown& e) {
        if (e.key == lumen::platform::Key::Escape)
            running = false;
        return false;
    });
    d.dispatch<lumen::platform::EventWindowResize>(
        [&](lumen::platform::EventWindowResize& r) {
            fbWidth = r.width;
            fbHeight = r.height;
            needRecreateSwapchain = true;
            return false;
        });
});

while (running && pump.poll()) {
    const auto& input = pump.input();
    if (input.is_key_down(lumen::platform::Key::W))
        camera.move_forward();
}
```

## 7. 基本用法（含 ImGui）

```cpp
#include "platform/event_pump.hpp"
#include "ui/imgui_backend.hpp"
#include "ui/imgui_layer.hpp"

lumen::ui::imgui_backend_init(imguiInfo);

lumen::ui::ImGuiLayer imgui_layer;
imgui_layer.attach(pump);

pump.set_on_application_event([&](lumen::platform::DispatchableEvent& de) {
    lumen::platform::EventDispatcher d(de);
    d.dispatch<lumen::platform::EventQuit>([&](lumen::platform::EventQuit&) {
        running = false;
        return true;
    });
});

pump.push_layer([&](lumen::platform::DispatchableEvent& de) {
    lumen::platform::EventDispatcher d(de);
    d.dispatch<lumen::platform::EventMouseWheel>(
        [&](lumen::platform::EventMouseWheel& e) {
            if (!lumen::ui::imgui_wants_mouse())
                zoom(e.deltaY);
            return false;
        });
});

while (running && pump.poll()) {
    // 与 Hazel 一致：Update 之后再 Begin
    imgui_layer.begin_frame();
    // ImGui::Begin / End …
    imgui_layer.end_frame(cmd);
}
```

## 8. Input 状态查询

| 方法 | 说明 |
|------|------|
| `is_key_down(KeyCode)` | 按键是否按下 |
| `mouse_x()`, `mouse_y()` | 鼠标位置 |
| `mouse_delta_x()`, `mouse_delta_y()` | 本帧位移 |
| `is_mouse_button_down(MouseButton)` | 鼠标键 |
| `modifiers()`、`has_shift()` 等 | 修饰键 |

`Input` 在每次 `poll()` 末尾更新。

## 9. 名称查询

```cpp
lumen::platform::key_name(Key::W);
lumen::platform::mouse_button_name(MouseButton::Left);
lumen::platform::event_type_name(someEvent);
lumen::platform::modifier_name(Modifier::Shift);
```

## 10. 注意事项

### 10.1 调用顺序

- 在 Window 创建且 `SDL_Init` 已执行后使用 `poll()`
- 每帧通常调用一次 `poll()`，放在帧逻辑最前

### 10.2 退出

- `EventQuit` 经层链分发后，`poll()` 返回 `false`。应在 `push_layer` 内 `dispatch<EventQuit>` 设置 `running = false` 等资源清理（引擎不再提供 `on_quit`）。

### 10.3 线程安全

- `EventPump` 非线程安全，主线程使用

### 10.4 键重复

- `EventKeyDown::repeat == true` 表示系统重复键

### 10.5 相对鼠标模式

相对鼠标模式请用 `Window::set_relative_mouse_mode`；启用前结合 `imgui_wants_mouse()`，见 [../design/imgui-integration.md](../design/imgui-integration.md)。

## 11. `Window::poll_events`

内部临时 `EventPump::poll()`，无层回调。需要 `EventDispatcher` / `Input` 时请自建 `EventPump`。

## 12. 输入调试

```cpp
#include "platform/event_debug.hpp"

// 若使用 ImGui，在 ImGuiLayer::attach 之后
lumen::platform::add_input_debug_handler(pump);
```

## 13. 扩展

- 新事件类型：改 `event.hpp` 的 variant、`event_pump.cpp` 的转换、`event_dispatcher.cpp` 的 `event_categories`、`event_type_name` 等
- 多窗口库：可另做 `event_pump_*.cpp`，保持 `Event` + `DispatchableEvent` 接口
