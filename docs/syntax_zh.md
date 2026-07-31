# Toka 语法指南

本文描述当前 Toka 实现中的公开语法表面。它刻意保持保守：示例只选取编译器、标准库或测试集中已经使用的形式。短片段可能省略外围声明，但展示的语法应与当前 Toka 保持一致。

项目定位请先阅读仓库 [README_zh.md](../README_zh.md)。历史坑、内部提醒和 AI 辅助开发笔记保留在 [syntax_notes_zh.md](syntax_notes_zh.md)。

## 1. 核心模型

Toka 把许多系统语言混在一起表达的东西拆成两层：

| 层级 | 含义 | 示例 |
| :--- | :--- | :--- |
| Payload / Soul | 被读取、写入、传参、匹配的对象内容 | `x`, `x.field`, `x = value` |
| Handle / Representation | 对象如何被访问、拥有、借用、共享或重绑定 | `&x`, `*x`, `^x`, `~x`, `*x = *y` |

裸名操作 payload。帽子操作 handle 身份。这是理解 Toka 指针与资源语法的核心规则。

```toka
auto ^p = new i32(100)
auto value = 10
auto &r = &value
```

在这个例子中，`p` 是独占对象的 payload 视图，`^p` 命名独占所有权 handle，`&value` 创建借用 handle。

## 2. 文件、导入与入口

Toka 源码文件使用 `.tk`。

模块定位路径采用面向文件系统的写法。只要路径段指向目录或 `.tk` 文件名，就可以使用 kebab-case：

```toka
import std/io::println
import core/types::{usize, Addr}
import ./third-party/http-client as http_client

fn main() -> i32 {
    println("hello")
    return 0
}
```

连字符只属于路径层。凡是在 `.tk` 源码内部新产生、并进入 Toka 语义名字空间的名字，都必须是普通 identifier：变量、函数、类型、字段、import alias、import item alias、以及可选择的 namespace 都不使用 kebab-case。因此 `as http-client`、`http-client::send()`、`(package-name = "...")` 都是非法形式。在表达式语法中，二元 `-` 是操作符，必须用空格隔开，例如 `a - b`。

入口函数是 `main`。Toka 1.0 接受 `i32` 或 `void` 返回类型，async `main`
也遵循相同规则。返回 `Result` 的工作函数必须在入口边界显式处理；
`main -> Result<...>` 留待未来 termination 协议。

注释：

```toka
// 单行注释
/* 块注释 */
```

## 3. 绑定、可变性与可空性

局部变量使用 `auto` 声明。

```toka
auto x = 1
auto y: i64 = 10
auto z = 10:i64
```

整数字面量采用上下文提供的唯一整数类型，包括函数或方法参数、赋值目标、
返回类型以及比较中已经确定类型的另一侧。仅当不存在唯一上下文，或位宽本身
就是边界意图时，才需要显式类型。无法容纳于选定类型的字面量由 `E04598`
拒绝；Toka 不会静默截断上下文字面量。

绑定名上的 `#` 表示该绑定具有可变权限。

```toka
auto count# = 0
count = count + 1
```

`#` 出现在声明处和显式可变方法调用处。普通读取与赋值使用裸名。

```toka
counter#.inc()
counter = 3
```

### 三种缺失语义

Toka 1.0 刻意区分三种“无”。它们的物理表示可能同构，但在类型系统中不是别名：

| 语义域 | 表层写法 | 空状态 | 含义 |
| :--- | :--- | :--- | :--- |
| 可空 handle | `nul *p: T`、`nul ^p: T`、`nul ~p: T` | `null` | handle 存储存在，但不指向对象 |
| 可空 payload | `value: T?` | `none` | payload 槽位存在，但当前没有 payload 值 |
| 可选结果 | `result: Option<T>` | `Option<T>::None` | 一次操作没有产生结果 |

例如：

```toka
auto nul *ptr: i32 = null
auto maybe: i32? = none
auto result: Option<i32> = Option<i32>::None
```

`T?` 不是 `Option<T>` 的语法糖，`none` 不是 `Option<T>::None`，可空
handle 也不是 `Option` handle。Toka 不在这三个语义域之间进行隐式转换或
自动 flatten。借用 handle（`&`）不可为空；`nul` 只用于 raw、unique、
shared 这几类 handle 形态。

当一次操作可以成功取得一个可空 payload 时，这个区别是可观察的：

```toka
fn lookup(key: str) -> Option<string?>
```

这个返回类型具有三种语义不同的状态：

| 状态 | 含义 |
| :--- | :--- |
| `Option<string?>::None` | 没有找到 key |
| `Option<string?>::Some(none)` | 找到了 key，但它存储的 payload 为空 |
| `Option<string?>::Some(value)` | 找到了 key，并取得 payload 值 |

move 失效不是 `Option` 的另一种结果。PAL 跟踪 moved-from 绑定，用户不能通过
匹配公开的 `Moved` 变体观察它；标准 `Option<T>` 的结果域只包含 `Some(T)`
与 `None`。

