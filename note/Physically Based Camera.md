下面是把我们刚才讨论的**“基于物理的摄像机（Physically Based Camera）”**整理成一份结构化笔记，适合你直接放进引擎文档或学习笔记中👇

---

# 📷 基于物理的摄像机（Physically Based Camera）笔记

---

# 一、核心思想

## ✔ 定义

> **物理相机模型 = 模拟真实世界中“光线通过镜头并在传感器成像”的过程**

它是 Physically Based Rendering 的一部分。

👉 PBR 的目标是：

* 使用真实物理规律建模光与物体/相机的交互
* 提高渲染的真实感 ([维基百科][1])

---

## ✔ 与传统摄像机对比

| 类型                  | 特点             |
| ------------------- | -------------- |
| Pinhole Camera（传统）  | 无景深、无曝光、无限清晰   |
| Physical Camera（物理） | 有光圈、曝光、模糊、镜头效应 |

---

# 二、曝光模型（Exposure Model）

## ✔ 曝光定义

> 曝光 = 到达传感器的光量 ([Quizlet][2])

---

## ✔ 曝光三角（最重要）

### 1️⃣ 光圈（Aperture / f-stop）

* 控制镜头开口大小
* 决定进光量 & 景深

👉 规律：

* f 值小 → 光圈大 → 更亮 → 浅景深
* f 值大 → 光圈小 → 更暗 → 深景深

---

### 2️⃣ 快门（Shutter Speed）

* 控制曝光时间

👉 规律：

* 时间长 → 更亮 → 运动模糊
* 时间短 → 更暗 → 冻结运动

---

### 3️⃣ ISO（感光度）

* 控制传感器对光的敏感度

👉 规律：

* ISO 高 → 更亮 → 噪声增加
* ISO 低 → 更干净

---

## ✔ 三者关系

```text
Exposure ∝ Aperture² × Shutter Time × ISO
```

👉 三者共同决定最终亮度（曝光三角） ([Quizlet][2])

---

# 三、物理相机的关键视觉效果

---

## 1️⃣ 景深（Depth of Field）

### ✔ 原因

真实相机不是点，而是“有限大小光圈”：

> 只有焦平面是清晰的，其余会模糊

---

### ✔ 模型

👉 Thin Lens Model（薄透镜模型）

核心参数：

* 焦距（focal length）
* 光圈半径（aperture radius）
* 对焦距离（focus distance）

---

## 2️⃣ 运动模糊（Motion Blur）

### ✔ 原因

快门在一段时间内开启，而不是瞬时：

```text
t ∈ [t_open, t_close]
```

---

### ✔ 实现

* 对时间采样（temporal sampling）
* 多帧积分

---

## 3️⃣ 曝光（Exposure）

在物理渲染中：

👉 光强不是随便调，而是由相机参数决定

例如在 Filament 中：

* 使用 aperture / shutter / ISO 计算曝光
* 可匹配真实光照单位（lux） ([SceneView][3])

---

## 4️⃣ 镜头效应（Lens Effects）

真实摄像机还包含：

* 暗角（Vignetting）
* 色差（Chromatic Aberration）
* 畸变（Distortion）
* 光晕（Bloom / Lens flare）

👉 这些通常作为后处理或光学模拟实现 ([Maxon帮助中心][4])

---

# 四、完整物理相机模型（理论）

经典论文指出：

> 相机应建模为“镜头系统 + 成像平面”
> 并计算入射辐射在传感器上的能量 ([伍斯特理工学院计算机科学][5])

---

## ✔ 数学本质

物理相机做的事情：

```text
Radiance (场景光) → Lens → Irradiance (传感器能量)
```

---

# 五、在渲染引擎中的实现层级

---

## ✅ Level 1：基础（你现在）

```cpp
view = lookAt(...)
proj = perspective(...)
```

---

## ✅ Level 2：曝光控制

* HDR
* Tone Mapping
* Exposure 参数

---

## ✅ Level 3：物理相机

* Thin Lens DOF
* Motion Blur
* Physical Exposure（ISO / shutter / aperture）

---

## ✅ Level 4：电影级

* Lens distortion
* Bloom / glare
* Rolling shutter
* Camera response curve

---

# 六、工程实现要点（重点总结）

👉 一个“物理摄像机”至少需要：

### ✔ 几何参数

* position
* direction
* up

### ✔ 光学参数

* focal length
* aperture（光圈）
* focus distance

### ✔ 曝光参数

* ISO
* shutter speed
* exposure / EV

---

# 七、核心理解（最重要）

👉 普通摄像机：

> “从哪里看世界”

👉 物理摄像机：

> **“光是如何进入镜头并形成图像”**

---

# 📚 参考资料（精选）

* Physically Based Rendering: From Theory to Implementation
* Physically Based Rendering ([维基百科][1])
* Filament 相机曝光模型 ([SceneView][3])
* 曝光三角（Aperture / Shutter / ISO） ([Quizlet][2])
* Redshift Camera 文档（光学效果） ([Maxon帮助中心][4])
* Kolb 等人：Realistic Camera Model 论文 ([伍斯特理工学院计算机科学][5])
* PBR Lighting 实践说明 ([Irradiance Blog][6])

---

# ✔ 一句话总结

👉 **物理相机 = 几何（lookAt） + 光学（lens） + 曝光（aperture / shutter / ISO） + 传感器响应**

---

如果你下一步想继续深入，我可以帮你把这份笔记扩展成：

👉 Vulkan / path tracing 里的 **Thin Lens Camera完整实现代码**
👉 或“如何把这些参数映射到你的 Camera 类设计”

[1]: https://en.wikipedia.org/wiki/Physically_based_rendering?utm_source=chatgpt.com "Physically based rendering"
[2]: https://quizlet.com/435867747/exposure-aperture-shutter-speed-iso-flash-cards/?utm_source=chatgpt.com "Exposure: Aperture, Shutter Speed & ISO Flashcards | Quizlet"
[3]: https://sceneview.github.io/api/sceneview-android/sceneview/io.github.sceneview.utils/exposure-factor.html?utm_source=chatgpt.com "exposureFactor"
[4]: https://help.maxon.net/r3d/blender/en-us/Content/html/Redshift_Camera_Object.html?utm_source=chatgpt.com "Redshift Camera"
[5]: https://web.cs.wpi.edu/~emmanuel/courses/cs563/S07/projects/lens.pdf?utm_source=chatgpt.com "A Realistic Camera Model for Computer Graphics"
[6]: https://irradiance.ca/posts/pbr/?utm_source=chatgpt.com "Physically Based Content"
