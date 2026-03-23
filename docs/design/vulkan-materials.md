# Vulkan 材质系统设计

本文描述在 **C++23 + Vulkan 1.4** 引擎中组织材质、管线与描述符的推荐做法，并与 **Scene / ECS、RenderGraph** 的分工对齐。代码示例仅表达结构意图，命名遵循仓库 [C++ 编码风格参考](../reference/cpp-style.md)。

---

## 1. 材质在 Vulkan 里解决什么问题

在 Vulkan 中，「材质」可拆为三部分：

1. **Pipeline**：固定功能状态 + 着色器阶段（GPU 状态机）。
2. **Descriptor 绑定**：buffer / image / sampler 如何连到 shader。
3. **参数与纹理**：每实例的 uniform、贴图槽位等。

可记为：

> Material ≈ Pipeline 状态 + DescriptorSet（及布局）+ 参数。

Descriptor 将 shader 资源与 pipeline layout 绑定，详见 [Khronos：Descriptor Sets](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)。

与 OpenGL 不同：上述对象均需显式创建、缓存与生命周期管理，不能在 draw 时「隐式改状态」。

---

## 2. 常见反模式

| 问题 | 后果 |
|------|------|
| 每个材质实例独占一条 `VkPipeline` | Pipeline 数量爆炸，切换开销大。 |
| 材质直接持有 shader、layout、descriptor 且强耦合 | 难以排序合批，后续 bindless / GPU-driven 需推倒重来。 |
| 把材质当成「一堆 float」，忽略 pipeline 变体 | 状态组合未纳入 key，易错绑或 validation 报错。 |
| Descriptor 布局与材质一一硬编码 | 扩展槽位困难，反射与工具链无法接入。 |

---

## 3. 推荐抽象（最小可用）

### 3.1 EffectTemplate（材质类型 / 着色模板）

表示一类可共享的 GPU 程序与布局，而不是某个具体贴图组合。

```cpp
struct EffectTemplate {
    std::string name;

    // ShaderModule* vertex;
    // ShaderModule* fragment;

    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    std::vector<VkDescriptorSetLayout> set_layouts;

    // 按 RenderPass / 子 Pass 缓存 pipeline（同 effect 多 pass 很常见）
    // std::unordered_map<RenderPassType, VkPipeline> pipelines;

    // ShaderParameters default_params;
};
```

要点：

- **Effect** 对应「Pipeline + Layout + Shader 接口」，与 [Vulkan Guide：材质与 GPU-driven](https://vkguide.dev/docs/gpudriven/material_system/) 中的思路一致。
- 同一套 PBR opaque / transparent 可对应少数几条 pipeline，而不是每个材质一条。

### 3.2 Material（资源层实例）

持有贴图引用、参数块，并指向某个 `EffectTemplate`。不应在热循环里参与「怎么画」的决策。

```cpp
struct Material {
    EffectTemplate* effect { nullptr };
    std::vector<Texture*> textures;
    // ShaderParameters params;
    // std::unordered_map<RenderPassType, VkDescriptorSet> descriptor_sets;
};
```

Material **不拥有** pipeline，只引用 effect；descriptor set 在此层创建与更新。

### 3.3 提交用扁平数据（RenderObject）

Draw 路径上应使用已解析好的句柄，避免每帧查表、遍历材质树：

```cpp
struct RenderObject {
    VkPipeline pipeline { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout { VK_NULL_HANDLE };
    VkDescriptorSet material_set { VK_NULL_HANDLE };
    // VertexBuffer*, IndexBuffer*, transform, ...
};
```

渲染时只依赖 **pipeline + descriptor set（+ buffer）**，与 vkguide 中「展开材质」的做法一致。

---

## 4. Descriptor Set 分层

按**更新频率**分 set 是工业界常规做法：

| Set | 典型内容 | 更新频率 |
|-----|----------|----------|
| 0 | 相机、全局灯光、环境 | 每帧或少次 |
| 1 | Pass 级（阴影、GBuffer） | 每 Pass |
| 2 | 材质（贴图、材质 UBO） | 每材质 |
| 3 | 物体（Transform、Object ID） | 每对象 |

Vulkan 对每 pipeline 可绑定的 set 数量有限（常见上限约 4），应按频率拆分，避免单 set 过大。参见 [Vulkan Guide：Descriptors](https://vkguide.dev/docs/chapter-4/descriptors/)。

可配合 **DescriptorBuilder** 一类辅助类，用代码或反射生成 layout，减少手写 binding 错误。

---

## 5. 排序与合批

提交前对 draw 项排序，减少状态切换：

```text
pipeline → material（或 descriptor）→ mesh
```

目标：尽量连续使用同一 pipeline、同一 material set，提高 GPU 利用率。

---

## 6. 默认资源

缺贴图时提供占位纹理（白贴图、默认法线、粗糙度 1.0 等），避免 descriptor 不完整导致未定义行为或校验层报错。

---

## 7. PipelineKey 与缓存

### 7.1 应用层 Pipeline 缓存

用稳定字段计算 key，在 `std::unordered_map` 中缓存 `VkPipeline`，避免重复创建：

```cpp
struct PipelineKey {
    // ShaderId shader;
    // RenderPassType pass;
    // bool depth_test;
    // bool depth_write;
    // BlendMode blend;
    // VertexLayout layout;

    std::uint64_t hash() const;
};

class PipelineSystem {
public:
    VkPipeline get_or_create(const PipelineKey& key);

private:
    std::unordered_map<std::uint64_t, VkPipeline> cache_;
};
```

`PipelineKey` 表达「GPU 状态的唯一标识」；相同 key 必须对应可复用的 pipeline。

### 7.2 `VkPipelineCache`

`vkCreateGraphicsPipelines` 时传入 `VkPipelineCache`，可加速后续创建并可将 blob 落盘，跨运行复用。参见 [VkPipelineCache](https://registry.khronos.org/vulkan/specs/latest/man/html/VkPipelineCache.html)。  
注意：这是 **驱动级** 缓存，与上一节的 **逻辑 key → pipeline** 缓存互补，不能互相替代。

### 7.3 MaterialKey（可选）

当多个 `Material` 实际上共享相同 effect + 贴图组合时，可用 `MaterialKey` 去重实例与 descriptor，减少重复分配。材质系统整体应保持 **heavily cached** 的取向。

---

## 8. 与 Scene、RenderGraph 的边界

- **Scene / ECS**：存储实体、组件、层级与材质资产引用；不做 GPU 同步与 barrier。
- **Extract 阶段**：从 Scene 生成 `RenderObject` 列表（或等价结构）。
- **RenderGraph**：负责 pass 依赖、资源生命周期与同步；不反向修改 Scene 拓扑。

---

## 9. 演进路线（简表）

| 阶段 | 内容 |
|------|------|
| 基础 | Effect + Material + 分层 descriptor；按 key 缓存 pipeline。 |
| 进阶 | Shader reflection 生成 layout；材质变体与关键字系统。 |
| 高级 | Bindless、GPU-driven、bindless 贴图数组与可扩展着色器变体。 |

---

## 10. 参考

- 本仓库 **Demo3D 当前材质与双 Set 实现**：[demo3d-pbr-material-system.md](demo3d-pbr-material-system.md)
- **Demo3D 分阶段规划与面板演进**：[material-system-ibl-pbr.md](material-system-ibl-pbr.md)
- [Vulkan Guide：Material / GPU-driven](https://vkguide.dev/docs/gpudriven/material_system/)
- [Vulkan Guide：Descriptors](https://vkguide.dev/docs/chapter-4/descriptors/)
- [Khronos：Vulkan Specification — Descriptors](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html)
