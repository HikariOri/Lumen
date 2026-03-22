# 日志系统（实现说明 + 设计路线）

排版与文中 C++ 片段遵循 [中文文案排版指北](../guides/chinese-typography.md) 与 [C++ 编码风格参考](cpp-style.md)。

## 当前实现（仓库）

Lumen 日志系统基于 spdlog，将**引擎内部日志**与**应用层日志**分离，支持控制台彩色输出与按大小轮转的文件输出。使用自封装的 `LogLevel`，不直接暴露 spdlog 类型。

### 架构

- **双 Logger**：`engine`（引擎内部）与 `app`（应用层）独立配置
- **单例模式**：`Logger` 采用 Meyers 单例，通过静态 `init()` / `shutdown()` 管理
- **谁创建谁销毁**：调用 `init()` 的代码必须调用 `shutdown()`

### 核心类型

| 类型 | 说明 |
|------|------|
| `LogLevel` | 日志级别：`Trace`、`Debug`、`Info`、`Warn`、`Error`、`Critical` |
| `LoggerPathConfig` | 单个 logger 配置：级别、启用、文件路径、大小限制 |
| `LoggerConfig` | 整体配置：控制台、engine、app 的 `LoggerPathConfig` |

### 宏分类

| 宏族 | 用途 | Debug 模式 | Release 模式 |
|------|------|------------|--------------|
| `LUMEN_LOG_*` | 引擎内部（render、platform 等） | 输出 | 空操作 `((void)0)` |
| `LUMEN_APP_LOG_*` | 应用层 | 输出 | 输出 |

### 输出格式

- **控制台**：`[时间] [logger名] [级别] [文件:行号] 消息`
- **文件**：同控制台，无颜色
- 支持 spdlog 占位符：`{}`、`{:.2f}` 等

### 使用示例

```cpp
#include "core/logger.hpp"

int main() {
    if (!lumen::core::Logger::init()) {
        return -1;
    }

    LUMEN_APP_LOG_INFO("应用启动");
    LUMEN_APP_LOG_DEBUG("调试信息: {}", value);
    LUMEN_APP_LOG_ERROR("错误: {}", errorMsg);

    lumen::core::Logger::shutdown();
    return 0;
}
```

```cpp
lumen::core::LoggerConfig config;
config.engine.level = lumen::core::LogLevel::Info;
config.app.level = lumen::core::LogLevel::Warn;
config.engine.filePath = "logs/my_engine.log";
config.app.filePath = "logs/my_app.log";
config.engine.maxFileSize = 10 * 1024 * 1024;

if (!lumen::core::Logger::init(config)) {
    return -1;
}
```

引擎源码内使用 `LUMEN_LOG_*`。

### 注意事项

- **生命周期**：程序退出前必须 `shutdown()`；推荐在 `main` 中 `init` → `run` → `shutdown`。
- **Release**：`LUMEN_LOG_*` 在 `NDEBUG` 下为空操作；`LUMEN_APP_LOG_*` 始终保留。
- **初始化顺序**：尽早 `init()`，否则宏可能静默无输出。
- **文件**：默认 `logs/engine.log`、`logs/app.log`；`maxFileSize > 0` 时轮转。
- **线程安全**：内部使用 spdlog 多线程 sink。

### 文件结构

```
engine/
├── include/core/logger.hpp
└── src/core/logger.cpp
```

---

## 设计与扩展路线（目标能力摘要）

以下条目来自原《引擎日志系统设计文档》中的**目标架构**，用于规划；**是否已实现请对照上文与源码**，未落地的能力在实施前需单独开任务与验收。

| 方向 | 说明 |
|------|------|
| 多 Sink | 除控制台、文件外，可扩展 ImGui 视窗、网络等 |
| 异步日志 | 主线程入队、后台 worker 写 IO；队列策略如 `overrun_oldest` |
| Tag / 分类 | 按模块、Pass 名等结构化过滤 |
| GPU / 调试 | Validation、Debug Utils Label 与日志联动 |
| 崩溃诊断 | backtrace、信号处理、安全 `shutdown` |
| 远程与控制台 | 运行时改等级、与 Debug Console / 指令系统集成 |

### 原则（DO / DON'T）

- **DO**：异步或低开销输出、控制等级、带上下文、分类清晰。
- **DON'T**：在渲染热路径刷屏、依赖日志做 profiling、同步阻塞 IO、等级滥用。

### 常见反模式

避免：无意义高频日志、缺少上下文、热路径每帧打印、`printf` 与统一日志混用、无轮转导致磁盘打满等。
