[中文官方网站 (tokalang.dev)](https://tokalang.dev/zh) | [在线尝鲜 (Playground)](https://tokalang.dev/playground) | [阅读学术论文](https://arxiv.org/abs/2606.01974) | [English](README.md)

# Toka 编程语言

**Toka 是一门无需垃圾回收（GC）的现代系统级编程语言。它融合了类 C 的极致物理性能与底层控制权，吸纳了源自 Rust 的内存安全纪律，并致力于提供如同脚本语言般清晰、高效的开发体验。**

📖 **学术论文：** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## 🚀 "Show, Don't Tell"

告别繁重的 `<'a>` 生命周期标注负担，忘记错综复杂的 CMake。在 Toka 中，高性能系统代码被设计为保持显式、清晰且有纪律：

```toka
// Toka 的日常：物理级并发，极简的符号 RAII 与原生错误传播
import std/io::println
import std/net::TcpStream
import stdx/net/websocket

// `async` 显式染色；`stream#` 小写变量加 `#` 表示该变量的底层状态会被操作修改
fn handle_client(stream#: TcpStream) -> async Result<(), String> {
    // 自动深层释放、无隐式拷贝、彻底告别 GC 暂停
    auto ws_conn# = websocket::accept_async(stream).await!
    
    // .await! 中的 `!` 是 Toka 的原生异常传播符。
    // 遇到阻塞则交出控制权 (await)，遇到报错则直接向上冒泡 (!)，抛弃繁琐的 if err 检查
    ws_conn#.write_text_async("Hello from Toka!").await!
    
    // 离开作用域时，RAII 机制会静默、安全地深度回收所有的 Socket 与内存资源
    return Ok(())
}
```

> **只需一秒，立即体验：**
> ```bash
> curl -fsSL https://tokalang.dev/install.sh | bash
> ```

---

## 🎯 Toka 适合你吗？

为了尊重您的时间，在您决定深入了解前，请确认：

**✅ 它是你的绝佳选择，如果：**
*   你需要极高的运行性能与极低的内存占用（如高并发网关、游戏后端、系统级工具）。
*   你受够了 Go/Java 的 GC 暂停（STW），但也厌倦了在 Rust 里为了过编译而**无休止地和编译器“打架”**，甚至被迫绕路去实现某些本该非常自然的代码逻辑。
*   你希望拥有高效的开发体验，同时仍然保留接近机器层的安全边界与原生掌控力。

**❌ 它可能暂不适合你，如果：**
*   你的生产环境必须部署在纯 Windows Server 上（目前原生支持仍在开发中，暂推荐 WSL2/macOS/Linux）。
*   你需要开箱即用的极其庞大的业务级生态（如 Spring Boot 全家桶）—— Toka 的生态目前更偏向底层和系统级。

---

## 💎 核心支柱 (Core Pillars)

### 🛡️ 无需生命周期标注的显式资源安全
首创**单帽原则 (Single-Hat Principle)** 与 **PAL (Pointer Aliasing & Lifecycle) 检查器**。Toka 将所有权、借用、指针身份与可变意图直接暴露在源码表面，目标是在不引入显式生命周期参数的前提下提供强编译期资源安全约束。

### ⚡ 零成本抽象与 C 原生互操作
底层基于 **LLVM 20** 构建，**无垃圾回收 (No GC)**。Toka 的原生布局与面向 FFI 的设计，旨在让调用现有 C 生态（如 SQLite、OpenGL）保持直接、明确且可预测。

### 🚦 物理级透明的并发模型与错误流
拒绝隐式的“黑魔法调度器”。Toka 将函数的开销显式化：普通函数、`async` (异步挂起) 都有清晰的颜色边界。配合原生的 `!` 错误传播符，错误处理与异步调度会清楚地体现在函数签名和调用点上。

### 📦 一体化现代工具链
Toka 内置 `toka` CLI，支持 `toka run`、`toka build` 以及基于 `package.tk` / `build.tk` 的包与构建流程，让日常项目操作尽量贴近语言本身。

---

## 🌟 深度理念：属性标记与灵身二元论

Toka 通过采用**正交后缀符号**将内存属性显式化，彻底消除了隐蔽状态。开发者仅需扫一眼变量定义，就能瞬间秒懂其背后隐藏的内存结构与生命周期。

### 核心革新点 (Key Innovations)

**1. 原生语法的 RAII (Syntactic RAII)**
Toka 将**智能指针提升为核心语法符号**。这意味着“生命周期管理”被内化为语言的呼吸。
*   **^T (独占)** 和 **~T (共享)** 直接作为原生类型存在，让 RAII 成为零视觉负担的默认模式。

**2. 灵-身二元论 (Soul-Identity Duality)**
Toka 哲学性地将对象拆分为 **Soul (内涵/值)** 与 **Identity (外延/身份)**。
*   **精确语义**：通过借用 (`&`) 访问 Soul，通过指针 (`*`) 操作 Identity。消除“引用赋值”与“内容修改”的语义模糊。

**3. 变与空的彻底解耦 (Orthogonal Attributes)**
告别 `const volatile unsigned` 的关键字堆砌。Toka 极度严谨地规定了属性的归属：
*   **可变性是变量的属性 (`#`)**：代表身份转移或内容修改。
*   **可空性是类型的属性 (`?`)**：代表内存中可能不存在该实体。
*   **视觉化形态**：`auto ^p = new Node(...)`、`auto x# = 1`，以及极低频完整形态 `auto nul ^#node#: Node? = null`，都能在语法层面直接呈现所有权、可变性与可空性。

**4. @encap 显式封装与资源安全 (Explicit Encapsulation)**
Toka 默认普通的 `shape` 对外透明，但对于持有关键资源（如 File、Socket）的结构体，必须使用 `@encap` 进行严格封装。
*   **权限与生命周期绑定**：在 `impl Type@encap` 块中，开发者必须显式暴露接口，且这是定义 `drop` (析构) 等生命周期方法的唯一合法场所。这从底层契约上防止了资源的非法篡改与内存泄漏。

**5. 显式控制流与错误传播 (Explicit Control Flow & Error Propagation)**
Toka 偏好显式的控制流契约：`match` 执行穷尽检查，`guard` 让 `null` / `none` 检查直接可见，后缀 `!` 操作符则在不隐藏提前返回路径的前提下传播 `Result` / `Option` 失败。

### 符号系统表

| 标记 (Token) | 内容上的含义 (Value/Content) | 身份上的含义 (Identity/Address) |
| :--- | :--- | :--- |
| `#` | **可写**: 可修改字段/内容 | **可交换**: 可重定向(Reseat) |
| `?` | **可空**: 值为 `none` | **可空**: 身份为 `null` |
| `!` | **可写且可空** | **可交换且可空** |
| `^` | - | **独占指针** (所有权) |
| `~` | - | **共享指针** (引用计数) |
| `&` | **借用**: 值的临时视图 | **引用**: Soul 的临时视图 |
| `*` | - | **原始指针** (无所有权) |

---

## ✅ 项目状态 (路线图)

Toka 目前已经具备了完整的语言核心特性与标准库，我们正在积极构建编译器的自举 (Self-Hosting) 能力。

### 🚀 已完成的核心能力 (Completed)
- [x] **多平台原生支持** 
    - [x] **Linux / macOS**: 已提供完善的一等公民支持。
    - [x] **Windows**: 新增对 MinGW/MSYS2 ABI 的底层适配，已打通核心文件系统与进程调用。
- [x] **编译器基础设施** (Lexer, Parser, LLVM 20 IR CodeGen)
- [x] **类型系统** (基础类型, 五态 Shape 系统, ADTs, 模式匹配)
- [x] **内存管理** (独占/共享智能指针, 移动语义 (Move), 递归式释放, 指针重绑定)
- [x] **面向对象特性** (`impl` 块, 契约分离的 Trait 系统)
- [x] **控制流表达式** (基于值的循环表达式, `break`/`continue` 标签)
- [x] **模块与可见性** (物理路径与逻辑导入, 精细的 `pub` 访问控制)
- [x] **语义分析 (Sema)**
    - [x] 严格的可变性强制检查 (`#` 检查)
    - [x] 所有权与借用验证 (Ownership & Borrowing Verification)
    - [x] 显式空安全 (Explicit Null Safety)
    - [x] 资源安全分析 (强制含资源 Shape 必须实现 `@encap/drop`)
- [x] **高级特性**
    - [x] **泛型 / 模板** (支持刚性与指针形态泛型推导)
    - [x] **原生并发** (原生 OS 线程, 锁, MPSC 消息通道, `async`/`await` 协程)
    - [x] **标准库** (系统 I/O, 网络 Socket, 以及 `String`/`Vec`/`Option`/`Result` 等基础容器)
- [x] **开发体验 (DX) 工具链**
    - [x] **内置构建系统** (`toka run`, `toka build`)
    - [x] **语言服务器 (LSP)** (官方 `tokalsp` 已稳定集成至 VS Code，支持实时诊断与智能语法高亮)
    - [x] **官方包管理器** (`toka` CLI 内置 `package.tk` 依赖解析与全局注册表网关)
    - [x] **原生 Kebab-case 支持** (在底层词法层面完美融合 Lisp 风格连字符命名，并反向约束格式化)

### 🚧 正在进行与未来规划 (Next Steps)
- [ ] **Windows 平台特性补齐** (原生 Windows 底层 IOCP 异步网络栈支持)
- [ ] **编译器自举 (Self-Hosting)** (使用 Toka 完全重写目前的 C++ 前端)

## 🌟 社区与生态 (Ecosystem & Community)

看到社区用 Toka 构建出各种有趣的项目，我们感到非常兴奋！以下是一些值得一看的社区驱动项目：

- [**toka-book**](https://github.com/lumicore-dev/toka-book) ([在线阅读](https://lumicore-dev.github.io/toka-book))：一本详尽的、由社区驱动的 Toka 学习指南（灵感来源于“The Rust Book”）。

*(用 Toka 做了好玩的项目？欢迎提交 PR 把你的项目展示在这里！)*

## 📚 官方文档与资源

更多详细的**安装指引**、**教程**、**代码示例**以及**API 手册**，请访问 Toka 语言官方网站：

👉 **[tokalang.dev/zh](https://tokalang.dev/zh)**

> 注意：为了保证文档的统一性，避免信息过期，所有使用说明、最佳实践与深度技术文章均已集中迁移至官方网站。

---

## 致敬与灵感来源 (Inspirations & Lineage)

Toka 是一门站在巨人肩膀上的现代系统级编程语言。它的诞生并非为了重新发明轮子，而是为了在前辈们的智慧基础上，探索**高性能**与**开发体验**之间的最佳平衡。

### 1. C & C++ (Modern C++)
**核心启迪：极速的系统控制权、智能指针与 RAII (起源)**
Toka 的内存管理机制直接从 Modern C++ (C++11/14+) 一脉相承。Toka 将 `std::unique_ptr` 和 `std::shared_ptr` 的语义直接熔铸为**核心前缀语法** (`^T` / `~T`)，让资源安全首次变成一种“零视觉负担”的默认标准。

### 2. Rust
**核心启迪：所有权与借用安全法则 (Borrowing Discipline)**
Rust 证明了无 GC 的内存安全是可以达成的。Toka 受到 Rust 严格借用纪律的启发，但探索了另一种表层语法：通过显式资源形态、PAL 检查与依赖路由表达安全约束，而不是要求用户书写 `<'a>` 这样的生命周期参数。

### 3. Haskell / ML Family
**核心致敬：类型类与正交设计 (Typeclasses & Orthogonality)**
Toka 的 Trait 系统精神上继承自 Haskell 的 Typeclasses。我们推崇将“数据定义”(`shape`)与“行为实现”(`impl`)完全剥离。

### 4. Swift / Kotlin / C#
**核心致敬：空安全 (Null Safety)**
为了彻底解决“十亿美元的错误”，Toka 采用了**显式空安全**。通过后缀修饰符 (`?`)，我们将空指针检查强制提升到了类型系统层面。

### 5. Python
**核心致敬：可读性与开发效率 (Readability & Productivity)**
Toka 致力于将脚本语言的开发体验带入系统编程，力求让系统级代码拥有脚本般的清晰度与亲和力。

### 6. 万有引力与未尽的致敬
编程语言的设计是一场在浩瀚星海中不断传承的探索。Toka 拥抱并崇尚开源社区汇聚的集体智慧，并对一切推动了程序设计学发展的先行者们保持着最虔诚的敬畏。

---

## 学术引用

如果您在学术工作中引用了 Toka 语言的设计（包括其显式的 Hat-Soul 资源模型、PAL 借用安全机制、编译期反射能力与基于 Shape 的统一数据模型），请按照以下格式引用我们的论文与仓库：

### 学术论文 (arXiv)

```bibtex
@article{yi2026toka,
  title={{Toka}: A Systems Programming Language with Explicit Resource Semantics},
  author={Yi, Zhonghua},
  journal={arXiv preprint arXiv:2606.01974},
  year={2026},
  eprint={2606.01974},
  archivePrefix={arXiv},
  primaryClass={cs.PL},
  doi={10.48550/arXiv.2606.01974}
}
```

### 软件仓库

```bibtex
@misc{toka_language,
  author       = {Yi, Zhonghua and {Toka Language Contributors}},
  title        = {{Toka} Programming Language},
  howpublished = {GitHub repository},
  url          = {https://github.com/tokalang/toka},
  year         = {2025--2026},
  note         = {Version 0.9.8}
}
```