控制流必须保留“存储为空”和“操作无结果”的区别。`??` 断言每次只解除源码
位置明确选中的 nullable 层。`guard ^node` 这类带帽 guard 只检查选中的
handle 层；`guard node` 这类裸 payload guard 则是路径可用性检查，可以一次
证明安全到达该 payload 所必需的全部 nullable handle 与 nullable payload
层。它的失败分支会有意把这些存储层的失败原因合并为“payload 路径不可用”。
如果代码需要区分究竟哪一层为空，就必须分别 guard handle 层与 payload 层。

两种 guard 都不会跨越名义上的 `Option` 或 `Result` 边界。后缀 `!` 只适用于
Result 传播一节规定的 `Result` 和 `Option`，不解开可空 handle 或可空
payload。因此，深层 payload guard 是控制流便利语法，不是隐式类型转换，也
不是 `Option` flatten。

实现可以让多个缺失语义域复用 niche 或其他紧凑布局。布局相同不会产生类型
同一性、隐式转换，也不构成稳定的跨语言 ABI 保证。

## 4. 帽子与 Handle

Toka 使用帽子暴露 handle 身份：

| 帽子 | 作用 |
| :--- | :--- |
| `&` | 借用 / 引用 handle |
| `*` | 原始指针 handle |
| `^` | 独占所有权 handle |
| `~` | 共享所有权 handle |

示例：

```toka
auto value = 10
auto &r = &value
auto ^owned = new i32(5)
```

payload 赋值与 handle 重绑定是不同操作：

```toka
p = value      // 写 payload
*p = *q        // 重绑定 raw pointer handle
```

如果 handle 绑定本身可以重绑定，把 `#` 放在帽子后面：

```toka
shape Node(
    val: i32,
    nul ^next: Node
)

auto ^#head = new Node(val = 0, ^next = null)
```

`#` 的位置有语义差异。`^#p`、`*#p`、`~#p`、`&#p` 表示 handle identity 可重绑定；`^p#`、`*p#`、`~p#`、`&p#` 里的 `#` 仍在绑定名 / payload 侧，不授予 handle 重绑定权限。两种权限都需要时，两个位置都要写，例如 `^#p#`。

`$` 是显式只读 / 阻断标记。因为普通 payload 默认就是只读的，`$` 绝大多数时候
应该省略；写在普通局部变量或参数上会被视为冗余并报错。它真正的用途是在会发生
权限继承的位置阻断继承：`field$` 即使通过 `obj#` 访问也始终不可写，`^$p`、
`*$p` 这类带帽写法则表示 handle identity 即使通过可写父对象访问也不可重绑定。

权限继承只在当前物理层生效。对象 payload 的可写权限可以继承到普通字段，但 handle
字段会形成继承边界：父对象可以授权重绑定该字段的 handle identity，但不会穿透到
pointee payload。若需要写 pointee payload，字段或绑定本身必须显式带 payload 侧
`#`，例如 `^p#` 或 `*p#`。

## 5. 函数、参数与 `cede`

函数参数需要显式类型。

```toka
fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

普通对象参数使用逻辑上的原地捕获。函数需要 payload 视图时，使用 `x: T` 或 `x#: T` 这类形式。

这是源码层语义规则，不是对物理参数布局的承诺。目标 ABI 可以把标量放入寄存器，
也可以用不同表示降低 aggregate、handle、closure 或返回值存储；这些选择不会在
源码语义中产生隐式复制，也不会改变 PAL 的调用借用规则。生成布局和调用约定仍然
绑定编译器、目标与版本，不属于稳定的 Toka 1.x 二进制 ABI。

只有函数确实需要 handle 本体时，才在参数名侧加帽子。例如 `*p: T` 表示 raw handle 参数，`*#p: T` 表示被调函数需要重绑定该 handle。

在调用点传递 handle 本体时，也使用带帽视图，例如 `take(*p)`。裸 `p` 仍然表示 payload 视图。

在 PAL 看来，一次函数调用会同时声明一组临时借用。把 `x` 传给 payload
参数仍然是一次借用事件，虽然源码没有写成 `&x`：`x: T` 产生临时共享 payload
借用，`x#: T` 产生临时独占 payload 借用。整个参数列表会一起检查，因此
只读 payload 参数下的 `read_twice(x, x)` 合法，但当任一参数需要独占 payload
访问时，`mutate_twice(x, x)` 和 `mutate_and_read(x, x)` 都会被拒绝。`cede`
实参是失效性转移，不是借用；它会与同一次调用中其他重叠路径实参冲突。

`cede` 是显式资源转移契约。

```toka
shape Resource(val: i32)

fn keep(cede r: Resource) -> Resource {
    return cede r
}
```

如果参数声明为 `cede`，调用方必须以 `cede` 传入，函数体也必须显式完成这条资源转移：消费、转发、存储、返回，或以其他方式结束资源路径。单纯读取 payload 不满足这条契约；未声明为 `cede` 的参数也不能在函数体内被 `cede`。

默认参数受到支持。调用方选择默认值时，调用参数列表需要包含 `..`。

```toka
fn test_val(x: i32 = 42) -> i32 { return x }

auto a = test_val(..)
```

