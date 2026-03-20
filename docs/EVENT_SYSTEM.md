# 事件与输入系统

> 平台无关的事件模型 + SDL3 实现，提供键盘、鼠标、窗口事件与输入状态查询。

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────┐
│                     应用层 (Application)                  │
│  EventPump pump; pump.on_key_down(...); while(pump.poll())│
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│  EventPump  │  Input  │  Event (variant)                 │
│  回调注册   │  状态查询  │  平台无关事件类型               │
└─────────────────────────┬───────────────────────────────┘
                          │
┌─────────────────────────▼───────────────────────────────┐
│  SDL3: SDL_PollEvent, SDL_GetKeyboardState, ...          │
└─────────────────────────────────────────────────────────┘
```

- **event.hpp**：平台无关的事件类型、KeyCode、Modifier、MouseButton
- **input.hpp**：输入状态（按键、鼠标位置、delta）
- **event_pump.hpp/cpp**：事件轮询 + 回调分发，内部持有 Input
- **event_utils.cpp**：名称查询（key_name, event_type_name 等）

## 2. 文件结构

```
engine/
├── include/platform/
│   ├── event.hpp       # 事件类型、KeyCode、Modifier、MouseButton、名称声明
│   ├── event_pump.hpp  # EventPump（轮询 + 回调）
│   └── input.hpp       # Input 状态
└── src/platform/
    ├── event_pump.cpp  # SDL3 事件转换与分发
    ├── event_utils.cpp # key_name、event_type_name 实现
    └── input.cpp       # Input 状态实现
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

## 4. KeyCode 与 Modifier

- **KeyCode**：物理扫描码（与 SDL_Scancode 一致），与键盘布局无关，适合游戏
- **Key 命名空间**：`Key::W`, `Key::A`, `Key::Escape`, `Key::Space` 等常量
- **Modifier**：`Shift`, `Ctrl`, `Alt`, `Gui`，支持 `|` 组合，`has_modifier(mask, m)` 检查

## 5. 基本用法

```cpp
#include "platform/event_pump.hpp"

lumen::platform::EventPump pump;
bool running = true;

// 按需注册回调（未注册则忽略）
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

// 主循环：poll() 返回 false 表示应退出
while (running && pump.poll()) {
    const auto& input = pump.input();
    if (input.is_key_down(lumen::platform::Key::W))
        camera.move_forward();
    if (input.is_mouse_button_down(lumen::platform::MouseButton::Left))
        handle_drag(input.mouse_x(), input.mouse_y());
    // 渲染...
}
```

## 6. Input 状态查询

| 方法 | 说明 |
|------|------|
| `is_key_down(KeyCode)` | 按键是否按下 |
| `mouse_x()`, `mouse_y()` | 鼠标位置（窗口坐标） |
| `mouse_delta_x()`, `mouse_delta_y()` | 本帧鼠标位移 |
| `is_mouse_button_down(MouseButton)` | 鼠标按钮是否按下 |
| `modifiers()` | 当前修饰键 |
| `has_shift()`, `has_ctrl()`, `has_alt()` | 修饰键快捷检查 |

**注意**：Input 仅在 `poll()` 之后有效，每帧由 EventPump 更新。

## 7. 名称查询

```cpp
lumen::platform::key_name(Key::W);           // "W"
lumen::platform::key_name(Key::Escape);      // "Escape"
lumen::platform::mouse_button_name(MouseButton::Left);  // "Left"
lumen::platform::event_type_name(someEvent); // "KeyDown", "MouseMove" 等
lumen::platform::modifier_name(Modifier::Shift);        // "Shift"
```

- `key_name`：依赖 SDL_GetScancodeName，跨平台名称可能略有差异
- `modifier_name`：组合修饰键时返回第一个非 None 的名称

## 8. 注意事项

### 8.1 调用顺序

- **必须在 Window 创建之后** 调用 `poll()`，因为依赖 SDL_Init
- 每帧**只调用一次** `poll()`，在渲染逻辑之前

### 8.2 回调执行时机

- `on_quit`：收到 Quit 时立即调用，随后 `poll()` 返回 false
- 其余回调：在 `poll()` 内部、事件处理完成后统一 `dispatch_` 调用
- 若在回调中再次调用 `poll()`，可能导致递归或重复处理，应避免

### 8.3 线程安全

- EventPump 非线程安全，应在主线程使用
- SDL 事件 API 要求主线程调用

### 8.4 键重复 (Key Repeat)

- `EventKeyDown` 的 `repeat` 为 true 表示系统按键重复，非首次按下
- 若只需响应“首次按下”，可忽略 `repeat == true` 的事件

### 8.5 窗口 Resize

- 同时处理 `SDL_EVENT_WINDOW_RESIZED` 与 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`
- 应用层收到 `EventWindowResize` 后应重建 Swapchain、Framebuffer 等

### 8.6 KeyCode 与 Keycode

- 使用 **Scancode**（KeyCode），不用 SDL_Keycode
- Scancode 对应物理键位，与键盘布局无关；Keycode 与布局相关

## 9. Window::poll_events 兼容

`Window::poll_events()` 内部创建临时 EventPump 并调用 `poll()`，仅用于“轮询直到退出”的简单场景。需要回调或 Input 查询时，应直接使用 EventPump。

## 10. 扩展

- 新增事件类型：在 `event.hpp` 增加结构体，并加入 `Event` variant、`dispatch_` 和 `event_type_name`
- 新增回调：在 EventPump 增加 `on_xxx` 与对应 `xxxFn` 类型
- 多后端：若需支持其他窗口库，可新增对应 `event_pump_xxx.cpp`，通过编译选项切换实现
