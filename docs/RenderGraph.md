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
    void addPass(const RGPass& pass);
    void execute(VkCommandBuffer cmd);  // 拓扑排序 + 自动 barrier + 执行
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
| 离屏渲染 (Scene) | ❌ | 渲染到自有 VkImage |
| ImGui 绘制 | ✅ | 最终画到 Swapchain 上 |
| Present | ✅ | 必须用 Swapchain |

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

```text
Pass 1 (Scene)：
  - writes: sceneTarget.color_view(), sceneTarget.depth
  - framebuffer: sceneTarget.framebuffer()
  - renderPass: sceneTarget.render_pass()

Pass 2 (UI)：
  - reads:  sceneTarget.color_view() → ImGui::Image 采样
  - writes: swapchain 当前帧 framebuffer
  - renderPass: 主屏 RenderPass（Clear + ImGui）
```

### 7.3 当前实现的 Layout 处理

- **RenderPass 内**：Vulkan 通过 attachment 的 `initialLayout` / `finalLayout` 自动处理
- **跨 Pass**：Scene 的 `colorFinalLayout = SHADER_READ_ONLY_OPTIMAL`，供 ImGui 采样
- **显式 RenderGraph**：当前未实现，仍为手写顺序 + RenderPass 声明式 layout

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
| 拓扑排序 | 多 Pass 时自动推导 DAG 顺序 |
| 资源复用 | 多 Image 共用显存，减少峰值 |
| Compute Pass | 支持 Graphics + Compute 混合 |
| 多帧并发 | frame-in-flight 下的资源生命周期 |

---

## 十、工程建议

### 10.1 当前阶段推荐

```text
✔ 明确 read/write 的 Pass 划分
✔ 离屏与 Swapchain 的职责分离（OffscreenRenderTarget / SwapchainRenderTarget）
✔ RenderPass 内用 attachment layout 声明式管理
```

### 10.2 暂不实现

```text
✘ 完整 DAG 自动排序
✘ 自动 Barrier 生成
✘ 内存复用 / Aliasing
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

- `engine/include/render/pass/render_target.hpp`：OffscreenRenderTarget / SwapchainRenderTarget
- `examples/demo3d/src/main.cpp`：Scene → Offscreen → ImGui → Swapchain 实际流程
- [RENDER_ENGINE_PLAN.md](RENDER_ENGINE_PLAN.md)：完整管线设计
