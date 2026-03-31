# Vulkan RenderGraph 设计与原理

> 用「资源依赖关系」描述渲染流程，由依赖自动推导执行顺序与同步，而非手写 barrier 与 layout 转换。

---

## 一、核心概念

### 1.1 定义

**RenderGraph = 用资源读写依赖描述整个渲染流程的系统**

- **不再手写**：执行顺序、PipelineBarrier、Image Layout 转换
- **改为声明**：每个 Pass 读哪些资源、写哪些资源
- **系统推导**：根据依赖关系自动排序、插入同步、管理 layout

### 1.2 本质：DAG（有向无环图）

```
节点 (Node)  = Pass（渲染节点）
边 (Edge)    = 资源依赖（读/写关系）
```

```
[Scene] ──写──→ [Offscreen] ──读──→ [ImGui] ──写──→ [Swapchain] ──→ Present
   │                │                   │
 写Color         写Color              写Swapchain
 写Depth         写Depth              读Offscreen
```

---

## 二、传统 Vulkan vs RenderGraph

### 2.1 传统写法

```text
Pass1 → vkCmdPipelineBarrier → Pass2 → barrier → Pass3 → Present
```

| 问题 | 说明 |
|------|------|
| 顺序写死 | 改流程要动大量代码 |
| barrier 手动维护 | 容易漏、难维护 |
| layout 手动转换 | 每个 attachment 的 initial/final 都要算清楚 |
| 依赖关系隐式 | 新人难以理解「为什么这里要 barrier」 |

### 2.2 RenderGraph 写法

```text
ScenePass  写 → OffscreenImage
UIPass     读 → OffscreenImage，写 → SwapchainImage
```

系统自动推导：Pass 顺序、Barrier 插入位置、Layout 转换。

### 2.3 对比表

| 特性 | 传统 Vulkan | RenderGraph |
|------|-------------|-------------|
| 执行顺序 | 手写 | 自动拓扑排序 |
| PipelineBarrier | 手动插入 | 按读写依赖自动生成 |
| Image Layout | 手动 transition | 按用途自动转换 |
| 扩展性 | 差 | 增删 Pass 不改核心逻辑 |
| 心智负担 | 高 | 声明依赖即可 |

---

## 三、核心价值

### 3.1 自动执行顺序

根据资源依赖做拓扑排序：谁依赖谁 → 自动决定执行顺序。

### 3.2 自动同步（核心价值）

```cpp
// 传统：每个 read-after-write 都要手写
vkCmdPipelineBarrier(cmd, ...);

// RenderGraph：声明 reads / writes，系统自动插入
```

### 3.3 自动 Layout 转换

| 用途 | Layout |
|------|--------|
| 作为颜色附件写入 | `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` |
| 作为纹理采样 | `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` |
| 作为深度附件 | `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` |
| Present | `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` |

系统根据 Pass 的 reads/writes 自动插入 transition。

### 3.4 资源生命周期

- 创建时机：第一次被写入前
- 销毁时机：所有消费者执行完后（或帧末）

### 3.5 进阶：内存复用（Aliasing）

多个 Image 在不同时段共用同一块显存，减少占用。

---

## 四、简化设计结构

### 4.1 资源 (Resource)

```cpp
struct RGImage {
    VkImage        image;
    VkImageView    view;
    VkFormat       format;
    VkExtent2D     extent;
    VkImageLayout  currentLayout;
    bool           isDepth;  // 颜色 vs 深度
};
```

### 4.2 Pass（渲染节点）

```cpp
struct RGPass {
    std::string name;

    std::vector<RGImage*> reads;   // 输入：采样、只读
    std::vector<RGImage*> writes;  // 输出：渲染目标

    std::function<void(VkCommandBuffer)> execute;
};
```

### 4.3 RenderGraph

```cpp
class RenderGraph {
public:
    void add_pass(const RGPass& pass);
    bool compile();   // DAG 拓扑排序；有环则失败
    void execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex);
};
```

---

## 五、自动 Layout Transition 示例

```cpp
void transitionImage(VkCommandBuffer cmd, RGImage* img, VkImageLayout newLayout) {
    if (img->currentLayout == newLayout) return;

    VkImageMemoryBarrier barrier{};
    barrier.oldLayout = img->currentLayout;
    barrier.newLayout = newLayout;
    // ... 其他字段
    vkCmdPipelineBarrier(...);

    img->currentLayout = newLayout;
}

// 执行 Pass 前
for (auto* img : pass.reads)
    transitionImage(cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
for (auto* img : pass.writes)
    transitionImage(cmd, img, isDepth ? DEPTH_ATTACHMENT : COLOR_ATTACHMENT);
```

