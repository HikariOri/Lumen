# Scene & ECS 系统设计文档（从基础到工业级 · 递进版）

---

# 1. 概述

---

## 1.1 什么是 Scene

Scene 是：

```text
整个“世界状态”的容器
```

它包含：

* 所有实体（Entity）
* 所有组件（Component）
* 空间关系（Transform / Hierarchy）
* 系统（Systems）

---

👉 更严格定义：

> Scene 是对虚拟世界的完整表示，包括对象及其关系 ([Imstk][1])

---

---

## 1.2 Scene 的职责边界（必须明确）

---

❗ Scene 不负责：

* GPU 渲染
* Vulkan 同步
* RenderGraph

---

❗ Scene 只负责：

```text
世界数据 + 逻辑组织
```

---

---

## 1.3 Scene 在引擎中的位置

---

```text
Scene（CPU世界）
 ↓
Extract（提取）
 ↓
RenderGraph（调度）
 ↓
GPU
```

---

👉 核心原则：

> Scene ≠ Rendering
> Scene → 提供数据

---

---

# 2. Scene 的三种架构模型

---

# 2.1 Scene Graph（传统）

---

## 定义

> Scene Graph 是一种层级数据结构，用于组织3D对象 ([维基百科][2])

---

## 结构

```text
树结构（父 → 子）
```

---

## 特性

* 父节点变换影响子节点
* 自动空间传播
* 层级清晰

---

👉 示例：

```text
Car
 ├── Body
 ├── Wheel
```

---

---

## 优点

* 直观
* Transform 自动传播
* 易做编辑器

---

## 缺点

* 数据分散
* cache 不友好
* 不适合大规模系统

---

---

# 2.2 ECS（现代架构）

---

## 定义

> ECS 将对象拆分为 Entity + Component + System ([Aframe Kids Plates][3])

---

## 核心思想

```text
Entity = ID
Component = 数据
System = 行为
```

---

---

## 优点

* 高性能（连续内存）
* 易扩展
* 解耦

---

## 缺点

* 无层级关系
* Transform 传播困难

---

---

# 2.3 ECS + Scene Graph（工业标准）

---

## 定义

```text
ECS 管数据
SceneGraph 管空间
```

---

👉 实际引擎：

* Unity
* Unreal

---

## 原理

> ECS 负责数据存储，而 SceneGraph 提供层级结构 ([OpenReality][4])

---

---

# 3. 推荐架构（必须采用）

---

```text
Scene
 ├── ECS（核心数据）
 ├── Transform System（空间）
 ├── Hierarchy（层级）
 └── Systems（行为）
```

---

👉 核心思想：

# 👉 数据驱动 + 层级空间

---

---

# 4. ECS 系统设计（工程实现）

---

# 4.1 Entity（实体）

---

## 定义

```cpp
struct Entity {
    uint32_t id;
    uint32_t generation;
};
```

---

## 作用

* 唯一标识对象
* 不包含数据

---

👉 原则：

```text
Entity = 纯 ID
```

---

---

# 4.2 Component（组件）

---

## 定义

```cpp
struct Transform { ... };
struct Mesh { ... };
struct Material { ... };
```

---

👉 特点：

* 纯数据（POD）
* 不包含逻辑

---

---

# 4.3 Component Storage（核心）

---

## 目标

```text
连续内存 + O(1)访问
```

---

## 结构（SparseSet）

```text
dense → 连续数组
sparse → 查找表
```

---

👉 特性：

* O(1) 查找
* O(1) 删除（swap）
* cache-friendly

---

---

# 4.4 System（系统）

---

## 定义

```cpp
class System {
    void Update(Scene& scene);
};
```

---

👉 特点：

* 不持有数据
* 只操作组件

---

---

# 4.5 View（查询）

---

## 本质

```text
Component 集合交集
```

---

## 示例

```cpp
View<Transform, Mesh>
```

---

👉 遍历：

```text
只遍历有这些组件的实体
```

---

---

# 5. Transform & Hierarchy（空间系统）

---

# 5.1 为什么 ECS 不够

---

👉 ECS 无法表达：

```text
父子关系
```

---

👉 必须引入：

