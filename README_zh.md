[中文官方网站 (tokalang.dev)](https://tokalang.dev/zh) | [快速开始](#快速开始) | [RC12 Public Preview](docs/release_notes_v1.0.0-rc.12.md) | [Discussions](https://github.com/tokalang/toka/discussions) | [支持](SUPPORT.md) | [AI 包复刻指南](AGENTS-USER.md) | [阅读学术论文](https://arxiv.org/abs/2606.01974) | [English](README.md)

# Toka systems programming language（Toka 系统编程语言）

**Toka 是一门以无 GC、可预测的资源成本、静态安全和面向 AI 的可验证语义为设计底线的系统编程语言；它让真实系统边界同时对程序员和工具保持显式。**

本项目统一使用名称 **Toka systems programming language**，官方仓库为
`tokalang/toka`。它与历史上的 Toka Forth、Tokelang，以及其他同名或近似命名的
Toka / Toke 项目无关。

确定性清理 · PAL 静态检查 · 显式 `cede` 转移 · 带版本的 JSON 语义协议

## 设计目标

### 三项底线

Toka 从三项底线出发：

- **无 GC、可预测的资源成本：** 底层表示和资源成本应该保持可预测，不能把 GC 或隐藏运行时层作为默认答案。
- **强静态安全：** 危险路径必须显式到足以让编译器检查，安全性是要求，而不是为了便利可以交换掉的特性。
- **面向 AI 的可验证语义：** 重要的编译器结论应以稳定、可机器读取的事实公开。AI 辅助修复必须能够定位相关契约、提出最小改动，并用编译器与目标测试验证结果。

在守住这三项前提的情况下，Toka 追问的是：系统代码能否在不隐藏真实系统语义的前提下，尽量接近简洁、直接、可维护的表达。源码应该尽量贴近程序员的意图和程序真实发生的行为：所有权转移、可变性、重绑定、可空性、错误传播、异步挂起、底层表示这些事情，应该在真正重要的位置可见、可被静态检查，同时日常代码仍然保持相对紧凑。

Toka 不以“最少语法”为目标。它追求的是：在真实系统语义不被隐藏的前提下，触摸到所需的最小表层。每一份表层复杂性都应该证明自己揭示了一个真实边界；否则这个边界就会变得隐式、遥远，或者更难审计。

这也是一条 AI 辅助开发要求：程序员阅读到的所有权、权限、异步和接口边界，也应当作为确定的编译器证据提供给工具。Toka 不把 AI 当作语言之外、只生成文本的工具；它把 AI 辅助编辑视为语言语义契约的消费者。

### 关键张力

在目前主流语言版图里，这个组合通常近似一个不可能三角：

- 贴近机器的性能
- 强安全性
- 清晰、可维护的表达

Toka 想挑战的正是这种张力，并尽可能逼近那个理想点。它追求的紧凑不是抹去底层行为，而是让底层行为在真正发生作用的位置保持显式。

一个可能的额外红利是优化可见性。当资源流、别名关系和底层表示被更清楚地表达出来时，编译器有机会看到传统 C 或 Rust 写法中不容易暴露的优化空间。在某些性能敏感场景下，这可能体现为更好的寄存器利用、更清晰的别名边界，以及超过等价手写实现的可能性。这是设计可能带来的结果，不是泛化的性能承诺。

### 实现路径

Toka 组合了几类机制来接近这个目标。设计上，它试图让日常代码保持可读，同时给资源、表示和安全边界在语言中留下精确的位置：

- 显式资源语义和确定性清理
- PAL（Path-Anchored Ledger）静态检查，用于借用有效性与资源契约安全
- 用紧凑标记表达可变性、重绑定、转移、可空性和 handle 身份
- 内建项目工具链，降低对庞大外部构建系统配置的依赖
- payload / handle 分离：裸名操作对象 payload；只有代码确实需要 handle 层时，才用 `&`、`*`、`^`、`~` 等帽子暴露或保留 handle 身份

帽子语法只是这套设计的结果之一，不是设计目的本身。它存在，是因为 Toka 需要一种紧凑且一致的方式区分 payload 操作和 handle 操作。

**设计谱系说明。** Toka 的帽子语法系独立设计；本项目同时承认 C、Cforall
和 Alusus 中的相关机制属于既有先例。Toka 不主张这些字符、多层引用或显式
handle 选择由本项目首创；其设计重点是将帽子形态与 payload/handle 选择、
所有权、借用、重绑定和资源契约整合。详见
[设计谱系与证据](docs/design_lineage.md)。

因此，Toka 探索的位置介于 C、Rust、Go、Zig 之间：接近机器层，强调静态纪律，同时让日常系统代码保持可读，而不是把重要的系统边界退回约定。

**论文：** [Toka: A Systems Programming Language with Explicit Resource Semantics (arXiv:2606.01974)](https://arxiv.org/abs/2606.01974)

## 快速开始

Toka 当前处于 Public Preview。为获得可复现的安装，请使用一个明确、已发布的
release candidate：

```bash
curl -fsSL https://tokalang.dev/install.sh | bash -s -- v1.0.0-rc.12
export PATH="$HOME/.toka/bin:$PATH"
export TOKA_LIB="$HOME/.toka/lib"
toka doctor
```

预构建 SDK 在项目编排和原生链接阶段仍会使用宿主工具，需要 Python 3.10+
和 C linker。Ubuntu / Debian 可一次安装完整运行依赖：

```bash
sudo apt-get install clang lld python3 pkg-config libssl-dev
```

`toka doctor` 会先检查这些运行条件，再报告 SDK ready。

更换 tag 前，请先检查 [GitHub Releases 页面](https://github.com/tokalang/toka/releases)。
不带参数的安装脚本会遵循 GitHub 的稳定版 Latest 选择器，因此不建议将其作为
Public Preview 阶段的默认路径。

若要参与编译器开发或测试未发布改动，可从源码构建。需要 CMake、C++17
编译器，以及 LLVM 20：

```bash
git clone https://github.com/tokalang/toka.git
cd toka
cmake -S . -B build
cmake --build build
export PATH="$PWD/build/bin:$PATH"
export TOKA_LIB="$PWD/lib"
```

创建项目，并加入官方嵌入式键值存储引擎
[TokaKV](https://github.com/tokalang/tokakv)：

```bash
toka new tokakv_hello
cd tokakv_hello
toka add tokakv
```

用下面的完整示例替换 `src/main.tk`：

```toka
import std/io::{println}
import official/tokakv::{TokaKvEngine}

fn main() -> i32 {
    auto db = TokaKvEngine::open(string::from("hello.tokakv")).unwrap()
    db.put(string::from("language"), string::from("Toka")).unwrap()

    auto value = db.get(string::from("language")).unwrap().unwrap()
    println("{}", value)

    db.close().unwrap()
    return 0
}
```

然后运行：

```bash
toka run
```

最终输出应为 `Toka`。这条流程已经使用干净的 RC10 SDK 和公开 `tokakv` 包
完成验证。示例为保持紧凑使用了 `unwrap()`；生产代码应显式处理存储和 I/O 错误。

若目标是用 AI 协助复刻生态库，请从 [AI 包复刻指南](AGENTS-USER.md) 开始；
它说明了受支持的包发布路径，以及每次编辑后应运行的编译器检查。

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

## 已经有 Rust，为什么还需要 Toka？

Rust 已经证明：实用的系统编程语言可以在不把垃圾回收作为默认方案的前提下，
提供强内存安全。Toka 建立在这项成就之上；它不把 Rust 视为失败的设计，也不把
自己定位成可以直接替换 Rust 的语言。

两门语言探索的是系统编程复杂性的不同分配方式。Rust 提供通用的生命周期与
trait 系统，能够表达高级借用模式；Toka 则倾向于局部路径推导、更窄的跨边界
依赖契约，以及在 PAL 无法局部证明安全时保守拒绝。

| 维度 | Rust | Toka |
| :--- | :--- | :--- |
| 借用关系 | 生命周期通常可以推导或省略；无法省略的关系使用具名生命周期参数表达 | PAL 推导局部关系；逃逸的借用值使用面向路径的 `<-` / `effects:` 契约，而不是具名生命周期变量 |
| 安全取舍 | 可以表达高度通用的借用模式，有时会带来较多类型层复杂性 | 有意少接受一部分困难的借用模式，以换取更小的日常源码表层 |
| 访问表示 | 引用、原始指针和所有权指针是不同类型，并经常参与解引用强制转换 | payload 操作与 handle 身份是两个源码维度，通过帽子形态表达 |
| 所有权转移 | move 遵循 Rust 的所有权、类型和值上下文规则 | 转移由所有权形态和 `cede` 等声明契约约束；解析到的 `cede` 形参本身就是所有权边界，即使调用方省略 `cede` 拼写 |
| 异步地址稳定性 | `Pin` 为 Future 提供底层地址稳定契约，普通异步代码通常会隐藏它 | PAL 状态跨挂起点保持有效，普通源码没有 `Pin` 构造；Toka 1.0 不支持 shape 内部自引用 |
| 机器工具链 | 提供 JSON 诊断、Cargo metadata、rust-analyzer 和成熟的工具生态 | 额外提供面向 PAL 判定、转移义务、权限和生命周期契约的领域语义协议，但这些接口及其生态年轻得多 |
| 成熟度 | 稳定语言、庞大生态、丰富生产经验，以及已有的形式化与认证工作 | Public Preview、生态年轻、实现仍在演进，生产证据明显更少 |

Toka 并不声称生命周期关系、地址稳定性、别名或资源转移可以没有成本。它把其中
一部分负担移入编译器的局部分析，用不同契约表达一部分关系，并拒绝一部分 Rust
能够表达的模式。

如果紧凑的所有权源码、显式 payload/handle 分离、可预测的无 GC 资源行为，或
机器可读的语义证据是主要需求，Toka 可能值得评估。如果生态广度、长期稳定性、
安全认证、平台覆盖或经过验证的生产部署更重要，Rust 仍然是更稳妥的默认选择。

## 核心设计

### 显式资源语义

Toka 没有垃圾回收器。托管资源通过确定性析构、move / transfer 语义、`@Encap` 生命周期边界，以及显式 `clone` / `drop` 契约管理。跨所有权边界的资源转移写作 `cede`。

### Payload-Handle 分离

语言区分“正在使用的对象内容”和“到达该对象的 handle”。因此 `*p = *q` 表达的是 handle 重绑定，而普通的 `p = q` 仍然是 payload 赋值。

### PAL (Path-Anchored Ledger) 静态检查

PAL（Path-Anchored Ledger，路径锚定账本）是 Toka 的编译期资源安全检查机制。它把借用、所有权转移和失效风险记录到源码级存储路径上，并拒绝会破坏活跃借用或所有权契约的操作。其目标是在没有用户手写 `<'a>` 这类生命周期参数的情况下，提供编译期纪律。

PAL 遵循四条核心规则：
1. **独占所有权是唯一的（Unique ownership is exclusive）：** `^` 资源在任意时刻只能由一个有效 handle 拥有。
2. **转移是显式的（Transfer is explicit）：** 所有权交接必须在语法上可见。直接书写帽子形态的 unique-handle move 也属于显式转移语法；`cede` 用于声明了 cede 契约的参数和显式 cede 交接路径，且必须履行相应的转移义务。
3. **借用有效性受保护（Borrow validity is protected）：** 在活跃借用存在时，任何可能使该借用失效的操作（如 move、`cede`、drop、handle 重绑定或底层重新分配等）都将被拒绝。
4. **独占修改需要独占权限（Exclusive mutation requires exclusive permission）：** 可变/独占借用与其他重叠的活跃借用冲突。普通不可变借用的设计含义是该借用通道只读，而不是对被借用存储作出全局冻结承诺；在 mutation class 完全拆分前，当前 checker 对重叠 payload 写入仍保持保守拒绝。

PAL 的取向是先保守，再宽容。Toka 不用降低安全性来换取更轻的表层；当某条所有权或借用关系无法在局部被证明，除非把完整生命周期演算暴露给用户时，编译器应该拒绝这种模式，或要求程序写成更显式的结构。这意味着 Toka 可能接受更少极端但安全的借用程序，但它保持了高安全线，同时降低日常代码中的证明负担。

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

### 原生与 AI 工具链

`toka` CLI 支持 `toka new`、`toka run`、`toka build`、包解析，以及基于 `package.tk` / `build.tk` 的构建编排。编译器也会导出依赖元数据，供增量构建路径使用。`tokalsp` 通过标准 LSP 传输提供诊断、悬停、定义跳转、引用、补全与重命名；详见 [LSP 支持](docs/lsp.md)。

除诊断外，Toka 还提供带版本的机器可读语义协议，当前覆盖公共编译器决策、`cede` 转移义务、TaskHandle 生命周期契约和 H/P 调用权限判定。这些事实使工具能够区分“调用方漏写转移”和“callee 未消费参数”，解释 payload 写入为何被拒绝，并为异步编辑选择对应的生命周期红线测试。

```text
语义上下文 -> 编译器证据 -> 最小改动 -> 编译器复检 -> 目标红线测试
```

```bash
toka evidence --json main.tk
toka cede-obligations --json main.tk
toka capabilities --json main.tk
```

机器可读诊断、语义证据与有界上下文见 [AI tooling](docs/ai_tooling.md)。这些协议是解释与验证接口，不承诺任何特定模型无需审查就能正确编写代码。

## RC12 状态与已知边界

Toka `v1.0.0-rc.12` 是已经发布的 **Public Preview** release candidate，
不是稳定 1.0 兼容性承诺。在当前稳定化阶段，1.0 语言语义已冻结；工作重点是
文档、生态采用、资格验证和缺陷修复，不再增加语言新特性。

| 平台 | RC12 状态 |
| :--- | :--- |
| Linux x86_64 | 已发布 Tier 1 SDK archive |
| Linux aarch64 | 已发布 Tier 1 SDK archive |
| macOS x86_64 | 已发布 Tier 1 SDK archive |
| macOS aarch64 / Apple Silicon | 已发布 Tier 1 SDK archive |
| Windows / MSYS2 | 源码构建与 dogfood 路径；没有 RC12 SDK archive |
| WSL2 / WASI | 可用或实验性路径；不是 1.0 阻塞发布目标 |

已知边界：

- RC12 是 prerelease；源码、包和接口兼容性在稳定 1.0 前仍可能变化。
- 语言尚未自举，包生态仍然年轻。
- TokaKV 当前是单进程嵌入式 preview 引擎，compaction 范围为 L0-to-L1；
  尚不包含更深层级、分布式复制或 Redis 协议服务端。

当前仓库包含：

- 基于 C++ 的编译器前端与 LLVM 20 后端。
- 面向可变性、move、借用、空访问、资源安全、morphic 泛型等规则的语义分析和诊断。
- 包含核心容器与系统级模块的标准库。
- `toka` 项目管理器 / 构建工具、`tokafmt`、`tokalsp`。
- 增量构建元数据与 TKI interface cache 校验。
- Linux 与 macOS 是受支持的 1.0 发布平台。

当前优先事项是让冻结后的 RC12 表面更容易评估：清晰文档、可复现示例、
TokaKV 这样的生态证明，以及发布资格验证。Windows parity 与最终自举仍属于后续工作。

## Toka 适合你吗？

如果你想要这些东西，Toka 可能值得关注：

- 无 GC 的系统编程与确定性资源清理。
- 底层表示控制，但不把裸指针作为默认公共 API 风格。
- 不依赖显式生命周期参数的静态资源流检查。
- 在调用点直接看见可变、重绑定、可空与资源转移意图。
- 适合实验、系统工具、运行时和基础设施代码的紧凑工具链。
- 需要让所有权、权限与生命周期约束可由编译器解释、并能被独立验证的 AI 辅助系统开发。

如果你需要这些东西，Toka 现在可能还不合适：

- 与 Rust、Go、Java、C++ 相当的大型生产生态。
- 长期稳定的语言规范承诺。
- 开箱即用的原生 Windows 生产部署。
- 直接替代现有 C/C++/Rust 代码库。

## 文档与资源

- 官方网站：[tokalang.dev/zh](https://tokalang.dev/zh)
- 网站源码：[tokalang/toka-web](https://github.com/tokalang/toka-web)
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
  note         = {Version 1.0.0-rc.12 public preview}
}
```
