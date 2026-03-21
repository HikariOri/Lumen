# RenderGraph 系统设计与开发文档（从 MVP 到工业级 · 超详细版）

---

# 1. 概述

RenderGraph 是现代图形引擎的核心系统，其本质是：

> **一个“帧级编译器”，将资源依赖描述编译为 GPU 执行计划**

---

## 1.1 为什么必须使用 RenderGraph

传统 Vulkan：

```text
手动顺序 + 手动 barrier + 手动 layout + 手动内存
```

问题：

* 同步极易出错（flicker / GPU hang）
* barrier 编写复杂且不可维护
* 内存利用率低

---

RenderGraph：

```text
声明依赖 → 自动生成执行计划
```

优势：

* 自动同步（Barrier）
* 自动资源生命周期
* 自动 layout 转换
* 自动内存优化（aliasing）

👉 实际项目验证：

* 同步 bug 从“频繁出现” → **0 bug** ([DEV Community][1])
* 显存降低 ~20%+ ([DEV Community][1])

---

## 1.2 RenderGraph 核心抽象

```text
Pass = 节点
Resource = 边
Graph = DAG（有向无环图）
```

👉 注意：

> 依赖不是“pass → pass”，而是“resource → pass” ([PORTA][2])

---

## 1.3 三阶段模型（必须严格遵守）

```text
1️⃣ Setup   → 描述依赖
2️⃣ Compile → 构建执行计划
3️⃣ Execute → 提交 GPU
```

👉 这是整个系统的“架构基石”

---

# 2. 阶段一：MVP RenderGraph（调度系统）

---

## 2.1 目标

建立：

* Pass 图
* 依赖关系
* 执行顺序

---

## 2.2 DAG 构建（核心）

规则：

```text
Pass A 写资源 → Pass B 读 → A → B
```

---

## 2.3 为什么必须 DAG

因为 GPU：

> **异步执行 + 无序执行**

如果不排序：

* 会读到未写完数据（RAW hazard）

---

## 2.4 拓扑排序本质

```text
找到一个合法执行序列，使所有依赖满足
```

---

## 2.5 工程注意点

❗ 必须检测环（cycle）
否则：

```text
A → B → C → A → 死循环
```

---

## 2.6 阶段输出

```text
Execution Order（执行顺序）
```

---

# 3. 阶段二：Builder / Context（声明式 API）

---

## 3.1 为什么必须 Builder

👉 防止用户：

```text
在执行阶段修改依赖（灾难）
```

---

## 3.2 设计原则

| 阶段    | 能做什么 |
| ------- | -------- |
| Builder | 描述依赖 |
| Context | 执行 GPU |

---

## 3.3 API 设计（工程标准）

```cpp
graph.AddPass("Lighting",
    [&](RGBuilder& b) {
        b.Read(gbuffer);
        b.Write(hdr);
    },
    [&](RGContext& ctx) {
        // Vulkan commands
    }
);
```

---

## 3.4 为什么要分离

👉 因为：

> Compile 阶段需要完整图信息

否则：

❌ 无法做优化
❌ 无法插 barrier

---

# 4. 阶段三：资源生命周期（核心基础）

---

## 4.1 定义

```text
first_use = 第一次使用
last_use  = 最后一次使用
```

---

## 4.2 本质

👉 构建：

```text
Frame Timeline（帧时间线）
```

---

## 4.3 为什么必须做

用于：

* Barrier 推导
* Memory aliasing
* Pass culling

---

## 4.4 算法细节

遍历拓扑排序结果：

```cpp
for pass in order:
    for resource in reads+writes:
        first = min
        last  = max
```

---

## 4.5 特殊情况

### 外部资源（swapchain）

```text
生命周期不由 graph 控制
```

👉 只能管理 layout，不做 aliasing ([Riccardo Loggini][3])

---

## 4.6 阶段输出

```text
Resource Timeline
```

---

# 5. 阶段四：Barrier 自动生成（同步系统）

---

## 5.1 GPU 同步本质

```text
写 → 必须可见 → 读
```

---

👉 Vulkan 要求：

* Stage（执行顺序）
* Access（内存访问） ([DEV Community][1])

---

## 5.2 Hazard 分类（必须掌握）

| 类型 | 说明   |
| ---- | ------ |
| RAW  | 写后读 |
| WAW  | 写后写 |
| WAR  | 读后写 |

---

## 5.3 Barrier 推导规则

```text
Write → Read  → flush + invalidate
Write → Write → serialize
Read → Write  → invalidate
Read → Read   → 无需
```

---

👉 RenderGraph 自动完成：

* flush（写完成）
* invalidate（读前刷新缓存） ([Tony Adriansen][4])

---

## 5.4 为什么 RenderGraph 能自动做

因为：

```text
它知道完整 usage 序列
```

---

## 5.5 工程实现关键点

### 1. usage 序列

```text
Resource:
    Pass0 Write
    Pass1 Read
    Pass2 Write
```

---

### 2. 相邻分析

```text
usage[i] → usage[i+1]
```

---

## 5.6 Vulkan 映射（必须正确）

示例：

```text
ColorAttachment → ShaderRead
```

对应：