会逃出函数边界的借用类值，必须在签名中声明依赖路径。Toka 1.0 对 private
和 public 函数统一要求显式标注：调用方只消费签名，被调函数体必须证明逃逸借用
只来自签名声明的依赖源。这样 `.tki` 接口与源码编译保持一致。

引用返回可以使用 effects 路由：

```toka
fn choose(a: i32, b: i32) -> &res: i32
effects:
    &res <- a | b
{
    if a > b {
        return &a
    }
    return &b
}
```

同一规则也适用于其他跨边界的 borrowed view，包括 `str`、`bytes`、包含 `&`
字段的 record / shape，以及捕获借用状态的闭包或 async 值。编译器可以在函数内
使用局部控制流分析，但调用点不依赖查看被调函数体。`^T`、`~T` 这类所有权或
共享 handle 本身不是 borrow-like dependency；只有返回值内部实际携带的 borrowed
state 才形成依赖。

Shape 头部依赖不属于 1.0 语法。直接写出 borrowed field：

```toka
shape RefInt(
    &val: i32
)
```

不要写 `shape RefInt <- val`。字段形态和初始化表达式本身携带依赖事实；返回值成员
依赖使用函数 `effects:` 路由表达。

Toka 1.0 也不支持 `&view: i32 <- owner` 这类 shape 内部成员依赖声明；parser 会把
它作为暂不支持的语法拒绝。这类关系会让 shape 具备内部自引用语义，必须先有稳定放置 /
不可移动构造模型才安全。1.0 中请通过函数返回 borrowed view，并在函数签名或
`effects:` 中声明依赖。shape 级 `effects:` 块不属于 shape 语法。

Execution boundary 比普通局部调用更严格。对 Toka 1.0 来说，thread / task
handoff 不能携带隐藏的借用状态：传给 `thread_spawn` 的闭包不能隐式捕获外层变量，
`.start` 也遵守同一规则。started task 可以按值接收不携带借用的标量；shape 或资源
必须同时通过 `cede` 参数和调用点显式 `cede` 转移。即使 shape 可复制，`.start`
也不会隐式复制，因为普通对象参数仍是逻辑原地捕获。引用、`str`、`bytes`、raw
pointer 以及携带 PAL dependency 的 task 都不能跨过 `.start`。`fn f(x: str) ->
async str <- x` 这类 async 返回依赖仍然只是普通签名依赖，并不授权 detached task
持有未声明的借用。

在声明为 `-> async T` 的函数中，`.await` 是源码层的 suspension consumer。未声明
async 的函数使用 `.await` 会被拒绝；async 函数内部使用会阻塞 executor 的 `.wait`
同样会被拒绝。Suspension 不会结束当前作用域，也不会重置语义状态。需要在
`.await` 后继续使用的局部值会保留在 coroutine frame 中，init、move 和 PAL 借用
事实会穿过 suspension point，并继续参与外层 `if`、`match`、`loop`、`break`、
`continue` 的状态合并。由 awaited async 结果产生的依赖在恢复执行后仍然有效，
因此替换、move 或 `cede` 其来源时，检查规则与同步代码完全相同。这些保证不会把
PAL 扩展到 raw pointer；raw pointer 仍属于显式 unsafe / FFI 边界。

### PAL (Path-Anchored Ledger) 静态安全边界

对 Toka 1.0 来说，PAL 冻结为 **Path-Anchored Ledger（路径锚定账本）**：safe 语言子集中的局部、基于路径的安全检查器。它将借用、所有权转移和失效风险记录到源码级存储路径上，跟踪借用路径、payload 修改、handle 重绑定、资源移动、unset 状态，以及 `if`、`guard`、`match`、`loop`、`for`、`break`、`continue` 产生的分析状态。

稳定契约遵循四条核心规则：
1. **独占所有权是唯一的（Unique ownership is exclusive）：** `^` 资源在任意时刻只能由一个有效 handle 拥有。
2. **转移是显式的（Transfer is explicit）：** 所有权交接必须在语法上可见。直接书写帽子形态的 unique-handle move 也属于显式转移语法；`cede` 用于声明了 cede 契约的参数和显式 cede 交接路径，且必须履行相应的转移义务。
3. **借用有效性受保护（Borrow validity is protected）：** 在活跃借用存在时，任何可能使该借用失效的操作（如 move、`cede`、drop、handle 重绑定或底层重新分配等）都将被拒绝。
4. **独占修改需要独占权限（Exclusive mutation requires exclusive permission）：** 可变/独占借用与其他重叠的活跃借用冲突。普通不可变借用的设计含义是该借用视图本身只读，而不是对原路径可达的全部存储作出全局冻结承诺。普通 payload 写入、独占修改与失效性操作会被分开分类。

在这些规则的约束下：
- 函数调用会一次性声明所有实参借用；payload 传参不会在 PAL 中隐形。
- 隐式解引用或基于借用的传参不能提升写权限：只读 `&T` 视图不能满足
  `T#` payload 参数。
