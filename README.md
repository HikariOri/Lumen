# Lumen

基于 **C++23**、**Vulkan 1.4** 与 **SDL3** 的渲染引擎与学习用代码库。

**平台说明**：当前仓库**仅在 Windows 上做过构建与运行验证**（Visual Studio 工具链）。若在 Linux / macOS 上配置，需自行调整工具链与依赖路径；欢迎反馈可复现的补丁。

---

## 获取源码

```bash
git clone <你的仓库 URL> Lumen
cd Lumen
```

若需要生成带 Doxygen 主题的文档（可选），初始化子模块：

```bash
git submodule update --init --recursive
```

---

## 需要先安装的内容

| 项目 | 说明 |
|------|------|
| **操作系统** | Windows 10 / 11（当前唯一验证平台） |
| **Git** | 用于克隆与子模块 |
| **CMake** | **3.31 或更高**（与根目录 `CMakeLists.txt` 一致） |
| **C++ 编译器** | **Visual Studio 2022**（推荐「使用 C++ 的桌面开发」工作负载），或同等 MSVC 工具集 |
| **Vulkan SDK** | 安装 [Vulkan SDK](https://www.vulkan.org/sdk)；需包含 **Validation Layer**，且 **`glslc` 在 PATH 中**（SDK 自带） |
| **`slangc`** | 配置阶段会执行 `find_program(slangc)`，**必须能在 PATH 中找到**。可从 [Slang 发行版](https://github.com/shader-slang/slang/releases) 安装并加入 PATH；若通过 vcpkg 安装 `shader-slang`，请把对应 `tools` 目录加入 PATH（具体路径因三元组而异，一般在 `vcpkg_installed/<triplet>/tools/` 下） |
| **vcpkg** | 用于解析根目录 [vcpkg.json](vcpkg.json) 中的依赖（manifest 模式）。需设置环境变量 **`VCPKG_ROOT`** 指向你的 vcpkg 克隆目录 |

可选：

- **Ninja**：配合 `cmake -G Ninja` 可加快生成；非必须。
- **Doxygen**：若安装，可配置 `doc` 目标生成 API 文档；未安装时 CMake 会跳过并给出提示。

---

## 依赖如何提供

- **C/C++ 库**：由 **vcpkg** 根据 [vcpkg.json](vcpkg.json) 与 [vcpkg-configuration.json](vcpkg-configuration.json) 拉取并构建（SDL3、GLM、ImGui、VMA、Catch2 等，列表以 manifest 为准）。
- **系统级**：**Vulkan 驱动**（由显卡厂商提供）与 **Vulkan SDK** 需自行安装。
- **第三方源码**：`third_party/` 下部分内容随仓库提供；`doxygen-awesome-css` 为可选子模块。

首次通过 vcpkg 解析依赖时编译量较大，请预留时间与磁盘空间。

---

## 构建步骤（Windows + MSVC + vcpkg）

1. 安装并配置好上文「需要先安装的内容」。
2. 在仓库根目录执行（将 `VCPKG_ROOT` 换为你的 vcpkg 路径）：

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

调试构建可将 `Release` 改为 `Debug`。

3. 生成的可执行文件位于 `build\<配置>\` 下，例如：

| 目标 | 说明 |
|------|------|
| `sandbox` | 2D 纹理矩形示例 |
| `shadertoy` | 全屏 Shadertoy 风格示例 |
| `demo3d` | 3D 立方体示例 |

更完整的 API 与注意事项见 **[文档索引](docs/README.md)** 与 [快速参考](docs/guides/quick-reference.md)。

---

## 文档

完整索引见 **[docs/README.md](docs/README.md)**。编写规范：[中文排版](docs/guides/chinese-typography.md)、[C++ 文档示例风格](docs/reference/cpp-style.md)。

常用入口：

- [快速参考](docs/guides/quick-reference.md) — 项目列表、Vulkan 3D 要点、常用 API
- [GLM 与 Vulkan 适配](docs/reference/glm-vulkan.md) — 必选宏、Y 翻转、frontFace、UBO 对齐、常见坑
- [ImGui 集成说明](docs/design/imgui-integration.md) — Vulkan + SDL3 后端、SetMinImageCount、注意事项
- [开发注意事项](docs/guides/project-notes.md) — 初始化顺序、Vulkan 同步、常见坑点
- [编码风格](docs/reference/cpp-style.md) — C++ 风格参考
- [渲染引擎规划](docs/design/render-engine-roadmap.md)
- [Git 提交规范](docs/guides/git-commit-convention.md)
- [AI 协作开发（对话模型 → Cursor）](docs/guides/ai-assisted-workflow.md)
- [Cursor 内全流程（设计 → 交付）](docs/guides/cursor-end-to-end-workflow.md)
