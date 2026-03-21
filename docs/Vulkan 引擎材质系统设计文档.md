下面是把我们整个讨论整理成一份**可直接放进你引擎仓库的设计文档**（偏工程规范 + 架构说明）。我已经按“从基础 → 工业实现 → 优化 → 扩展”做了递进结构。

---

# Vulkan 引擎材质系统设计文档

---

# 1. 设计目标

本材质系统的目标是：

* 最小化 **Pipeline 数量**
* 最大化 **批处理（Batching）能力**
* 解耦 **Material / Pipeline / Descriptor**
* 支持 **RenderGraph 多 Pass**
* 可扩展到 **Bindless / GPU Driven**

---

# 2. Vulkan 核心抽象回顾

在 Vulkan 中：

* **Pipeline**：GPU 固定状态 + Shader
* **DescriptorSet**：资源绑定（buffer / texture）
* **Material参数**：uniform / 常量

👉 本质定义：

```text
Material = Pipeline + DescriptorSet + Parameters
```

---

## Descriptor 的本质

* Descriptor 是 GPU 资源的“句柄”
* DescriptorSet 是这些资源的集合
* Shader 通过 set + binding 访问资源 ([Khronos Group][1])

👉 重要限制：

* pipeline 最多绑定有限数量 descriptor set（通常 ≤4） ([Vulkan Guide][2])

---

# 3. 系统总体架构

---

## 3.1 分层结构

```text
Asset层：
    EffectTemplate（材质类型）
    Material（资源）

运行时：
    MaterialInstance（GPU就绪）

渲染层：
    RenderObject（扁平化数据）
```

---

## 3.2 数据流

```text
Material → MaterialInstance → RenderObject → DrawCall
```

---

# 4. 核心模块设计

---

# 4.1 EffectTemplate（材质类型）

定义：

```cpp
struct EffectTemplate {
    std::string name;

    ShaderModule* vertex;
    ShaderModule* fragment;

    VkPipelineLayout layout;
    std::vector<VkDescriptorSetLayout> setLayouts;

    std::unordered_map<RenderPassType, VkPipeline> pipelines;

    ShaderParameters defaultParams;
};
```

---

## 职责

* 定义 shader
* 定义 descriptor layout
* 管理 pipeline（按 pass）

---

## 关键设计

👉 一个 Effect = 多个 Pipeline（按 RenderPass）

例如：

```text
PBR：
    - Forward
    - Shadow
    - GBuffer
```

---

# 4.2 Material（资源层）

```cpp
struct Material {
    EffectTemplate* effect;

    std::vector<Texture*> textures;
    ShaderParameters params;

    std::unordered_map<RenderPassType, VkDescriptorSet> descriptorSets;
};
```

---

## 职责

* 持有纹理
* 持有参数
* 构建 descriptor set

---

## 重要原则

❗ Material **不拥有 pipeline**

---

# 4.3 MaterialInstance（运行时）

```cpp
struct MaterialInstance {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSet descriptorSet;
};
```

---

👉 渲染阶段只使用：

```text
pipeline + descriptor set
```

---

# 5. Descriptor 系统设计

---

## 5.1 Descriptor 分层（标准方案）

```text
set0 → global（camera / light）
set1 → pass（shadow / gbuffer）
set2 → material（纹理 / 参数）
set3 → object（transform）
```

---

## 设计依据

* descriptor set 数量有限 ([Vulkan Guide][2])
* 按“更新频率”分组性能最佳 ([Vulkan Guide][2])

---

## 5.2 Descriptor 生命周期

* 从 DescriptorPool 分配
* 写入资源（vkUpdateDescriptorSets）
* bind 后不可修改（否则 validation error） ([Vulkan Guide][2])

---

## 5.3 DescriptorBuilder

```cpp
class DescriptorBuilder {
public:
    DescriptorBuilder& bind_buffer(...);
    DescriptorBuilder& bind_image(...);

    bool build(VkDescriptorSet& set);
};
```

---

# 6. Material 构建流程

---

## 6.1 创建流程

```text
MaterialInfo → Material → DescriptorSet
```

---

## 6.2 示例

```cpp
DescriptorBuilder::begin(...)
    .bind_image(0, albedo)
    .bind_image(1, normal)
    .bind_buffer(2, paramUBO)
    .build(materialSet);
```

---

## 6.3 缓存（必须）

👉 材质系统必须缓存

原因：

* 多模型共享材质
* 避免 descriptor 重复创建

📌 实践中：

> 材质系统 heavily cached ([Vulkan Guide][3])

---

# 7. PipelineSystem（核心优化模块）

