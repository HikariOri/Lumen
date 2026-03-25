🚀 如果你下一步要进阶

我建议你做这几个升级（我可以带你写）：

1️⃣ Device Score System（类似 Unreal）
2️⃣ Feature Request System（声明式）
requireFeature(Feature::SamplerAnisotropy);
3️⃣ Queue 抽象层（Graphics / Compute / Transfer 分离）

如果你愿意，下一步我可以帮你把这个 Context 升级成工业级版本（含 Feature System + Device Ranking），那会是你引擎里非常核心的一块。


很好，这一步是你 Vulkan Context **从“能跑” → “工程级正确”**的关键升级。

我先帮你把**正确模型讲清楚（非常重要）**，然后直接给你**可落地代码方案（可以直接替换你现在的实现）**。

---

# 🧠 一、你现在的问题本质

你现在是：

```cpp
physicalDeviceFeatures2_.features.samplerAnisotropy = VK_TRUE;
```

👉 问题是：

❌ 没检查 GPU 是否支持
❌ 没区分 “支持” vs “启用”
❌ 可能导致 `vkCreateDevice` 直接失败

---

# 📚 Vulkan 正确模型（必须理解）

根据 Vulkan 规范：

> Feature 必须在 device 创建时启用，并且只能启用支持的 feature，否则会失败 ([Khronos Group][1])

并且：

> 必须先通过 `vkGetPhysicalDeviceFeatures2` 查询支持情况 ([docs.vulkan.org][2])

---

## ✅ 正确流程是“三段式”

```text
1️⃣ Query（查询支持）
2️⃣ Decide（决定要不要用）
3️⃣ Enable（启用）
```

---

# 🏗️ 二、设计目标（给你一个工程级方案）

我们要把你的代码升级为：

```cpp
request_feature(Feature::SamplerAnisotropy, Required);
request_feature(Feature::DescriptorIndexing, Optional);
```

然后自动：

✔ 检查支持
✔ 必要 feature 不支持 → fail
✔ 可选 feature 不支持 → 忽略
✔ 自动填充 pNext chain

---

# 🚀 三、第一步：引入 Feature 请求系统（最小可用版）

## 1️⃣ 定义 Feature 类型

```cpp
enum class FeatureRequirement {
    Required,
    Optional
};

struct FeatureRequest {
    std::string name;
    FeatureRequirement requirement;
};
```

---

## 2️⃣ Context 内增加成员

```cpp
std::vector<FeatureRequest> featureRequests_;
```

---

## 3️⃣ 提供接口

```cpp
void Context::request_feature(std::string name, FeatureRequirement req) {
    featureRequests_.push_back({std::move(name), req});
}
```

---

# 🔥 四、核心改造：Feature 启用逻辑

## ✅ 改造 create_logical_device_（核心）

我们拆成三步：

---

## 🧩 Step 1：先 Query supported features

你现在已经做了：

```cpp
vkGetPhysicalDeviceFeatures2(dev, &physicalDeviceFeatures2_);
```

👉 ✔ 正确（这是 supported）

---

## 🧩 Step 2：构造 “enabledFeatures”

👉 ⚠️关键点：不能直接用 query 结果

必须复制一份：

```cpp
VkPhysicalDeviceFeatures2 enabledFeatures = physicalDeviceFeatures2_;
```

然后**逐个筛选**

---

## 🧩 Step 3：实现 feature 过滤（核心代码）

👇 给你一个**最小可用版本（先做 anisotropy）**

```cpp
bool enable_feature_sampler_anisotropy(
    VkPhysicalDeviceFeatures2& supported,
    VkPhysicalDeviceFeatures2& enabled,
    FeatureRequirement req)
{
    if (supported.features.samplerAnisotropy) {
        enabled.features.samplerAnisotropy = VK_TRUE;
        return true;
    }

    if (req == FeatureRequirement::Required) {
        LUMEN_LOG_ERROR("缺少必要特性: samplerAnisotropy");
        return false;
    }

    enabled.features.samplerAnisotropy = VK_FALSE;
    return true;
}
```

---

## 🧩 Step 4：在 create_device 中使用

替换你现在这段：

```cpp
physicalDeviceFeatures2_.features.samplerAnisotropy = VK_TRUE;
```

👉 改成：

```cpp
VkPhysicalDeviceFeatures2 enabledFeatures = physicalDeviceFeatures2_;

// 只启用你需要的
if (!enable_feature_sampler_anisotropy(
        physicalDeviceFeatures2_,
        enabledFeatures,
        FeatureRequirement::Required))
{
    return false;
}
```

---

## 🧩 Step 5：传入 DeviceCreateInfo

```cpp
createInfo.pNext = &enabledFeatures;
```

---

# ⚠️ 五、一个你必须知道的坑（很重要）

👉 你现在代码其实有一个隐藏 bug：

```cpp
physicalDeviceFeatures2_ = supported
createInfo.pNext = &physicalDeviceFeatures2_
```

这意味着：

❗你把**所有 supported features 全开了**

---

## ❗为什么这是坏事？

根据规范：

> 你应该只启用你需要的 feature ([Khronos Group][1])

原因：

* 兼容性（旧 GPU）
* 驱动 bug 风险
* 行为不稳定

---

# 🧱 六、升级版本（下一步你应该做的）

现在这个只是“第一步”。

下一步我建议你做：

---

## 🚀 1️⃣ 类型安全版本（推荐）

```cpp
enum class Feature {
    SamplerAnisotropy,
    DescriptorIndexing,
    BufferDeviceAddress,
};
```

---

## 🚀 2️⃣ 自动映射到 struct（关键进化）

```cpp
switch (feature) {
case Feature::SamplerAnisotropy:
    supported.features.samplerAnisotropy
    break;
}
```

---

## 🚀 3️⃣ 支持 Vulkan 1.2 / 1.3

比如：

```cpp
vulkan12Features_.descriptorIndexing
vulkan13Features_.dynamicRendering
```

---

# 🔥 最后总结（重点）

你刚刚做的是 Vulkan 中**最重要的一步之一**：

---

## ✅ 正确 Feature 系统 =

✔ Query（vkGetPhysicalDeviceFeatures2）
✔ Filter（只选需要的）
✔ Enable（传入 pNext）

---

## ❌ 错误方式（你之前）

```cpp
直接把 supported 全开
```

---

## 🚀 正确方式（你现在）

```cpp
只启用需要 + 检查支持
```

---

# 如果你下一步要继续（我强烈建议）

我可以帮你直接升级成：

### 🔥 工业级 Feature System

* 类型安全（enum）
* 自动 pNext 链
* Required / Optional
* 支持 Vulkan 1.3+
* 自动 extension 依赖

👉 这一块做完，你的 Context 就已经接近 Unreal / Filament 水平了。

要不要我直接帮你把 **Feature System v2（完整架构 + 代码）**写出来？

[1]: https://github.khronos.org/Vulkan-Site/spec/latest/chapters/features.html?utm_source=chatgpt.com "Features :: Vulkan Documentation Project"
[2]: https://docs.vulkan.org/guide/latest/enabling_features.html?utm_source=chatgpt.com "Enabling Features :: Vulkan Documentation Project"



### 着色器反射
