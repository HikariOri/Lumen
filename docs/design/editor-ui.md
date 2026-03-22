# Editor UI 系统设计文档（ImGui + Console + Command · 递进版）

---

# 1. 概述

---

## 1.1 目标

构建一个完整的编辑器 UI 系统：

```text
Editor = UI + Command + State + Tooling
```

---

## 1.2 系统定位

---

UI 系统属于：

```text
引擎工具层（Editor Layer）
```

---

不属于：

* ECS
* RenderGraph
* Runtime Gameplay

---
## 1.3 ImGui 的本质（必须理解）

---

ImGui 是：

> **Immediate Mode UI（即时模式 UI）**

---

## 核心特性

```text
每帧重新构建 UI
无持久 UI 对象
状态由用户维护
```

---

即 UI 是“代码执行结果”，不是对象树

每一帧都会重新生成 UI（参见 [Unity 文档][1]）。

---
## 1.4 ImGui 的正确用途

---

适用于：

- 编辑器 UI
- Debug 工具
- 内部工具

不适用于：

- 面向玩家的正式游戏 UI（应使用独立 UI 方案或保留专门管线）

---
# 2. Editor UI 总体架构

---

```text
EditorUI
 ├── PanelManager
 ├── Panels（窗口）
 ├── UIState（状态）
 ├── CommandSystem（命令）
 ├── Console（日志 + 输入）
 └── ECS（数据源）
```

---

核心思想：

# UI = Panel + State + Command

---
# 3. Panel 系统（UI核心）

---

## 3.1 Panel 接口

---

```cpp
class IPanel {
public:
    virtual void on_imgui_render() = 0;

    bool open { true };
    std::string name;
};
```

---
## 3.2 PanelManager

---

```cpp
class PanelManager {
public:
    void register_panel(IPanel* panel);
    void render_all();

private:
    std::vector<IPanel*> panels_;
};
```

---
## 3.3 渲染流程

---

```cpp
void PanelManager::render_all() {
    for (auto* p : panels_) {
        if (p->open) {
            p->on_imgui_render();
        }
    }
}
```

---

本质：

```text
UI = 多个 Panel 的组合
```

---
# 4. 核心面板设计

---

## 4.1 Hierarchy Panel

---

作用：

```text
展示 ECS 中所有 Entity
```

---
## 4.2 Inspector Panel

---

作用：

```text
展示选中 Entity 的 Component
```

---
## 4.3 Scene Panel

---

作用：

```text
显示渲染结果（Framebuffer）
```

---
## 4.4 Console Panel（重点）

---

作用：

```text
日志显示 + 命令输入
```

---
# 5. UI State（关键）

---

## 5.1 为什么必须有？

---

ImGui 不存 UI 状态：

```text
必须由你自己管理
```

---
## 5.2 状态结构

---

```cpp
struct UIState {
    Entity selectedEntity;
    bool showGrid;
};
```

---
## 5.3 数据流

---

```text
UIState ← UI交互
UIState → 驱动UI显示
```

---
# 6. Console 系统设计

---

# 6.1 Log 数据结构

---

```cpp
enum class LogLevel {
    Info,
    Warning,
    Error
};

struct LogEntry {
    LogLevel level;
    std::string message;
};
```

---
# 6.2 LogSystem

---

```cpp
class LogSystem {
public:
    static void Log(LogLevel level, const std::string& msg);

    static const std::vector<LogEntry>& GetLogs();

private:
    static std::vector<LogEntry> logs;
};
```

---
# 6.3 Console Panel

---

功能：

```text
显示日志
支持过滤（可扩展）
支持输入命令
```

---
# 6.4 Console 输入机制

---

```cpp
ImGui::InputText("##input", buffer, size,
    ImGuiInputTextFlags_EnterReturnsTrue);
```

---

Enter 后执行命令

---
# 7. Command 系统（核心架构）

---

## 7.1 为什么必须有？