# 👉 Hierarchy

---

---

# 5.2 Hierarchy 组件

---

```cpp
struct Hierarchy {
    Entity parent;
    std::vector<Entity> children;
};
```

---

---

# 5.3 Transform 设计（关键）

---

```cpp
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    Mat4 local;
    Mat4 world;

    bool dirty;
};
```

---

👉 必须分：

```text
local（局部）
world（全局）
```

---

---

# 5.4 Transform 传播

---

## 原理

```text
world = parent.world * local
```

---

👉 SceneGraph 核心机制：

> 父节点变换会传播到子节点 ([Unity][5])

---

---

# 5.5 Dirty Flag（优化）

---

## 问题

```text
每帧全更新 → 性能浪费
```

---

## 解决

```text
只更新 dirty 节点
```

---

---

# 6. Scene → Rendering（关键桥梁）

---

# 6.1 核心原则

---

```text
Scene ≠ 渲染
必须 Extract
```

---

---

# 6.2 Render Extraction

---

## 定义

```text
从 ECS 提取渲染数据
```

---

---

## RenderObject

```cpp
struct RenderObject {
    Mat4 transform;
    MeshHandle mesh;
    MaterialHandle material;
};
```

---

---

## 提取流程

```text
遍历 ECS → 生成 RenderObject
```

---

---

# 7. Culling（性能基础）

---

# 7.1 目的

---

```text
减少不可见对象
```

---

---

# 7.2 方法

---

* Frustum Culling
* Hierarchy Culling

---

---

## SceneGraph 优势

---

```text
整棵子树裁剪
```

---

---

# 8. Batching（性能核心）

---

# 8.1 本质

---

```text
减少状态切换
```

---

---

# 8.2 关键思想

---

```text
按 Material 分组
```

---

---

# 8.3 数据结构

---

```cpp
struct Batch {
    Material material;
    Mesh mesh;
    std::vector<Mat4> transforms;
};
```

---

---

# 9. Scene → RenderGraph（完整流程）

---

```text
Scene（ECS）
 ↓
Transform Update
 ↓
Culling
 ↓
Batching
 ↓
Render Extraction
 ↓
RenderGraph
 ↓
GPU
```

---

---

# 10. 系统分层（工业级）

---

```text
Scene
 ├── EntityManager
 ├── ComponentStorage
 ├── TransformSystem
 ├── CullingSystem
 ├── RenderExtractionSystem
 ├── PhysicsSystem
 └── ScriptSystem
```

---

👉 Scene 是：

# 👉 系统容器（World）

---

---

# 11. 常见错误（必须避免）

---

## ❌ Scene 和 RenderGraph 混合

---

## ❌ Entity 持有逻辑

---

## ❌ Component 写函数

---

## ❌ 没有 Hierarchy

---

## ❌ 每帧 new/delete

---

---

# 12. 架构本质总结

---

## Scene

```text
世界数据容器
```

---

## ECS

```text
数据组织方式
```

---

## SceneGraph

```text
空间关系系统
```

---

## RenderGraph

```text
GPU执行系统
```

---

---

# 🚀 最终一句话总结

---

> **Scene = 世界
> ECS = 数据
> Hierarchy = 空间
> RenderGraph = 执行**

---

---

# 🔥 终极理解（非常关键）

---

> 现代引擎不是“对象驱动”，而是：

```text
数据驱动（ECS）
+
关系驱动（SceneGraph）
+
调度驱动（RenderGraph）
```

---

[1]: https://imstk.gitlab.io/Scene.html?utm_source=chatgpt.com "Scene & Object Model - imstk-documentation"
[2]: https://en.wikipedia.org/wiki/Scene_graph?utm_source=chatgpt.com "Scene graph"
[3]: https://aframe.kidsplates.jp/docs/introduction/entity-component-system.html?utm_source=chatgpt.com "エンティティコンポーネントシステム | A-FRAME"
[4]: https://open-reality.com/docs/architecture?utm_source=chatgpt.com "Architecture - OpenReality Docs"
[5]: https://unity.com/ja/glossary/scene-graph?utm_source=chatgpt.com "Scene Graph とは: Scene Graph の定義 | Unity"