- 共享借用会阻止同一路径或重叠父 / 子路径上的失效性操作或独占/可变修改。
- 普通 payload 写入本身不等同于失效性操作，但如果写入父路径会替换仍含有活跃借用的存储，则仍属于失效风险并会被拒绝。
- 可变借用会阻止重叠路径上的读写，除非该访问能被证明是互不相交的。
- `obj.&field`、`obj.&#field` 这类 terminal member borrow 会按选中的成员路径
  进入 PAL，语义上等同于 `&(obj.field)`。
- 对已借用资源路径执行 move 或 `cede` 会被拒绝。
- 在循环回边上移动定义于循环外部的资源路径会被拒绝；这也包括通过 `continue` 到达回边的路径。
- 带 `#` 的内部可变字段可以通过显式字段规则更新，但普通字段仍受当前借用保护。

PAL 不跨函数边界推断隐藏生命周期关系。会逃逸的 borrowed view 必须写在签名中，
从而让源码编译和 `.tki` 接口编译保持一致。raw pointer 与 `unsafe` 代码不属于
这条 safe-borrow 保证，除非它们被安全 API 封装，并由签名暴露必要的依赖关系。

## 6. Shape、Enum 与初始化

`shape` 用于定义聚合数据，也可定义带变体的数据。

```toka
shape Point(x: i32, y: i32)

shape State(
    On |
    Off |
    ErrCode(i32)
)
```

Shape 字段有名字。构造 Shape 使用具名参数。

```toka
auto p = Point(x = 10, y = 20)
```

Shape 定义始终是编译器可见的类型契约，即使部分字段通过 `@Encap` 对用户隐藏。
接口文件必须保留语义检查所需的完整结构事实，包括字段形态、可变性、可空性、
布局相关属性和 borrow-like 成员类型。可见性只控制用户访问，不擦除编译器知识。

字段可以有默认值。使用 `..` 接受剩余默认值。

```toka
shape Config(host: i32, port: i32 = 80, debug: i32 = 0)

auto cfg = Config(host = 127, ..)
```

按位置初始化不是公开推荐语法；对于要求具名字段的 Shape，当前诊断会拒绝位置初始化。

别名与新的名义类型：

```toka
alias ID = i32
type UserID = i32
```

## 7. 方法、Trait 与封装

方法定义在 `impl` 块中。

```toka
shape Rect(w: i32, h: i32)

impl Rect {
    pub fn area(self) -> i32 {
        return self.w * self.h
    }
}
```

Trait 实现使用 `impl Type@Trait`。

```toka
trait @Shape {
    pub fn area(self) -> i32
}

impl Rect@Shape {
    pub fn area(self) -> i32 {
        return self.w * self.h
    }
}
```

Trait 可以声明关联类型。普通 `type` 关联类型对某个 shape 的整个 trait family 保持稳定：一旦 `Data@Mapper<i32>::Output` 被绑定，另一个 `Data@Mapper<bool>` 实现也必须使用相同的 `Output`。`per type` 关联类型则绑定到具体 trait 实例，因此不同 trait 参数可以选择不同输出类型。

```toka
trait @Readable {
    type Item
    pub fn read(self) -> Item
}

shape IntBox(value: i32)

impl IntBox@Readable {
    type Item = i32
    pub fn read(self) -> Item {
        return self.value
    }
}

trait @Slot<K> {
    per type Value
    pub fn get(self) -> Value
}

shape IntSlot(value: i32)

impl IntSlot@Slot<i32> {
    per type Value = i32
    pub fn get(self) -> Value {
        return self.value
    }
}
```

在定义该关联类型的 trait 或 impl 块内部，方法签名和局部类型标注可以直接使用关联类型名。块外部使用投影语法：`IntBox@Readable::Item` 或 `IntSlot@Slot<i32>::Value`。

动态 trait 对象在类型位置使用 `dyn @Trait`。如果某个具体类型实现了该 trait，它的值可以传给期望 `dyn @Trait` 的参数；普通参数传递不需要写 `&`，因为 Toka 参数本来就是原地捕获。

```toka
fn print_area(item: dyn @Shape) -> i32 {
    return item.area()
}

auto rect = Rect(w = 10, h = 20)
auto area = print_area(rect)
```

通过 `dyn @Trait` 调用方法时，会经由 trait 接口动态派发。在定义模块之外，只有 trait 中的 `pub fn` 方法可以被调用。当前稳定的 trait object 语法是单个 trait facet，例如 `dyn @Shape`；`dyn @{A, B}` 不属于当前公开语法。动态闭包使用独立的 `dyn fn(...) -> T` 语法，不是 trait object。

不是所有 trait 都能成为 `dyn @Trait`。1.0 规则是：trait object 必须能被擦除为固定的 receiver handle 与固定 vtable ABI。因此，带泛型参数的 trait、带关联类型的 trait、带泛型方法的 trait、以及方法签名中在非 receiver 位置使用 `Self` 的 trait，在 1.0 中不能作为 `dyn @Trait` 使用。

Toka 1.0 也不支持 dynamic trait object 的关联类型绑定语法。`dyn
@Readable<Item = i32>` 这类写法会被拒绝。1.0 阶段请改用具体泛型参数、不带关联类型的
wrapper trait，或具体 adapter；显式绑定关联类型的 dyn object 设计推迟到 1.0 之后。

