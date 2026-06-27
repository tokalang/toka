[中文官方网站 (tokalang.dev)](https://tokalang.dev/zh) | [在线尝鲜 (Playground)](https://tokalang.dev/playground) | [阅读学术论文](https://arxiv.org/abs/2606.01974) | [English](README.md)

# Toka 编程语言

**Toka 是一门以零成本性能和静态安全为底线，同时让代码更易读、易写、易维护的无 GC 系统编程语言。**

## 设计目标

### 双底线

Toka 从两个底线出发：

- **零成本性能：** 底层表示和资源成本应该保持可预测，不能把 GC 或隐藏运行时层作为默认答案。
- **强静态安全：** 危险路径必须显式到足以让编译器检查，安全性是要求，而不是为了便利可以交换掉的特性。

在守住这两个前提的情况下，Toka 追问的是：系统代码还能不能更简洁、更直接、更可维护。源码应该尽量贴近程序员的意图和程序真实发生的行为：所有权转移、可变性、重绑定、可空性、错误传播、异步挂起、底层表示这些事情，应该在真正重要的位置可见、可被静态检查，同时日常代码仍然保持相对简洁。

### 关键张力

在目前主流语言版图里，这个组合通常近似一个不可能三角：

- 贴近机器的性能
- 强安全性
- 简洁、可维护的表达

Toka 想挑战的正是这种张力，并尽可能逼近那个理想点。

一个可能的额外红利是优化可见性。当资源流、别名关系和底层表示被更清楚地表达出来时，编译器有机会看到传统 C 或 Rust 写法中不容易暴露的优化空间。在某些性能敏感场景下，这可能体现为更好的寄存器利用、更清晰的别名边界，以及超过等价手写实现的可能性。这是设计可能带来的结果，不是泛化的性能承诺。

### 实现路径

Toka 组合了几类机制来接近这个目标：

- 显式资源语义和确定性清理
- PAL (Pointer Aliasing & Lifecycle) 检查
- 用紧凑标记表达可变性、重绑定、转移、可空性和 handle 身份
- 内建项目工具链，降低对庞大外部构建系统配置的依赖
- payload / handle 分离：裸名操作对象 payload；只有代码确实需要 handle 层时，才用 `&`、`*`、`^`、`~` 等帽子暴露或保留 handle 身份

帽子语法只是这套设计的结果之一，不是设计目的本身。它存在，是因为 Toka 需要一种紧凑且一致的方式区分 payload 操作和 handle 操作。

因此，Toka 探索的位置介于 C、Rust、Go、Zig 之间：接近机器层，强调静态纪律，同时让日常系统代码保持可读。

**论文：** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## 快速开始

安装最新 release：

```bash
curl -fsSL https://tokalang.dev/install.sh | bash
```

从源码构建：

需要 CMake、C++17 编译器，以及 LLVM 20。

```bash
git clone https://github.com/tokalang/toka.git
cd toka
cmake -S . -B build
make -C build -j8
```

检查编译器和项目管理器版本：

```bash
tokac --version
toka --version
```

## 90 秒心智模型

理解 Toka，最重要的是先分清两层：

| 层级 | 含义 | 典型语法 |
| :--- | :--- | :--- |
| Payload / Soul | 被读取、写入、传参、模式匹配的对象内容 | `x`, `x.field`, `x = value` |
| Handle / Representation | 对象如何被访问、拥有、共享、借用或重绑定 | `&x`, `*x`, `^x`, `~x`, `*x = *y` |

这与 C 里把 `*p` 理解为“p 背后的值”的习惯相反。在 Toka 中：

| 意图 | Toka 写法 | 含义 |
| :--- | :--- | :--- |
| 读取或写入对象内容 | `p` / `p = value` | 操作 payload |
| 观察或移动指针式身份 | `*p`, `^p`, `~p`, `&p` | 操作 handle |
| 重绑定 handle | `*p = *q` | 让 handle 指向别处 |
| 允许 payload 绑定可写 | `x#` | payload 可被修改 |
| 允许 handle 绑定可重绑定 | `*#p`, `^#p` | handle 本体可被替换 |
| 显式转移资源 | `cede x` | 调用方放弃该资源路径 |

函数参数也遵循同一规则。普通对象参数使用逻辑上的原地捕获：函数需要 payload 视图时写 `x: T` 或 `x#: T`。只有当函数确实需要 handle 本体时，才在参数名侧加帽子，例如用于观察、转发或重绑定这个 handle。

```toka
shape Resource(val: i32)

fn keep(cede r: Resource) -> Resource {
    return cede r
}

fn main() -> i32 {
    auto r = Resource(val = 42)
    auto moved = keep(cede r)

    if moved.val != 42 {
        return 1
    }
    return 0
}
```

签名里的 `cede` 不只是权限标记。它也是执行义务：函数体必须显式消费、转发、存储、返回，或以其他方式完成这条资源转移。

## Toka 为什么存在

主流系统语言通常在几个目标之间取舍：

| 语言家族 | 强项 | Toka 试图补上的缺口 |
| :--- | :--- | :--- |
| C | 直接的表示控制和可预测 ABI | 安全高度依赖约定 |
| C++ | RAII、泛型抽象、底层控制 | 许多所有权和别名规则仍然隐含 |
| Rust | 无 GC 的强内存安全 | 生命周期和借用推理可能成为主要表层负担 |
| Go / Java / C# | 日常开发高效，生态庞大 | GC 和运行时模型削弱底层可预测性 |
| Zig / Odin | 简洁的系统级控制 | 资源与别名纪律更多依赖程序员维护 |

Toka 的判断是：**访问表示本身应该成为源码里的独立维度**。与其把指针身份、所有权、共享、借用、可空、可变、资源转移全部压进一个过载的变量记法中，不如给这些概念提供小而正交的标记，并让编译器检查它们形成的契约。

## 核心设计

### 显式资源语义

Toka 没有垃圾回收器。托管资源通过确定性析构、move / transfer 语义、`@encap` 生命周期边界，以及显式 `clone` / `drop` 契约管理。跨所有权边界的资源转移写作 `cede`。

### Payload-Handle 分离

语言区分“正在使用的对象内容”和“到达该对象的 handle”。因此 `*p = *q` 表达的是 handle 重绑定，而普通的 `p = q` 仍然是 payload 赋值。

### PAL 静态检查

PAL 检查指针别名与生命周期约束，让借用、移动、空值检查、资源路径和重绑定意图保持显式且可审计。目标是在没有用户手写 `<'a>` 这类生命周期参数的情况下，提供编译期纪律。

### 正交表层标记

Toka 使用一组紧凑的标记：

| 标记 | 作用 |
| :--- | :--- |
| `#` | payload 绑定可变，或 handle 绑定具有重绑定权 |
| `?` | 可空类型状态 |
| `&` | 借用 / 引用 handle |
| `*` | 原始指针 handle |
| `^` | 独占所有权 handle |
| `~` | 共享所有权 handle |
| `'T` | 保留 handle 形态的 morphic 泛型参数 |

### 显式控制流与错误传播

控制流成本应当可见。`async` 标记可挂起函数，`.await` 标记挂起点，后缀 `!` 在不隐藏提前返回路径的情况下传播 `Result` / `Option` 失败。

### 原生工具链

`toka` CLI 支持 `toka new`、`toka run`、`toka build`、包解析，以及基于 `package.tk` / `build.tk` 的构建编排。编译器也会导出依赖元数据，供增量构建路径使用。

## 当前状态

Toka 仍处于积极开发中。当前仓库包含：

- 基于 C++ 的编译器前端与 LLVM 20 后端。
- 面向可变性、move、借用、空访问、资源安全、morphic 泛型等规则的语义分析和诊断。
- 包含核心容器与系统级模块的标准库。
- `toka` 项目管理器 / 构建工具、`tokafmt`、`tokalsp`。
- 增量构建元数据与 TKI interface cache 校验。
- Linux 与 macOS 是主要开发路径，Windows / MSYS2 支持正在推进。

Toka 还没有自举。生态仍然年轻。近期最重要的工程工作仍然是编译器加固、标准库稳定、Windows parity，以及最终自举。

## Toka 适合你吗？

如果你想要这些东西，Toka 可能值得关注：

- 无 GC 的系统编程与确定性资源清理。
- 底层表示控制，但不把裸指针作为默认公共 API 风格。
- 不依赖显式生命周期参数的静态资源流检查。
- 在调用点直接看见可变、重绑定、可空与资源转移意图。
- 适合实验、系统工具、运行时和基础设施代码的紧凑工具链。

如果你需要这些东西，Toka 现在可能还不合适：

- 与 Rust、Go、Java、C++ 相当的大型生产生态。
- 长期稳定的语言规范承诺。
- 开箱即用的原生 Windows 生产部署。
- 直接替代现有 C/C++/Rust 代码库。

## 文档与资源

- 官方网站：[tokalang.dev/zh](https://tokalang.dev/zh)
- 在线 Playground：[tokalang.dev/playground](https://tokalang.dev/playground)
- 学术论文：[arXiv:2606.01974](https://arxiv.org/abs/2606.01974)
- 语法参考：[docs/syntax_zh.md](docs/syntax_zh.md)
- 构建工具说明：[docs/BUILD_TOOL.md](docs/BUILD_TOOL.md)

## 社区与生态

- [**toka-book**](https://github.com/lumicore-dev/toka-book) ([在线阅读](https://lumicore-dev.github.io/toka-book))：一本详尽的、由社区驱动的 Toka 学习指南。

用 Toka 做了有趣的项目？欢迎提交 PR 把你的项目展示在这里。

## 与其他语言的关系

Toka 受 C / C++ 的表示控制和确定性资源管理启发，也受 Rust 的编译期内存安全纪律、ML 家族的代数数据和 trait 风格抽象，以及脚本语言的日常简洁性影响。它的独特贡献不是声称全面替代这些语言，而是把 payload 语义与 handle 语义的显式分离作为一条源语言层面的设计原则。

---

## 学术引用

如果您在学术工作中引用了 Toka 语言的设计（包括其显式的 Hat-Soul 资源模型、PAL 借用安全机制、编译期反射能力与基于 Shape 的统一数据模型），请按照以下格式引用我们的论文与仓库：

### 学术论文 (arXiv)

```bibtex
@misc{yi2026toka,
  title={{Toka}: A Systems Programming Language with Explicit Resource Semantics},
  author={Yi, Zhonghua},
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
  note         = {Version 0.9.8-06}
}
```