```cpp
srcStage = COLOR_ATTACHMENT_OUTPUT
dstStage = FRAGMENT_SHADER

srcAccess = COLOR_ATTACHMENT_WRITE
dstAccess = SHADER_READ
```

---

## 5.7 常见错误（非常关键）

❌ stage 选错 → GPU stall
❌ access 漏写 → undefined behavior

---

# 6. 阶段五：Image Layout 系统

---

## 6.1 本质

```text
Layout = 内存访问语义
```

---

## 6.2 为什么必须管理

GPU：

> 根据 layout 做缓存 / tile 优化 ([Blender Developer][5])

---

## 6.3 设计思想（关键）

👉 不直接暴露 layout

👉 使用：

```text
Usage → Layout
```

---

## 6.4 自动转换流程

```text
usage[i] → usage[i+1]
```

如果 layout 不同：

```text
插入 transition
```

---

## 6.5 示例

```text
GBuffer:
Write(ColorAttachment)
→ Read(Sampled)
```

生成：

```text
COLOR_ATTACHMENT → SHADER_READ
```

---

## 6.6 工程难点

* layout 必须与 barrier 同步
* 不能重复 transition（性能问题）

---

## 6.7 阶段输出

```text
完整 resource state 轨迹
```

---

# 7. 阶段六：Resource Aliasing（显存复用）

---

## 7.1 本质

```text
生命周期不重叠 → 共享内存
```

---

## 7.2 为什么 RenderGraph 才能做

因为：

> 生命周期已知（compile 阶段） ([DEV Community][6])

---

## 7.3 算法本质

```text
Interval Packing（区间分配）
```

---

## 7.4 分配策略

### 贪心（推荐）

* 按 first_use 排序
* 找可复用 block

---

## 7.5 Vulkan 实现

```cpp
vkBindImageMemory(image, heap, offset);
```

---

## 7.6 Alias Barrier（关键）

当：

```text
A → B（共享内存）
```

必须：

```text
插入 barrier
```

---

## 7.7 性能收益

* 显存降低 20~50% ([Scribd][7])
* allocation 数量减少 90%+ ([DEV Community][1])

---

# 8. 阶段七：高级系统（工业级）

---

## 8.1 Pass Culling

删除：

```text
输出未被使用的 pass
```

---

## 8.2 Async Compute

```text
不同 dependency level 并行执行
```

👉 同一层可以并行 ([Scribd][7])

---

## 8.3 Pass Merging

👉 tile GPU 关键优化

---

## 8.4 多线程录制

RenderGraph 支持：

```text
多线程构建 + 单线程提交
```

([Blender Developer][5])

---

# 9. 完整编译流程（最终形态）

---

```text
Setup（用户声明）
↓
Build DAG
↓
Topological Sort
↓
Lifetime Analysis
↓
Barrier Generation
↓
Layout Tracking
↓
Memory Allocation（Aliasing）
↓
Pass Culling
↓
Execute（Record CommandBuffer）
```

---

# 10. 工程实现关键原则（非常重要）

---

## ✅ 正确做法

* 所有逻辑在 Compile 阶段完成
* Execute 阶段只 record command
* 资源必须“虚拟化”

---

## ❌ 错误做法

* 在 execute 阶段做决策
* 手写 barrier
* 手动管理 layout

---

# 11. RenderGraph 本质总结（终极理解）

---

## RenderGraph =

```text
资源依赖描述系统
+ 调度器
+ 同步生成器
+ 内存优化器
```

---

## 更高维理解

```text
它是 GPU 的“编译器”
```

---

将：

```text
声明式描述 → 编译为最优执行计划
```

---

# 🚀 最终一句话

> **RenderGraph 不是一个模块，而是整个渲染系统的核心执行模型**

[1]: https://dev.to/p3ngu1nzz/advanced-vulkan-rendering-building-a-modern-frame-graph-and-memory-management-system-15kn?utm_source=chatgpt.com "Advanced Vulkan Rendering: Building a Modern Frame Graph and Memory Management System - DEV Community"
[2]: https://dcgi.fel.cvut.cz/wp-content/wpallimport-dist/theses/pdf/theses-2020-galajrom-thesis.pdf?utm_source=chatgpt.com "Master Thesis"
[3]: https://logins.github.io/graphics/2021/05/31/RenderGraphs.html?utm_source=chatgpt.com "Render Graphs | Riccardo Loggini"
[4]: https://tadriansen.dev/2025-04-21-building-a-vulkan-render-graph/?utm_source=chatgpt.com "Building a Vulkan Render Graph | Tony Adriansen"
[5]: https://developer.blender.org/docs/features/gpu/vulkan/render_graph/?utm_source=chatgpt.com "Render graph - Blender Developer Documentation"
[6]: https://dev.to/p3ngu1nzz/inside-3-weeks-of-vulkan-engine-dev-render-graphs-descriptors-deterministic-frame-pacing-2nb2?utm_source=chatgpt.com "Inside 3 Weeks of Vulkan Engine Dev: Render Graphs, Descriptors & Deterministic Frame Pacing - DEV Community"
[7]: https://www.scribd.com/document/667157259/Unreal-Engine-Graphics-Rendering?utm_source=chatgpt.com "Unreal Engine Graphics & Rendering | PDF | Shader | Texture Mapping"
