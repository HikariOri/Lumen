# Vulkan RenderGraph 设计与原理（整理文档）

---

# 一、概念定义

**RenderGraph = 用“资源依赖关系”描述整个渲染流程的系统**

它的核心思想是：

* 不再手写执行顺序
* 不再手动管理同步与 layout
* 而是通过“数据读写关系”自动推导执行流程

---

# 二、直观理解

## 传统 Vulkan 写法

```text
Pass1 → barrier → Pass2 → barrier → Pass3
```

问题：

* 顺序写死
* barrier 手动维护
* 依赖关系隐式
* 难以扩展

---

## RenderGraph 写法

```text
ScenePass  写 → ColorImage
PostPass   读 → ColorImage，写 → PostImage
UIPass     读 → PostImage，写 → Swapchain
```

系统自动推导：

```text
Scene → Post → UI
```

---

# 三、本质：DAG（有向无环图）

RenderGraph 本质是一个 DAG：

* 节点（Node）= Pass
* 边（Edge）= 资源依赖

示意：

```text
[Scene] → [Post] → [UI]
   │         │        │
写Color   写Post   写Swapchain
```

---

# 四、解决的问题

## 1. 自动执行顺序

根据资源依赖自动排序：

```text
谁依赖谁 → 自动排序
```

---

## 2. 自动同步（核心价值）

无需手写：

```cpp
vkCmdPipelineBarrier(...)
```

系统根据 read/write 自动生成同步

---

## 3. 自动 Image Layout 转换

例如：

```text
COLOR_ATTACHMENT → SHADER_READ_ONLY
```

自动完成

---

## 4. 资源生命周期管理

自动处理：

* 创建时机
* 销毁时机

---

## 5. 内存优化（进阶）

支持资源复用（aliasing）：

```text
多个 image 共用显存
```

---

# 五、与传统 Vulkan 对比

| 特性      | 传统 Vulkan | RenderGraph |
| ------- | --------- | ----------- |
| 执行顺序    | 手写        | 自动推导        |
| barrier | 手动        | 自动          |
| layout  | 手动        | 自动          |
| 扩展性     | 差         | 强           |
| 复杂度     | 高         | 可控          |

---

# 六、核心设计结构（简化版）

## 1. Resource（资源）

```cpp
struct RGImage {
    VkImage image;
    VkImageView view;
    VkFormat format;
    VkExtent2D extent;
    VkImageLayout currentLayout;
};
```

---

## 2. Pass（渲染节点）

```cpp
struct RGPass {
    std::string name;

    std::vector<RGImage*> reads;
    std::vector<RGImage*> writes;

    std::function<void(VkCommandBuffer)> execute;
};
```

---

## 3. RenderGraph

```cpp
class RenderGraph {
public:
    std::vector<RGPass> passes;

    void addPass(const RGPass& pass);
    void execute(VkCommandBuffer cmd);
};
```

---

# 七、核心机制：自动 Layout Transition

```cpp
void transitionImage(
    VkCommandBuffer cmd,
    RGImage* img,
    VkImageLayout newLayout)
{
    if (img->currentLayout == newLayout)
        return;

    VkImageMemoryBarrier barrier{};

    barrier.oldLayout = img->currentLayout;
    barrier.newLayout = newLayout;

    vkCmdPipelineBarrier(...);

    img->currentLayout = newLayout;
}
```

---

# 八、执行流程

```cpp
void RenderGraph::execute(VkCommandBuffer cmd) {
    for (auto& pass : passes) {

        // 读资源 → shader read
        for (auto* img : pass.reads) {
            transitionImage(cmd, img,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // 写资源 → color attachment
        for (auto* img : pass.writes) {
            transitionImage(cmd, img,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        pass.execute(cmd);
    }
}
```

---

# 九、典型使用场景（ImGui）

## 渲染流程

```text
Scene → Offscreen → ImGui → Swapchain → Screen
```

---

## 示例

### Scene Pass

```cpp
scenePass.writes = { &offscreen };
```

---

### ImGui Pass

```cpp
imguiPass.reads  = { &offscreen };
imguiPass.writes = { &swapchain };
```

---

# 十、关键思想总结

## 1. Pass = 输入 + 输出

```text
reads  → 输入资源
writes → 输出资源
```

---

## 2. Graph = 依赖关系

```text
资源流动决定执行顺序
```

---

## 3. 不再依赖代码顺序

```text
顺序 = 推导结果，而不是手写
```

---

# 十一、类比理解

RenderGraph 类似构建系统（如 Makefile）：

```text
编译系统：
a.o depends on a.cpp

RenderGraph：
PostPass depends on ColorImage
```

---

# 十二、进阶方向

## 1. 自动拓扑排序（DAG）

不再手动安排 pass 顺序

---

## 2. 资源复用（aliasing）

减少显存占用

---

## 3. 多类型 Pass

* graphics
* compute

---

## 4. 多帧资源管理

支持多 frame-in-flight

---

# 十三、工程建议

当前阶段推荐实现：

```text
✔ 简单顺序执行 RenderGraph
✔ 自动 layout 管理
✔ 明确 read/write 关系
```

暂不需要：

```text
✘ 完整 DAG
✘ 内存复用
✘ 复杂调度
```

---

# 十四、一句话总结

**RenderGraph = 用“资源依赖”驱动渲染，而不是用“代码顺序”驱动渲染**

---

# 十五、核心认知提升

* Vulkan：手动管理 GPU 状态机
* RenderGraph：自动管理 GPU 状态机

---

（完）