---

# 7.1 PipelineKey（关键）

```cpp
struct PipelineKey {
    ShaderID shader;
    RenderPassType pass;

    bool depthTest;
    bool depthWrite;
    BlendMode blend;

    VertexLayout layout;

    uint64_t hash() const;
};
```

---

## 本质

👉 PipelineKey = GPU状态唯一标识

---

# 7.2 PipelineSystem

```cpp
class PipelineSystem {
public:
    VkPipeline get_or_create(const PipelineKey& key);

private:
    std::unordered_map<uint64_t, VkPipeline> cache;
};
```

---

## 工作流程

```cpp
if (cache.contains(key))
    return cache[key];

pipeline = create_pipeline(key);

cache[key] = pipeline;
```

---

## 目的

* 避免 pipeline 重复创建
* 防止组合爆炸

---

# 7.3 Vulkan Pipeline Cache（补充）

区别：

| 类型            | 作用     |
| --------------- | -------- |
| 自定义 cache    | 去重     |
| VkPipelineCache | 加速创建 |

---

# 8. MaterialKey（资源去重）

---

## 设计

```cpp
struct MaterialKey {
    EffectTemplate* effect;

    TextureID albedo;
    TextureID normal;

    uint64_t paramHash;
};
```

---

## 作用

* 相同材质复用
* descriptor 复用
* 提升 batching

---

# 9. 渲染阶段设计

---

# 9.1 RenderObject（扁平化）

```cpp
struct RenderObject {
    VkPipeline pipeline;
    VkPipelineLayout layout;

    VkDescriptorSet materialSet;
    VkDescriptorSet objectSet;

    Mesh* mesh;
};
```

---

## 关键思想

👉 渲染时 **不使用 Material**

---

# 9.2 提取阶段

```cpp
obj.pipeline = pipelineSystem.get(key);
obj.materialSet = material.descriptor;
```

---

# 9.3 排序策略

```text
pipeline → material → mesh
```

---

## 效果

* 减少 pipeline bind
* 减少 descriptor bind
* 提升 GPU 利用率

---

# 9.4 Draw Call

```cpp
vkCmdBindPipeline(cmd, pipeline);

vkCmdBindDescriptorSets(
    cmd,
    layout,
    2,
    1,
    &materialSet,
    0, nullptr
);

vkCmdDraw(...);
```

---

# 10. 工程实践注意事项

---

## ⚠️ 1. Descriptor 不可修改

使用中修改 → validation error ([Vulkan Guide][2])

---

## ⚠️ 2. PipelineLayout 必须匹配

否则：

* descriptor slot 错误
* 渲染失败

---

## ⚠️ 3. 默认纹理（必须）

缺失纹理 → descriptor 未写 → 错误

---

## ⚠️ 4. Pipeline 数量控制

建议：

```text
< 10（理想）
< 50（可接受）
> 100（危险）
```

---

## ⚠️ 5. Descriptor Pool 管理

推荐：

* 每帧一个 pool
* frame 结束 reset

---

# 11. 扩展方向（未来架构）

---

## 11.1 Bindless Material

```text
texture → global array
material → index
```

优点：

* 减少 descriptor bind
* 支持 GPU Driven

---

## 11.2 GPU Driven Rendering

* indirect draw
* GPU culling

---

## 11.3 Material Graph

```text
节点图 → shader
```

---

## 11.4 Shader Reflection

自动生成：

* descriptor layout
* binding

---

# 12. 总结（核心认知）

---

## 一句话总结

```text
Material = 资源描述
Pipeline = 执行方式
Descriptor = 资源绑定
```

---

## 架构核心原则

👉 **最少 Pipeline + 合理 Descriptor 分层 + 最大化 Batch**

---

## 最终心智模型

```text
MaterialSystem：决定“用什么”
PipelineSystem：决定“怎么跑”
RenderSystem：决定“什么时候跑”
```

---

如果你后面哪天想继续升级，这一套可以自然演进到：

* Unreal 风格材质系统
* Bindless + GPU Driven
* 全自动 Shader 编译管线

而不需要推翻重写。

[1]: https://github.khronos.org/Vulkan-Site/spec/latest/chapters/descriptorsets.html?utm_source=chatgpt.com "Resource Descriptors :: Vulkan Documentation Project"
[2]: https://vkguide.dev/docs/chapter-4/descriptors/?utm_source=chatgpt.com "Descriptor Sets - Vulkan Guide"
[3]: https://vkguide.dev/docs/gpudriven/material_system/?utm_source=chatgpt.com "Material System - Vulkan Guide"
