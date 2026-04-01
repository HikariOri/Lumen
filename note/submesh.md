---

# 🧠 一、先统一概念（非常关键）

---

## 🔑 SubMesh / Primitive 的本质

> 👉 **SubMesh = 一个“可单独渲染的几何片段（+材质）”** ([forums.ogre3d.org][1])

---

## 🔑 引擎里的对应关系

```text
glTF Primitive  ≈  SubMesh
```

---

## 🔑 ECS里的目标

> 👉 **让“每个 SubMesh 可以被独立控制（transform / material / visibility）”**

---

# 🚀 二、推荐设计（直接用这个）

---

# 🧱 方案：SubMesh Entity（标准做法）

---

## ✅ 1. 组件定义

---

### 🔹 SubMeshRenderer（核心）

```cpp
struct SubMeshRenderer
{
    const Mesh* mesh;
    uint32_t primitiveIndex; // 👈 关键

    const Material* materialOverride = nullptr; // 可选
};
```

---

👉 含义：

```text
这个 entity = mesh 的某一个 primitive
```

---

---

## ✅ 2. Transform（每个都有）

```cpp
struct Transform
{
    glm::mat4 world;
};
```

---

---

## ✅ 3. 层级关系（重要）

```cpp
struct Parent { entt::entity parent; };
struct Children { std::vector<entt::entity> children; };
```

---

---

# 🌳 三、Entity 结构（核心）

---

## 👉 一个 glTF Node

```text
Node（Entity）
 ├── Transform
 ├── Children
 │
 ├── SubEntity 0（Primitive 0）
 │    ├── Transform（local = identity）
 │    └── SubMeshRenderer
 │
 ├── SubEntity 1（Primitive 1）
 │    └── SubMeshRenderer
```

---

👉 注意：

> ❗ **SubEntity 是 Node 的子节点**

---

---

# 🔥 四、构建流程（你 loader 该怎么写）

---

## Step 1：创建 Node Entity

```cpp
auto nodeEntity = registry.create();
registry.emplace<Transform>(nodeEntity, nodeTransform);
```

---

---

## Step 2：为每个 primitive 创建子 entity

```cpp
for (uint32_t i = 0; i < mesh.primitives.size(); ++i)
{
    auto sub = registry.create();

    // 绑定父子关系
    registry.emplace<Parent>(sub, nodeEntity);
    registry.get<Children>(nodeEntity).children.push_back(sub);

    // transform（默认 identity）
    registry.emplace<Transform>(sub, identity);

    // 👇 核心
    SubMeshRenderer r;
    r.mesh = &mesh;
    r.primitiveIndex = i;

    registry.emplace<SubMeshRenderer>(sub, r);
}
```

---

---

# 🎬 五、渲染系统（非常干净）

---

```cpp
auto view = registry.view<Transform, SubMeshRenderer>();

for (auto e : view)
{
    auto& t = view.get<Transform>(e);
    auto& r = view.get<SubMeshRenderer>(e);

    auto& prim = r.mesh->primitives[r.primitiveIndex];

    const Material* mat = r.materialOverride ? r.materialOverride
                                             : prim.material;

    Draw(prim, mat, t.world);
}
```

---

---

# 🧠 六、为什么这是“正确设计”（非常关键）

---

## ✅ 1. 完全符合 ECS 思想

ECS 强调：

> 👉 数据 + 组合，而不是层级绑定 ([维基百科][2])

---

👉 SubMesh 变 Entity：

✔ 可独立控制
✔ 可单独系统处理

---

---

## ✅ 2. 和工业引擎一致

例如：

* OGRE：SubEntity ([ogrecave.github.io][3])
* Unity ECS：subMesh index ([docs.unity.cn][4])

---

👉 本质一样：

```text
Entity = Mesh实例
SubEntity = SubMesh实例
```

---

---

## ✅ 3. 支持这些能力（非常关键）

---

### 🎯 局部 transform

```text
只旋转“门”
```

---

### 🎯 材质 override

```cpp
r.materialOverride = newMat;
```

---

---

### 🎯 单独隐藏

```cpp
registry.remove<SubMeshRenderer>(sub);
```

---

---

### 🎯 picking 到 primitive

```text
直接返回 sub-entity
```

---

👉 不需要 encode primitiveID 了

---

---

# 🚀 七、Inspector 会变得非常简单

---

## UI：

```text
Car
 ├── Body
 ├── Wheel FL
 ├── Wheel FR
 ├── Window
```

---

👉 每个都是 Entity ✅

---

---

# ⚠️ 八、你必须注意的点

---

## ❗ 1. 不要复制 Mesh

```cpp
mesh 是共享资源
```

---

---

## ❗ 2. Transform 继承

```cpp
world = parent.world * local
```

---

---

## ❗ 3. 不要给 Primitive 加 Transform

👉 一定是 Entity 控制

---

---

## ❗ 4. 不要过度拆

👉 如果模型只是静态：

```text
一个 Entity + MeshRenderer 更快
```

---

---

# 🚀 九、进阶优化（你后面会用）

---

## 🟢 1. 合并 draw（重要）

```text
多个 subEntity → 同 buffer → batch
```

---

---

## 🟢 2. RenderItem 扁平化

```cpp
RenderItem {
    Primitive*
    Material*
    Transform
}
```

---

---

## 🟢 3. ID picking（更简单）

```text
pixel → entity（直接 submesh）
```

---

---

# 🧠 十、终极总结（非常关键）

> 👉 **SubMesh Entity = “一个 Primitive 的实例 + Transform + 可选 override”**

---

# 🚀 一句话版本（你可以写在代码注释里）

```text
SubMeshEntity 表示一个 Mesh 的某个 Primitive 的实例，
用于独立控制 transform / material / visibility。
```

---

# 🚀 如果你下一步继续做（强烈推荐）

我可以帮你把这一套升级成：

## 🔥 完整编辑器级结构

* Entity → Node → SubMeshEntity 自动生成
* Inspector 自动展开
* Picking → 自动选中 submesh
* Gizmo → 局部编辑

---

你现在这一步，其实已经在做：

👉 **“Unity / Unreal 内部的渲染对象模型”**

[1]: https://forums.ogre3d.org/viewtopic.php?t=67559&utm_source=chatgpt.com "what is the difference between MESH and SUBMESH - Ogre Forums"
[2]: https://en.wikipedia.org/wiki/Entity_component_system?utm_source=chatgpt.com "Entity component system"
[3]: https://ogrecave.github.io/ogre/api/13/class_ogre_1_1_sub_entity.html?utm_source=chatgpt.com "OGRE: Ogre::SubEntity Class Reference"
[4]: https://docs.unity.cn/Packages/com.unity.entities.graphics%401.2/api/Unity.Rendering.MaterialMeshInfo.SubMesh.html?utm_source=chatgpt.com "Property SubMesh
 \| Entities Graphics | 1.2.0-pre.6"