Trait 约束必须使用 `@Trait` 表示单个 facet，使用 `@{Trait1, Trait2}` 表示 trait facet set。Trait facet set 内部使用裸 trait 名称，因为前导 `@` 已经把整个集合放入 trait 语境。

```toka
fn draw_one<T: @Drawable>(item: T) {}
fn draw_and_fly<T: @{Drawable, Flyable}>(item: T) {}
fn convert<T, E1: @ErrorInto<E2>, E2>(value: T) {}
```

`T: {Drawable, Flyable}`、`T: {@Drawable, @Flyable}`、`T: @{@Drawable, @Flyable}` 这类形式会被拒绝。Import 中的 `path::{...}` 是导入项列表，不是 trait facet set。

标准 prelude 只隐式提供四个语义核心 trait：`@Encap`、`@Send`、`@Sync` 与
`@Callable`。其他 trait 名称均遵守普通词法模块命名空间，必须在当前模块声明
或通过 import 显式选择。仅加载一个模块不会让其中未选择的 trait 自动可见。

约束较多或需要独立表达时，使用 `where:` 区块。每一行是一条编译期约束。推荐形式与泛型参数约束一致：`T: @Trait` 或 `T: @{Trait1, Trait2}` 表示必须存在对应的 trait 实现。历史形式 `T impl @Trait` 仍被接受为兼容写法，但不是推荐风格。

```toka
fn copy<T>(io: T)
where:
    T: @{Reader, Writer}
{
}

trait @Ord
where:
    Self: @{Eq, PartialOrd}
{
}
```

`@Encap` 是显式资源策略标记。持有资源的 Shape 可以在
`impl Type@Encap` 中定义一个私有生命周期 hook：`fn drop(self#)`。
字段清理由编译器的生命周期计划统一完成，`drop` 不是普通可调用方法。

小写裸词 `encap` 已为未来独立语言构造预留，不能作为标识符使用；当前 trait
拼写始终是 `@Encap`。

复制能力由编译器的 `@Copy` 证明决定；不能复制的值不需要任何负向声明。
若类型明确需要产生第二个持有资源的值，应实现带有
`pub fn dup(self) -> Self` 的 `@Dup`。普通名为 `clone` 的方法仍可存在，
但不具有所有权、复制、lowering 或 trait 语义。

可见性有两层语法：

```toka
pub import std/io::{println}
pub shape Device(
    id: i32,
    secret: i32,
    public_config: i32,
    shared_state: i32
)
pub trait @Readable {
    pub fn read(self) -> i32
}
```

在声明层，前导 `pub` 会把 import、const、fn、shape、trait、alias、nominal type 导出到模块接口中。不写 `pub` 时，声明保持模块私有。

在普通 `impl` 和 `trait` 块中，方法可见性写作 `pub fn`。没有 `pub` 的方法对定义模块 / 接口语境保持私有。

`@Encap` 块还负责成员可见性控制。一旦某个 shape 拥有 `@Encap` 块，它的字段在定义源文件之外默认私有，除非 `@Encap` 可见性条目显式授权。

```toka
impl Device@Encap {
    pub public_config, shared_state

    fn drop(self#) {}
}
```

`pub field` 只全局开放该精确字段。带括号和通配符形式不属于
`@Encap`：`pub(crate)`、`pub(path)` 与 `pub *` 均会被拒绝。

每一个 `@Encap` 字段授权都必须逐字段写出；语法没有“全部字段”形式，
因此后续新增字段不会被意外公开：

```toka
impl PublicRecord@Encap {
    pub visible_name, visible_id, cache_slot
}
```

## 8. 成员访问与 Morphic 字段

普通成员访问请求 payload 视图。

```toka
auto x = point.x
point.x = 3
```

如果成员声明本身带有 handle 形态，当需要成员 handle 本体时，把帽子写在成员名处。

```toka
shape Data(^p#: i32)

auto d# = Data(^p = new i32(100))
d.^p = new i32(300) // 重绑定成员 handle
d.p = 500           // 通过 payload 视图写入
```

Morphic 字段在泛型代码内部保留 handle 形态。单引号写在绑定名上，不写在类型侧。

```toka
shape Box<'T>(
    'data: T
)

fn take_identity<'T>(cede box: Box<'T>) -> 'T {
    return cede box.'data
}
```

当泛型代码必须保留抽象 handle 形态时使用 `box.'data`。当代码明确请求 payload 视图时使用 `box.data`。

## 9. 泛型

刚性泛型参数描述 payload 类型。

```toka
shape Box<T>(data: T)
```

Morphic 泛型参数保留 handle 形态。

```toka
shape Box<'T>('data: T)
```

在有名字的绑定位置，单引号属于绑定名：

```toka
fn id<'T>('x: T) -> 'T {
    return 'x
}
```

局部绑定同理：写 `auto 'local: T = expr`，不要写 `auto local: 'T = expr`。

在纯类型位置，写 `'T`：

```toka
Vec<'T>
Option<'T>
-> 'T
sizeof('T)
```

