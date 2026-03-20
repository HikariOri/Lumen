# Commit 规范（Conventional Commits）

本项目使用 **Conventional Commits** 规范来管理 Git 提交记录，并推荐使用 VSCode 插件 **vscode-conventional-commits** 进行提交。

---

# 📌 什么是 Conventional Commits

Conventional Commits 是一种**标准化的 commit message 规范**，用于让提交信息既能被人理解，也能被工具解析 ([conventionalcommits.org][1])

它的核心目标：

* 清晰表达每次提交的意图
* 自动生成 CHANGELOG
* 自动推导版本号（SemVer）
* 提升团队协作效率 ([conventionalcommits.org][1])

---

# 🧱 基本格式

```
<type>[optional scope]: <description>

[optional body]

[optional footer]
```

说明：

| 部分          | 必须 | 作用                      |
| ----------- | -- | ----------------------- |
| type        | ✅  | 提交类型                    |
| scope       | ❌  | 影响范围                    |
| description | ✅  | 简短描述                    |
| body        | ❌  | 详细说明                    |
| footer      | ❌  | 额外信息（如 BREAKING CHANGE） |

---

# 🧩 字段详解

## 1️⃣ type（提交类型）

表示这次提交的“性质”

常用类型：

| type     | 含义               |
| -------- | ---------------- |
| feat     | 新功能              |
| fix      | 修复 bug           |
| docs     | 文档修改             |
| style    | 格式/风格（不影响逻辑）     |
| refactor | 重构（不新增功能、不修 bug） |
| perf     | 性能优化             |
| test     | 测试相关             |
| build    | 构建系统或依赖          |
| ci       | CI/CD            |
| chore    | 杂项（不影响代码逻辑）      |

👉 `feat` 和 `fix` 是最核心的两类
👉 分别对应版本号升级：minor / patch ([conventionalcommits.org][1])

---

## 2️⃣ scope（作用范围）

表示修改影响的模块

示例：

```
feat(renderer): add shadow pass
fix(memory): resolve leak
```

👉 scope 非常重要（强烈建议使用）

---

## 3️⃣ description（简短描述）

规则：

* 使用英文
* 小写开头
* 不要句号结尾
* 说明“做了什么”，不是“为什么”

✔ 正确：

```
feat(renderer): add deferred shading
```

❌ 错误：

```
Added a new feature.
```

---

## 4️⃣ body（可选）

用于补充说明：

```
fix(memory): resolve leak in buffer allocator

The allocator was not releasing staging buffers.
```

---

## 5️⃣ footer（可选）

用于标记：

* BREAKING CHANGE
* issue 关联

示例：

```
BREAKING CHANGE: remove old render pipeline API
```

👉 表示**不兼容修改（大版本升级）** ([conventionalcommits.org][2])

---

# 🚀 推荐工作流（VSCode）

使用插件：

```
vscode-conventional-commits
```

操作：

1. `Ctrl + Shift + P`
2. 输入 `Conventional Commits`
3. 按提示填写

---

# 🎯 Vulkan 渲染引擎规范（重要）

默认规范不够细，我们对 **scope 做工程化约束**

---

## 🧱 推荐 scope 分类

```txt
renderer       渲染系统核心
vulkan         Vulkan 封装
rendergraph    RenderGraph 系统
pipeline       管线/PSO
shader         shader / 编译
resource       资源（buffer / image）
memory         内存管理（VMA 等）
descriptor     描述符系统
sync           同步（barrier / semaphore）
framegraph     帧结构
scene          场景系统
math           数学库
core           核心框架
platform       平台相关
tools          工具链
```

---

## 🧠 推荐 type + scope 示例

### ✅ 功能开发

```
feat(renderer): add forward rendering path
feat(rendergraph): support transient resources
feat(vulkan): add dynamic rendering support
```

---

### 🐛 Bug 修复

```
fix(memory): fix buffer leak
fix(sync): resolve pipeline barrier issue
```

---

### ⚡ 性能优化

```
perf(renderer): reduce draw call overhead
perf(pipeline): cache pipeline state
```

---

### 🔧 重构

```
refactor(rendergraph): simplify pass scheduling
```

---

### 📚 文档

```
docs(renderer): update pipeline usage guide
```

---

# ❗ 提交原则（强烈建议）

## 1️⃣ 一次提交只做一件事

❌ 错误：

```
feat: add renderer + fix bug
```

✔ 正确：

```
feat(renderer): add renderer
fix(memory): fix leak
```

---

## 2️⃣ 描述“做了什么”，不是“改了什么”

❌：

```
fix: change code
```

✔：

```
fix(memory): fix buffer leak
```

---

## 3️⃣ scope 必填（本项目要求）

👉 方便快速定位问题

---

# 📌 总结

本项目提交规范：

```txt
<type>(scope): description
```

核心理念：

* 清晰表达意图
* 保持提交粒度小
* 强制模块归属（scope）

---

# ✅ 推荐示例（最终标准）

```
feat(rendergraph): add pass dependency tracking
fix(memory): resolve staging buffer leak
perf(renderer): reduce pipeline switches
```

---

[1]: https://www.conventionalcommits.org/en/v1.0.0-beta/?utm_source=chatgpt.com "Conventional Commits"
[2]: https://www.conventionalcommits.org/en/v1.0.0-beta.4/?utm_source=chatgpt.com "Conventional Commits"
