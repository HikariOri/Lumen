# GLM 与 Vulkan 适配

GLM 默认遵循 OpenGL 习惯，Vulkan 使用不同的坐标与深度规则，需手动对齐。本文档总结不会出错的实战规则。

## 一、必选宏（include 之前）

```cpp
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
```

| 宏 | 作用 |
|----|------|
| `GLM_FORCE_RADIANS` | 角度统一为弧度，避免误用度数 |
| `GLM_FORCE_DEPTH_ZERO_TO_ONE` | 深度从 OpenGL 的 [-1,1] 改为 Vulkan 的 [0,1] |

未使用 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 时，深度测试会出错，物体可能消失。

---

## 二、Y 轴翻转与 frontFace

### 坐标系差异

| 空间 | GLM 默认 | Vulkan |
|------|----------|--------|
| Y 轴 | 向上 | 向下 |
| Z 范围 | [-1, 1] | [0, 1] |

### 核心规则

**翻转 Y = 镜像变换 = 改变屏幕空间中的三角形绕序（CCW ↔ CW）**

Vulkan 根据**屏幕空间**的顶点绕序判断正反面，不是模型空间。对投影做 Y 翻转后，CCW 与 CW 互换，因此必须同时调整 `frontFace`。

### 推荐组合

```cpp
// 投影
glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
proj[1][1] *= -1;

// 管线
rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
```

| 未翻转 Y | 翻转 Y 后 |
|----------|-----------|
| CCW 为正面 | CW 为正面 |
| CW 为正面 | CCW 为正面 |

### 另一种做法：负 viewport

```cpp
viewport.y = height;
viewport.height = -height;
```

负 viewport 不改变绕序，可保持 `VK_FRONT_FACE_COUNTER_CLOCKWISE`，但工程上更常用「投影 Y 翻转 + CW frontFace」。

---

## 三、lookAt 与矩阵布局

- **`glm::lookAt`**：无需修改，可直接使用
- **矩阵布局**：GLM 与 GLSL 均为列主序，一致，无需转置

---

## 四、UBO / std140 对齐

GLM 与 GLSL 都是列主序，但 std140 有严格对齐：

```cpp
// 不推荐：std140 中 vec3 按 16 字节对齐，易与 GLSL 侧 layout 不一致
struct Ubo {
    glm::mat4 model;
    glm::vec3 color;
};

// 推荐：改用 vec4，或按 std140 规则显式插入对齐占位
struct Ubo {
    glm::mat4 model;
    glm::vec4 color;
};
```

---

## 五、常见现象与原因

| 现象 | 原因 |
|------|------|
| 模型消失 | 启用 back-face culling 且 frontFace 与 Y 翻转不匹配 |
| 只看到背面 | 正反面定义反了 |
| 深度测试异常 | 未使用 `GLM_FORCE_DEPTH_ZERO_TO_ONE` |
| 画面上下颠倒 | 未对投影做 Y 翻转 |

---

## 六、推荐模板

```cpp
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// 投影
glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
proj[1][1] *= -1;

// 视图（无需改）
glm::mat4 view = glm::lookAt(eye, center, up);

// 管线光栅化状态（字段名以 Vulkan API 为准）
VkPipelineRasterizationStateCreateInfo rasterizer{};
rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
```

---

## 七、深入理解：数学 → 管线 → Vulkan 行为

### 问题本质

`proj[1][1] *= -1` 为何会导致 frontFace 反转？

### frontFace 如何判断

在 Vulkan 中，**正反面 = 屏幕空间三角形的有向面积符号**：面积 > 0 为一种方向，面积 < 0 为另一种。规范见 [VkFrontFace](https://registry.khronos.org/vulkan/specs/latest/man/html/VkFrontFace.html)。

### 面积的数学表达

三角形三点 `p0(x0,y0)`、`p1(x1,y1)`、`p2(x2,y2)`，有向面积：

```
A = (x1 - x0)*(y2 - y0) - (x2 - x0)*(y1 - y0)
```

本质是二维叉积。

### Y 翻转会发生什么

令 `y → -y`：

原式：

```
A = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0)
```

替换后：

```
A' = (x1-x0)*(-(y2-y0)) - (x2-x0)*(-(y1-y0))
   = -(x1-x0)*(y2-y0) + (x2-x0)*(y1-y0)
   = -A
```

**结论**：Y 轴翻转 → 面积符号取反 → winding order 反转。

### 线性代数视角

变换矩阵 `[[1,0],[0,-1]]` 的行列式 `det = -1`。**det < 0 的变换会改变空间手性**（右手系 ↔ 左手系），从而翻转三角形绕序。

### 管线中的位置

流程：Model → View → Projection → Clip → NDC → Screen。

frontFace 判断发生在**屏幕空间**（framebuffer coordinates）。在 projection 阶段翻转 Y，最终屏幕上的三角形已被镜像，winding order 必然改变。

### 为何 `proj[1][1] *= -1` 会这样

GLM 默认投影 Y 向上（OpenGL 习惯），`proj[1][1] *= -1` 等价于在 clip space 做 Y 镜像，从而改变最终屏幕几何与绕序。

### Vulkan 与 OpenGL 坐标系

| | OpenGL | Vulkan |
|---|--------|--------|
| 屏幕原点 | 左下 | 左上 |
| Y 方向 | 向上 | 向下 |

关系：Vulkan = OpenGL + Y 镜像。

### 为何会“啥都看不到”

翻转了 Y 但未改 frontFace → 所有本该是正面的面被当成背面 → 被 back-face culling 剔除。

### 三种解决方式的统一本质

| 方案 | 做法 |
|------|------|
| 1（常见） | `proj[1][1] *= -1` + `frontFace = VK_FRONT_FACE_CLOCKWISE` |
| 2（推荐） | `viewport.height = -height`（不改变绕序） |
| 3（shader） | `gl_Position.y = -gl_Position.y` |

本质：做了镜像 → 必须修正 winding（要么改 frontFace，要么用负 viewport 避免镜像）。

### 终极理解

**front face 不是模型定义的，而是“最终在屏幕上呈现的顶点顺序”决定的。**

### 一句话总结

```
Y 翻转 = det < 0 = 改变手性 = 面积符号翻转 = winding order 反转
```

### 工程记忆法

只要做了以下任一操作：

- `proj[1][1] *= -1`
- `gl_Position.y = -y`
- viewport 负高度

先问：**我是不是做了镜像？** 若是 → 调整 frontFace，或用负 viewport 避免。

---

## 参考资料

- [VkFrontFace](https://registry.khronos.org/vulkan/specs/latest/man/html/VkFrontFace.html) — Khronos 规范
- [Why projection matrices typically used with OpenGL fail with Vulkan](https://johannesugb.github.io/gpu-programming/why-do-opengl-proj-matrices-fail-in-vulkan/) — Johannes Unterguggenberger
- [Rendering API coordinate systems](https://www.cg.tuwien.ac.at/research/publications/2022/fiedler-2022-vlk/) — TU Wien
- [Vulkan Tutorial](https://vulkan-tutorial.com/) — Y flip 与 winding 的说明