## 10. 控制流

条件表达式不需要括号。

```toka
if x > 0 {
    return 1
} else {
    return 0
}
```

循环：

```toka
loop {
    break
}

loop count < 10 {
    count = count + 1
}

for auto x in [1, 2, 3] {
    println("{}", x)
}
```

非数组迭代使用 `core/traits` 中三个普通 trait；它们不是隐式 prelude
trait：

```toka
trait @Iterable {
    type Iter
    pub fn iter(self) -> Iter <- self
}

trait @Iterator {
    type Item
    pub fn next(self#) -> Option<Item>
}

trait @BorrowIterator {
    type BorrowedItem
    pub fn next_ref(self#) -> Option<BorrowedItem> <- self
}
```

`for auto item in values` 要求 `values: @Iterable`，并且其 `Iter` 类型实现
`@Iterator`。`for auto &item in values` 还要求 `@BorrowIterator`；`&&item`
等更深的引用 morphology 由 `BorrowedItem` 保留。`iter` 和 `next_ref` 的
依赖是强制契约：显式 cursor 存活期间以及整个 `for` 循环期间，PAL 都会
保持源集合被借用；此时修改、替换、移动或 `cede` 源集合会被拒绝。隐藏
cursor 是普通作用域值，在迭代结束、`break` 和函数退出时执行 drop。

值迭代不会隐式 `cede` 集合或元素；其所有权行为完全由 `next` 声明返回的
`Item` 决定，并继续服从普通复制和资源规则。Toka 1.0 不定义 consuming
iterator 或 async-iterator 协议。

当前语法没有 `while`；条件循环使用 `loop condition { ... }`。

`match` 支持字面量、range、变体、guard、or-pattern、通配符和 `default`。

```toka
auto value = match x {
    0 => { pass 10 }
    auto v if v > 0 => { pass v }
    default => { pass -1 }
}
```

对 1.0 来说，enum match 会进行安全穷尽检查。每个变体都必须被无 guard
的穷尽 pattern 覆盖，或者由 `_` / `default` 兜底；带 guard 的 arm 只能细化
某个 case，不计入穷尽性。非 enum 目标需要无 guard 的 wildcard、`default`
或无条件变量 arm。编译器不尝试证明整数、range 或字符串的完整值域覆盖。

`pass` 从块表达式中产出值。

## 11. 模式匹配与解构

Shape 使用具名解构：

```toka
shape Point(x: i32, y: i32)

auto p = Point(x = 10, y = 20)
auto Point(a = .x, b = .y) = p
```

使用 `..` 省略剩余字段，使用 `_` 忽略某个字段。

```toka
auto Config(h = .host, ..) = cfg
auto Config(h = .host, _ = .port, d = .debug) = cfg
```

变体匹配：

```toka
match result {
    auto Result<i32, str>::Ok(v) => { pass v }
    auto Result<i32, str>::Err(&e) => { pass 0 }
}
```

or-pattern 使用 `|`。同一个 or-pattern 中的每个分支必须绑定完全相同的名字、
兼容的类型和相同的修饰符，因为 arm body 只能看到合并后的一套绑定环境。

```toka
shape OpNode(
    Binary(string, Point) |
    Unary(string, Point) |
    Literal(Point)
)

match node {
    auto OpNode::Binary(&op, pos) | auto OpNode::Unary(&op, pos) => {
        pass pos
    }
    auto OpNode::Literal(pos) => { pass pos }
}
```

解构带资源的值时，如果不希望移动原值，需要在模式里显式借用。

Toka 1.0 的解构声明只允许出现在局部作用域。模块级全局可以绑定一个完整值，
但模块作用域中的解构声明会以 `E0744` 拒绝；应在函数内部解构该全局值。

## 12. 闭包

闭包使用 `{ ... => ... }` 语法。

```toka
auto add: fn(i32, i32) -> i32 = { a, b => a + b }
auto inc: fn(i32) -> i32 = { .a + 1 }
auto zero: fn() -> i32 = { => 0 }
```

捕获列表写在闭包体开头。

```toka
auto f: fn(i32) -> i32 = { [cede env] x => x + env }
auto r = 10
auto g: fn(i32) -> i32 = { [copy ~r] x => x + r }
auto h: fn() -> i32 = { [dup resource] => resource.value }
```

`copy` 捕获只用于经编译器证明的 `@Copy` 值，且绝不调用用户代码。它不能复制
资源所有权；资源值应使用 `cede` 转移，或者用 `[dup value]` 在构造闭包时显式且
仅一次地调用该类型已验证的 `@Dup::dup` provider。

当闭包转换为 `dyn fn` 时，任何捕获外部变量的行为都必须通过 `cede`、`copy` 或
`dup` 显式写入捕获列表。这样可以避免 owned、可移动的闭包对象静默保存指向局部状态的借用引用。

普通 `fn` 闭包在局部范围内可以使用隐式借用捕获。如果这类闭包发生逃逸，例如
从当前函数返回，那么这些隐式捕获会作为生命周期依赖参与检查。需要让逃逸闭包
拥有、复制或显式重复捕获状态时，应使用 `[cede ...]`、`[copy ...]` 或 `[dup ...]`。