---

避免：

```text
UI 直接修改 ECS（应避免）
```

---

实现：

```text
UI → Command → ECS
```

---
## 7.2 Command 接口

---

```cpp
class ICommand {
public:
    virtual void Execute() = 0;
};
```

---
## 7.3 CommandQueue

---

```cpp
class CommandQueue {
public:
    void Submit(std::unique_ptr<ICommand> cmd);
    void ExecuteAll();

private:
    std::queue<std::unique_ptr<ICommand>> queue;
};
```

---
## 7.4 Command 执行流程

---

```text
UI提交 → Queue → 执行 → 修改ECS
```

---
# 8. 常见 Command 示例

---

## 8.1 修改 Transform

---

```cpp
class SetPositionCommand : public ICommand {
    void Execute() override;
};
```

---
## 8.2 创建 Entity

---

```cpp
class CreateEntityCommand : public ICommand {
    void Execute() override;
};
```

---
# 9. Command Parser（控制台核心）

---

## 9.1 作用

---

```text
解析字符串命令
```

---
## 9.2 示例

---

```cpp
if (cmd == "spawn")
    CreateEntity();

if (cmd.starts_with("log "))
    Log(...);
```

---
## 9.3 示例命令

---

```text
spawn
delete 1
log hello
```

---
# 10. Console + Command 融合

---

```text
Console 输入
 ↓
Command Parser
 ↓
CommandQueue
 ↓
Execute
 ↓
ECS 修改
```

---

Console 是：

# 编辑器“控制入口”

---
# 11. Selection 系统

---

## 11.1 定义

---

```cpp
struct Selection {
    Entity selected;
};
```

---
## 11.2 数据流

---

```text
Hierarchy点击
 ↓
更新 Selection
 ↓
Inspector 显示
```

---
# 12. UI 封装层（强烈推荐）

---

## **注意：** 不要直接用 ImGui API

---
## 封装

---

```cpp
namespace UI {

bool Button(const char* label);
bool Vec3Control(const char* name, Vec3& v);

}
```

---
## 优点

---

```text
统一风格
可替换UI库
减少重复代码
```

---
# 13. Docking 系统

---

## 使用

---

```cpp
ImGui::DockSpaceOverViewport();
```

---
## 作用

---

```text
面板拖拽
布局管理
多窗口编辑器
```

---
# 14. 完整系统流程

---

```text
Frame Begin
 ↓
PanelManager::render_all
 ↓
Console 输入
 ↓
CommandQueue::ExecuteAll
 ↓
ECS 更新
 ↓
Render
```

---
# 15. 关键设计原则

---

## 15.1 UI 无逻辑

```text
UI 只负责输入和展示
```

---
## 15.2 Command 解耦

```text
UI ≠ ECS
```

---
## 15.3 状态外置

```text
UIState = 单一数据源
```

---
## 15.4 Immediate Mode 特性

---

```text
UI 每帧重建
无持久对象
```

---
# 16. 常见错误

---

## 反模式：UI 直接修改 ECS

## 反模式：没有 Command 系统

## 反模式：状态散落

## 反模式：Panel 无管理

## 反模式：滥用 ImGui API

---
# 17. 最终架构总结

---

```text
Editor
 ├── UI（Panel）
 ├── State（Selection/UIState）
 ├── Command（行为）
 ├── Console（入口）
 └── ECS（数据）
```

---
# 🚀 最终总结

---

> **ImGui 只是工具
> Editor UI 是你自己构建的系统**

---

> **UI = 控制层
> Command = 行为层
> ECS = 数据层**

---
# 终极理解

---

现代编辑器本质是：

```text
数据驱动（ECS）
+
命令驱动（Command）
+
即时UI（ImGui）
```

---

[1]: https://docs.unity3d.com/cn/2018.3/Manual/GUIScriptingGuide.html "Immediate Mode GUI (IMGUI) - Unity Manual"