---

## 六、典型流程：ImGui Viewport

### 6.1 数据流

```text
Scene Pass    写 → OffscreenImage (Color + Depth)
                    │
                    ▼
UI Pass       读 OffscreenImage (ImGui::Image 采样)
               写 → SwapchainImage (Clear + ImGui 绘制)
                    │
                    ▼
Present        Swapchain → 屏幕
```

### 6.2 关键认知

| 阶段 | 是否需要 Swapchain | 说明 |
|------|-------------------|------|
| 离屏渲染 (Scene) | 否 | 渲染到自有 `VkImage` |
| ImGui 绘制 | 是 | 最终画到 Swapchain 上 |
| Present | 是 | 必须用 Swapchain |

**离屏渲染不依赖 Swapchain，但显示到屏幕必须用 Swapchain。**

### 6.3 Pass 声明示例

```cpp
// Scene Pass
scenePass.writes = { &offscreenColor, &offscreenDepth };

// UI Pass（ImGui 内部采样 offscreenColor，输出到 swapchain）
uiPass.reads  = { &offscreenColor };
uiPass.writes = { &swapchainImage };
```

---

## 七、与当前引擎实现对应

### 7.1 当前 demo3d 流程

| 阶段 | 引擎类型 | 作用 |
|------|----------|------|
| 离屏 | `OffscreenRenderTarget` | 3D 场景渲染到纹理，含 color + depth |
| 显示 | `Swapchain` + `Framebuffer` | ImGui 绘制到 Swapchain，含 Scene 视口采样 |
| 同步 | `FrameSync` | 每帧 Fence/Semaphore |

### 7.2 代码映射

引擎侧实现见 `engine/include/render/pass/render_graph.hpp`、`render_graph.cpp`。demo3d 中用 `RGImage::from_texture` / `from_swapchain` 绑定资源，再用 `RenderGraph::add_pass` 声明依赖。

```text
Pass Scene（示例）：
  - writes: rgColor / rgDepth（来自 OffscreenRenderTarget 的 Image）
  - framebuffer / renderPass：仍由 Pass 的 execute 回调内部绑定（与 RG 声明一致即可）

Pass UI（示例）：
  - reads:  rgColor → ImGui 视口采样
  - writes: rgSwapchain → 主屏 framebuffer
```

### 7.3 Layout 与同步（当前实现）

- **三阶段**：`add_pass`（Setup）→ `compile()`（拓扑排序，有环则失败、不执行）→ `execute()`（按编译顺序插入 `vkCmdPipelineBarrier` 与 layout 跟踪，再调用各 Pass 的 `execute`）。`execute` 在编译过期时会自动调用 `compile()`。
- **RenderPass 内**：各 `OffscreenRenderTarget` / 主屏 RenderPass 仍通过附件的 `initialLayout` / `finalLayout` 处理子通道内转换；离屏颜色默认 `colorFinalLayout = SHADER_READ_ONLY_OPTIMAL`，与 RenderGraph 在 Pass 结束后对颜色纹理跟踪的 layout 一致。
- **跨 Pass**：由 RenderGraph 在 Pass 前将 `reads` 迁到 `SHADER_READ_ONLY_OPTIMAL`，将 `writes` 迁到 color / depth attachment layout；Swapchain 从 `PRESENT_SRC_KHR` 再次写入时会插入合法 barrier。未实现：Resource Aliasing、异步计算、多线程编译与提交。

---

## 八、类比：构建系统

RenderGraph 类似 Makefile：

```text
Makefile：  a.exe 依赖 a.obj，a.obj 依赖 a.cpp
RenderGraph：UIPass 依赖 OffscreenImage，OffscreenImage 依赖 ScenePass
```

依赖关系决定执行顺序，而非手写顺序。

---

## 九、进阶方向

| 方向 | 说明 |
|------|------|
| Builder / Context API | 文档「阶段二」：编译期与执行期分离，避免执行阶段改依赖 |
| 资源时间线 / Pass Culling | 显式 `first_use` / `last_use`，未使用 Pass 剔除 |
| 资源复用 | 多 Image 共用显存（Aliasing） |
| Compute Pass | Graphics + Compute 混合与队列同步 |
| 多帧并发 | frame-in-flight 下的资源生命周期与调度 |

---

## 十、工程建议

### 10.1 当前阶段推荐