Callable 权限沿用 Toka 的接收者形态，而不是建立一组名义上的 `Fn` traits：

| Callable 类型或接收者 | 契约 |
| :--- | :--- |
| `fn(A) -> R` / `call(self, ...)` | 共享、可重复调用 |
| `fn#(A) -> R` / `call(self#, ...)` | 独占、可重复调用 |
| `cede fn(A) -> R` / `call(cede self, ...)` | 消费式调用 |

编译器根据闭包 body 推导所需的最低权限：只读捕获需要共享调用，修改捕获状态
需要独占调用，以 `cede` 转移捕获值需要消费式调用。`[cede value]` 只表示闭包
拥有 `value`；只有 body 真正转移它时，闭包才成为消费式 callable。

绑定权限与 callable 权限彼此独立：

```toka
auto counter#: fn#(i32) -> i32 = { [cede state] value =>
    state.value = state.value + value
    state.value
}
auto next = counter#(1)
```

声明和调用处的 `counter#` 授予绑定的独占访问；`fn#(...)` 记录 callable 本身
要求这种访问。消费式 callable 以 `cede take()` 调用。如果被调用值不是消费式，
同一个 `cede` 表达式只作用于返回值，不会消费 callable。

`@Callable` 是唯一的隐式 prelude callable 协议。用户类型可用 `self`、`self#`
或 `cede self` 实现 `call` 方法，随后以普通调用语法使用；泛型约束写作
`F: @Callable`。Callable receiver mode 和返回生命周期依赖会保存在同版本 TKI
接口中。修改 owned capture 的线程回调使用独占 `fn#` 契约；detached 执行仍然
遵守既有的显式捕获、依赖和 `@Send` 规则。

### Result 错误传播

后缀 `!` 消费 `Result<T, E>` 或 `Option<T>`。成功分支移出 payload；失败
分支按逆词法顺序 drop 所有仍存活的局部值后返回 `Err` 或 `None`。操作数只
求值一次。对完整局部绑定传播会把该绑定标记为 moved；`holder.result!` 这类
部分路径在 1.0 中保守拒绝，应先绑定到局部值再传播。

在返回 `Result<U, E2>` 的函数中传播 `Result<T, E1>` 时，`E1` 与 `E2`
必须是同一解析类型，或者 `E1` 实现下面这个普通、需要显式导入的协议：

```toka
trait @ErrorInto<Target> {
    pub fn into_error(cede self) -> Target
}
```

转换消费 `E1`，且只在错误路径执行一次。它只能直接转换一步：Toka 不搜索
转换链，也不会用数值 widening、结构兼容或原始表示复制来转换错误。
`E1: @ErrorInto<E2>` 可用于泛型声明和 `where:`。选中的实现及其 receiver/
return 契约会保存在同版本 TKI 中。

`std/error::ErrorContext<E>` 保存 owned message 和原始类型化错误；
`with_context(cede result, message)` 不擦除 source。async `.await!` 在恢复后
遵循相同的转换、move 与 cleanup 规则。Toka 1.0 不包含 throw/catch、自动
转换链、通用 `dyn error`，也不隐式决定 cleanup error 是否替换主错误。

### 用于未完成编辑的类型洞

`todo` 是保留的、只能作为表达式使用的关键字，用于表示未完成编辑。它不是值、
变量、通配符、所有权来源，也不会打开宽松的构建模式。每个出现位置都有独立的
诊断，且都会令检查/构建保持非零失败。

```toka
auto answer: i32 = todo      // 已知完整的 `i32` 需求
if todo {                    // 已知完整的 `bool` 需求
    return 0
}
```

只有周边上下文已经决定需求时，编译器才记录该需求：显式类型的局部绑定、向既有
绑定赋值、布尔条件、已解析的普通调用参数，或显式实例化的泛型调用。这些情况仍
会以 `E04603` 报告程序不完整。需要由洞参与推断的上下文，例如
`auto answer = todo` 或 `identity(todo)`，则以 `E04604` 报告需求不足。

洞不能代表 place、能力、来源关系或转移。前缀/后缀访问、成员/索引访问、guard、
`cede todo`，以及将待办传给 `cede` 参数，都会以 `E04605` 拒绝。因此待办不会创建
H/P 权限，也不会隐式转移资源。

编辑器和 AI 工具可用以下命令取得确定性的需求事实：

```bash
toka todo-goals --json --check-only path/to/source.tk
# 或：tokac --todo-goals=json --check-only path/to/source.tk
```

该输出是仅表示需求的协议，不是普通语义证据，不能当作编译器批准。存在可达洞的
程序不会产生可执行文件、对象文件、TKI 或可复用编译产物。机器可读 schema 见
[Typed Todo v1](typed_todo_goals_v1.md)，完整边界见
[RFC](semantic_core/typed_todo_rfc.md)。

## 13. 字符串、文本与格式化

字符串相关值有几个层级：

| 形式 | 作用 |
| :--- | :--- |
| `"..."` | `str` 文本视图 |
| `"""..."""` | 原始 `str` 文本视图 |
| `c"..."` | C 字符串字面量 |
| `string::from("...")` | 拥有型可变 `string` |

