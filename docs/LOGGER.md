# Lumen 日志系统

## 概述

Lumen 日志系统基于 spdlog，将**引擎内部日志**与**应用层日志**分离，支持控制台彩色输出与按大小轮转的文件输出。使用自封装的 `LogLevel`，不直接暴露 spdlog 类型。

## 架构

- **双 Logger**：`engine`（引擎内部）与 `app`（应用层）独立配置
- **单例模式**：`Logger` 采用 Meyers 单例，通过静态 `init()` / `shutdown()` 管理
- **谁创建谁销毁**：调用 `init()` 的代码必须调用 `shutdown()`

## 实现

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

## 使用

### 基本用法

```cpp
#include "core/logger.hpp"

int main() {
    // 默认配置（engine + app 均为 Debug 级别）
    if (!lumen::core::Logger::init()) {
        return -1;
    }

    LUMEN_APP_LOG_INFO("应用启动");
    LUMEN_APP_LOG_DEBUG("调试信息: {}", value);
    LUMEN_APP_LOG_ERROR("错误: {}", errorMsg);

    // ... 主逻辑 ...

    lumen::core::Logger::shutdown();
    return 0;
}
```

### 自定义配置

```cpp
lumen::core::LoggerConfig config;
config.engine.level = lumen::core::LogLevel::Info;   // 引擎日志级别
config.app.level = lumen::core::LogLevel::Warn;      // 应用日志级别
config.engine.filePath = "logs/my_engine.log";
config.app.filePath = "logs/my_app.log";
config.engine.maxFileSize = 10 * 1024 * 1024;        // 10MB

if (!lumen::core::Logger::init(config)) {
    return -1;
}
```

### 引擎内部使用

```cpp
// 在 engine 源码中
LUMEN_LOG_DEBUG("Vulkan 设备创建成功: {}", deviceName);
LUMEN_LOG_WARN("Validation layers 不可用");
LUMEN_LOG_ERROR("Swapchain 创建失败: {}", result);
```

## 注意事项

### 1. 生命周期

- **必须显式调用 `shutdown()`**：遵循“谁创建谁销毁”，在程序退出前调用
- **推荐结构**：将主逻辑放入独立函数，在 `main` 中统一 `init` → 主逻辑 → `shutdown`

```cpp
int main() {
    if (!lumen::core::Logger::init()) return -1;
    int result = run_application();
    lumen::core::Logger::shutdown();
    return result;
}
```

### 2. 引擎日志与 Release

- `LUMEN_LOG_*` 在 Release（`NDEBUG` 定义）下为空操作，无运行时开销
- `LUMEN_APP_LOG_*` 不受影响，始终输出

### 3. 初始化顺序

- `Logger::init()` 应在程序最早阶段调用，以便后续代码能正常打日志
- 若 `init()` 失败，`LUMEN_APP_LOG_*` 等宏会因 logger 未注册而静默不输出

### 4. 日志文件

- 默认路径：`logs/engine.log`、`logs/app.log`
- 路径相对于进程工作目录
- `maxFileSize > 0` 时使用 rotating sink，超过大小会轮转，最多保留 `maxFiles` 个文件

### 5. 线程安全

- 内部使用 spdlog 的 `_mt`（多线程）sink，多线程调用是安全的

### 6. 类型封装

- 对外仅使用 `LogLevel`，不直接依赖 `spdlog::level::level_enum`
- 更换日志库时只需修改 `Logger` 实现和 `detail::to_spdlog`

## 文件结构

```
engine/
├── include/core/logger.hpp   # 接口、配置、宏
└── src/core/logger.cpp       # 实现（sink 创建、spdlog 调用）
```