```text
- 明确 read/write 的 Pass 划分，与 execute 内实际 framebuffer 附件一致
- 离屏与 Swapchain 的职责分离（OffscreenRenderTarget / SwapchainRenderTarget）
- RenderPass 内继续用 attachment 的 initial/finalLayout；跨 Pass 交给 RenderGraph
```

### 10.2 暂不实现（相对当前代码）

```text
- Builder 式声明 API、资源时间线与 Pass Culling
- 内存复用 / Resource Aliasing
- 异步计算与多线程 RenderGraph 编译
```

---

## 十一、总结

| 要点 | 说明 |
|------|------|
| 本质 | 用资源依赖驱动渲染，而非代码顺序 |
| Vulkan | 手动管理 GPU 状态机 |
| RenderGraph | 声明依赖，自动管理状态机 |
| 离屏 vs 显示 | 离屏不依赖 Swapchain，显示必须用 Swapchain |

---

## 参考

- `engine/include/render/pass/render_graph.hpp`：RenderGraph / RGPass / RGImage
- `engine/include/render/pass/render_target.hpp`：OffscreenRenderTarget / SwapchainRenderTarget
- [offscreen-render-target-roadmap.md](offscreen-render-target-roadmap.md)：离屏目标共享 `RenderPass`、与 RenderGraph 深度整合等**远期路线**
- `examples/demo3d/src/main.cpp`：Scene → Offscreen → ImGui → Swapchain 实际流程
- [render-engine-roadmap.md](render-engine-roadmap.md)：完整管线设计


---

# 系统设计与开发路线（MVP 到工业级）

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

实际项目验证：

* 同步 bug 从“频繁出现” → **0 bug** ([DEV Community][1])
* 显存降低 ~20%+ ([DEV Community][1])

---

## 1.2 RenderGraph 核心抽象

```text
Pass = 节点
Resource = 边
Graph = DAG（有向无环图）
```

注意：

> 依赖不是“pass → pass”，而是“resource → pass” ([PORTA][2])

---

## 1.3 三阶段模型（必须严格遵守）

```text
1️⃣ Setup   → 描述依赖
2️⃣ Compile → 构建执行计划
3️⃣ Execute → 提交 GPU
```

这是整个系统的“架构基石”

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

**注意：** 必须检测环（cycle）
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

防止用户：

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

因为：

> Compile 阶段需要完整图信息

否则：

- 无法做优化
- 无法插 barrier

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

构建：

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

只能管理 layout，不做 aliasing ([Riccardo Loggini][3])

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

Vulkan 要求：

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

RenderGraph 自动完成：

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

- stage 选错 → GPU stall
- access 漏写 → undefined behavior

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

不直接暴露 layout

使用：

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

同一层可以并行 ([Scribd][7])

---

## 8.3 Pass Merging

tile GPU 关键优化

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

## 推荐做法

* 所有逻辑在 Compile 阶段完成
* Execute 阶段只 record command
* 资源必须“虚拟化”

---

## 不推荐做法

* 在 execute 阶段做决策
* 手写 barrier
* 手动管理 layout

---

# 11. 本质小结

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

## 核心结论

> **RenderGraph 不是一个模块，而是整个渲染系统的核心执行模型**

[1]: https://dev.to/p3ngu1nzz/advanced-vulkan-rendering-building-a-modern-frame-graph-and-memory-management-system-15kn "Advanced Vulkan Rendering: Building a Modern Frame Graph and Memory Management System - DEV Community"
[2]: https://dcgi.fel.cvut.cz/wp-content/wpallimport-dist/theses/pdf/theses-2020-galajrom-thesis.pdf "Master Thesis"
[3]: https://logins.github.io/graphics/2021/05/31/RenderGraphs.html "Render Graphs | Riccardo Loggini"
[4]: https://tadriansen.dev/2025-04-21-building-a-vulkan-render-graph/ "Building a Vulkan Render Graph | Tony Adriansen"
[5]: https://developer.blender.org/docs/features/gpu/vulkan/render_graph/ "Render graph - Blender Developer Documentation"
[6]: https://dev.to/p3ngu1nzz/inside-3-weeks-of-vulkan-engine-dev-render-graphs-descriptors-deterministic-frame-pacing-2nb2 "Inside 3 Weeks of Vulkan Engine Dev: Render Graphs, Descriptors & Deterministic Frame Pacing - DEV Community"
[7]: https://www.scribd.com/document/667157259/Unreal-Engine-Graphics-Rendering "Unreal Engine Graphics & Rendering | PDF | Shader | Texture Mapping"