原始 `str` 字面量使用三个或更多连续双引号作为围栏。闭围栏的双引号数量必须
与开围栏完全一致，因此可用更长围栏容纳较短的连续双引号：

```toka
auto path = """C:\Users\toka\config.json"""
auto quoted = """"
    Markdown can contain """ without escaping.
    """"
```

原始字面量不处理转义或插值，物理表示与普通文本字面量相同，都是静态只读
`str`。围栏没有前缀；尤其是 `c"""..."""` 不表示原始 C 字符串。

单行原始字面量必须在开围栏所在行闭合。如果开围栏与下一换行之间只有空格或
Tab，则进入多行形式：开围栏后的换行以及闭围栏前的换行只用于结构，不属于
结果。闭围栏必须单独占据空白行；它的缩进会从每个非空内容行移除，而额外缩进
保留。空白行会规范化为空行，源码中的 CRLF、CR 和 LF 换行都会在结果 `str`
中规范化为 `\n`。

格式化使用 `{}` 占位。

```toka
println("x={}, y={}", x, y)
```

普通 `{}` 可以打印 `String` 与 `str`。这些文本类型的格式说明符不属于 1.0
表面，诊断 `E04547` 用于标识这一排除项。

拥有型 `string` 与 `str` 视图（包括文本字面量）的相等比较，通过拥有值的
零分配只读 `as_str()` 投影完成。`command == "scan"` 和
`"scan" == command` 都只比较内容，不会分配、clone、move 或消费
`command`。

公开语法不使用 `+` 拼接字符串。需要构造拥有型字符串时，显式使用 `string` API，例如 `push_str`。

## 14. Unsafe 与 FFI

外部函数使用 `extern fn`。

```toka
extern fn sleep(seconds: i32) -> i32
extern fn libc_free(*ptr: void) -> void
```

原始分配与释放是显式 unsafe 操作。

```toka
shape Node(val: i32)

auto *node = unsafe alloc Node(val = 1)
unsafe free *node
```

指针转换使用 `as`。

```toka
auto *ptr = addr as *i32
auto raw = *ptr as *void
```

unsafe 代码应尽量留在系统与 FFI 边界。公共 API 不应暴露裸指针，除非 API 名称明确表达 unsafe 或 raw 语义。裸指针不属于 PAL 的 safe-borrow 保证范围，除非被安全库能力重新封装。

### 核心运行时契约

在正常作用域退出时，包括结构化控制流退出与函数返回，每个仍然存活的 owned 值
都恰好清理一次。成功的 `cede` 或 move 会转移这项义务，并阻止 moved-from 路径
再次清理。

Toka 1.0 中的 `panic` 是不返回的进程终止。它不是可捕获异常，也不承诺栈展开或
panic 点之后的清理。安全运行时中的边界检查和空值检查失败时使用相同的不返回
失败边界。原始分配、外部调用和裸指针有效性仍由显式 unsafe/FFI 代码负责。

## 15. 兼容性契约

Toka 1.x 保持冻结 1.0 表面中程序的源码层含义。新增能力以及放宽保守拒绝的版本
可以作为源码兼容扩展。内存安全或误编译修复可以拒绝依赖不健全行为的旧代码，
但必须记录为安全修复。

1.x 期间，诊断码不会被复用于无关规则；诊断文字和源码定位可以改进。`.tki`、
构建缓存格式、生成对象布局与二进制 ABI 均绑定编译器和格式版本，不提供跨版本
兼容承诺。

## 16. 常见错误

| 避免 | 使用 | 原因 |
| :--- | :--- | :--- |
| `let x = 1` / `var x = 1` | `auto x = 1` | Toka 使用 `auto` 声明局部绑定 |
| `while cond { ... }` | `loop cond { ... }` | 条件循环使用 `loop` |
| `for x in iter { ... }` | `for auto x in iter { ... }` | 迭代绑定必须显式 |
| `Point(1, 2)` | `Point(x = 1, y = 2)` | Shape 初始化使用具名字段 |
| `*p` 读取 payload | `p` | 裸名操作 payload |
| `*p = value` 写 payload | `p = value` | 带帽赋值操作 handle |
| `p = q` 重绑定指针身份 | `*p = *q` | 重绑定是 handle 操作 |
| `fn read(info: &Info)` | `fn read(info: Info)` | 帽子属于绑定名；普通参数使用 payload 视图 |
| 只读取 payload 时写 `fn inspect(&info: Info)` | `fn inspect(info: Info)` | 带帽参数是 handle 契约，不是普通传参的写法 |
| `shape Ref <- val (&val: T)` | `shape Ref(&val: T)` | borrow-like 字段直接携带依赖事实；shape 头部依赖已移除 |
| `fn id<'T>(x: 'T) -> 'T` | `fn id<'T>('x: T) -> 'T` | 在绑定位置，单引号属于绑定名 |
| `shape Box<'T>('data: 'T)` | `shape Box<'T>('data: T)` | 字段名保留形态，类型侧保持 `T` |
