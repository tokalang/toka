# Toka 现代包管理与构建系统设计规范 (Toka Package Management Specification)

## 1. 核心设计哲学
* **数据与执行物理隔离 (Separation of Concerns)**: 将包元数据声明与底层编译构建步骤彻底解耦。
* **TON (Toka Object Notation) 原生美学**: 拒绝引入 JSON、TOML 等异构配置格式。使用受限的、绝对安全的 Toka 自身语法（匿名元组/常量）来编写声明式配置，享受零开销解析与原生的 IDE 高亮支持。
* **确定性构建 (Reproducible Builds)**: 强制引入哈希与版本锁定机制，确保不同环境下的依赖树指纹绝对一致。

## 2. “三剑客”核心架构

Toka 的现代工程结构由三个具有高度辨识度的核心文件支撑：

### 📜 `package.tk` (包清单：纯数据，完全静态)
* **定位**：项目的“身份证”与依赖总纲，代替传统的 `package.json` 或 `Cargo.toml`。
* **规范**：
  * 文件内容必须是合法的 Toka 源码。
  * **只能包含数据结构**（例如 `pub const PACKAGE = (...)`），绝对禁止出现 `fn` 定义或任何执行控制流逻辑。
* 被 `toka add` 等 CLI 工具静态读写，对解析器和安全爬虫绝对友好。
* 官方维护包在不改变此格式的前提下，遵循
  [`official_package_v1.md`](official_package_v1.md) 的静态契约字段与资格证据要求。
* **示例**：
  ```toka
  pub const PACKAGE = (
      name = "my_app",
      version = "0.1.0",
      dependencies = (
          // 智能识别 1: 普通版本号 -> Registry 官方包
          tokamq = "1.0.0",
          
          // 智能识别 2: 以 `.` 或 `/` 开头 -> 本地 Path
          utils  = "../utils",
          
          // 智能识别 3: 包含 `/` 和 `:` 的类 Docker 标签 -> Git 包 + Tag
          router = "github.com/lumicore/router:1.2.0",
          
          // 只有遇到极其复杂的参数（如拉取特定 commit 或分支）时，才回退到显式声明
          core = Git("github.com/lumicore/core", commit="a1b2c3d")
      )
  )
  ```

### 🔒 `package.lock` (依赖指纹：机器生成的绝对真理)
* **定位**：防御供应链投毒，锁死编译环境。
* **规范**：
  * 完全由工具链（如 `toka build` / `toka add`）自动生成与维护。
  * 扁平化记录所有显式与隐式的子依赖的确切版本号（或 Git Commit SHA）以及源码的 SHA-256 加密哈希值。
  * **人类绝对不应手动修改此文件**。

### 🛠️ `build.tk` (构建编排：原 `Project.tk`)
* **定位**：编译和链接的指挥棒，处理复杂的 C 语言互操作或代码生成。代替传统的 `Makefile` 或 `CMakeLists.txt`。
* **规范**：
  * 作为入口执行脚本，导入 `build::{Executable, Library}`。
  * 对于没有复杂外部 C 依赖的普通项目，此文件应是**可选的**（`toka build` 有能力根据 `package.tk` 直接完成标准编译）。
  * **平滑兼容策略 (Fallback)**：`toka build` 寻找构建脚本的优先级为：`build.tk` > `Project.tk`。当发现仅存在 `Project.tk` 时，系统将继续编译，但会向终端打印黄色的 `Deprecation Warning`，引导开发者进行重命名迁移。

## 3. 现代化 CLI 子命令集 (The `toka` Toolchain)

为了支撑这套“三剑客”架构，实现丝滑的开发者体验，`toka` 命令行工具包含以下核心指令：

### 🚨 核心生命周期指令
* **`toka init` (新增)**
  * **作用**：在当前目录下就地初始化项目，直接生成 `package.tk` 与基础脚手架，避免 `toka new` 强制嵌套创建文件夹的问题。
* **`toka new <name>`**
  * **作用**：创建一个全新的项目文件夹，内置现代化的 `package.tk` 与可选的 `build.tk`。
* **`toka add <url/name>` (语义升级)**
  * **作用**：自动从 Git 或注册表下载包，**自动解析并将其以 TON 格式写入 `package.tk`**，随后生成或更新 `package.lock`。支持类似 Docker 标签的快捷语法（如 `toka add github.com/user/pkg:v1.2`）。
* **`toka remove <pkg>` 或 `toka rm` (新增)**
  * **作用**：安全地从 `package.tk` 中移除依赖节点，同步清理缓存并更新 `package.lock`。
* **`toka update` 或 `toka up` (新增)**
  * **作用**：根据 `package.tk` 中的模糊版本声明（如存在），主动拉取最新的兼容版本，并刷新 `package.lock` 以锁定新指纹。

### 🚀 隐式构建执行
* **`toka build` / `toka run` (体验跃升)**
  * **前置静默校验**：执行时会自动检查 `package.tk` 和 `package.lock`。如果发现依赖缺失或未下载，工具链将**静默自动拉取 (Implicit Fetch)**，彻底消除开发者手动执行 `toka fetch` 的心理负担。
  * **执行**：完成环境准备后，调用 `tokac` 编译执行 `build.tk`（或回退兼容的 `Project.tk`），并输出最终产物。

### 💡 进阶生态指令 (规划中)
* **`toka publish`**：向官方注册表 `pkg.tokalang.dev` 打包并推送当前项目。
* **`toka tree`**：在终端以树状图优雅打印当前项目锁定的所有依赖及子依赖版本结构。
