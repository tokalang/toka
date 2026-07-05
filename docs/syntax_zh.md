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

入口函数是 `main`，通常返回 `i32`。

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

payload 可空类型在类型侧写 `?`，空 payload 值是 `none`。

```toka
auto maybe: i32? = none
```

handle 可空使用 `nul` 标记和 `null`。

```toka
auto nul *ptr: i32 = null
```

借用 handle（`&`）不可为空；`nul` 只用于 raw、unique、shared 这几类 handle 形态。

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

## 5. 函数、参数与 `cede`

函数参数需要显式类型。

```toka
fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

普通对象参数使用逻辑上的原地捕获。函数需要 payload 视图时，使用 `x: T` 或 `x#: T` 这类形式。

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

### PAL (Path-Anchored Ledger) 静态安全边界

对 Toka 1.0 来说，PAL 冻结为 **Path-Anchored Ledger（路径锚定账本）**：safe 语言子集中的局部、基于路径的安全检查器。它将借用、所有权转移和失效风险记录到源码级存储路径上，跟踪借用路径、payload 修改、handle 重绑定、资源移动、unset 状态，以及 `if`、`guard`、`match`、`loop`、`for`、`break`、`continue` 产生的分析状态。

稳定契约遵循四条核心规则：
1. **独占所有权是唯一的（Unique ownership is exclusive）：** `^` 资源在任意时刻只能由一个有效 handle 拥有。
2. **转移是显式的（Transfer is explicit）：** 所有权交接必须在语法上可见。直接书写帽子形态的 unique-handle move 也属于显式转移语法；`cede` 用于声明了 cede 契约的参数和显式 cede 交接路径，且必须履行相应的转移义务。
3. **借用有效性受保护（Borrow validity is protected）：** 在活跃借用存在时，任何可能使该借用失效的操作（如 move、`cede`、drop、handle 重绑定或底层重新分配等）都将被拒绝。
4. **独占修改需要独占权限（Exclusive mutation requires exclusive permission）：** 可变/独占借用与其他重叠的活跃借用冲突。普通不可变借用的设计含义是该借用视图本身只读，而不是对原路径可达的全部存储作出全局冻结承诺。普通 payload 写入、独占修改与失效性操作会被分开分类。

在这些规则的约束下：
- 函数调用会一次性声明所有实参借用；payload 传参不会在 PAL 中隐形。
- 共享借用会阻止同一路径或重叠父 / 子路径上的失效性操作或独占/可变修改。
- 普通 payload 写入本身不等同于失效性操作，但如果写入父路径会替换仍含有活跃借用的存储，则仍属于失效风险并会被拒绝。
- 可变借用会阻止重叠路径上的读写，除非该访问能被证明是互不相交的。
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

不是所有 trait 都能成为 `dyn @Trait`。当前公开规则是：trait object 必须能被擦除为固定的 receiver handle 与固定 vtable ABI。因此，带泛型参数的 trait、带关联类型但未在 dyn 类型中绑定的 trait、带泛型方法的 trait、以及方法签名中在非 receiver 位置使用 `Self` 的 trait，暂时不能作为 `dyn @Trait` 使用。

Trait 约束必须使用 `@Trait` 表示单个 facet，使用 `@{Trait1, Trait2}` 表示 trait facet set。Trait facet set 内部使用裸 trait 名称，因为前导 `@` 已经把整个集合放入 trait 语境。

```toka
fn draw_one<T: @Drawable>(item: T) {}
fn draw_and_fly<T: @{Drawable, Flyable}>(item: T) {}
```

`T: {Drawable, Flyable}`、`T: {@Drawable, @Flyable}`、`T: @{@Drawable, @Flyable}` 这类形式会被拒绝。Import 中的 `path::{...}` 是导入项列表，不是 trait facet set。

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

`@encap` 用于显式资源与可见性控制。持有资源的 Shape 应在 `impl Type@encap` 块中定义生命周期行为。

`@encap` 块中的典型生命周期方法包括 `fn drop(self#)` 和 `pub fn clone(self) = delete`。

可见性有两层语法：

```toka
pub import std/io::{println}
pub shape Device(
    id: i32,
    secret: i32,
    public_config: i32,
    crate_state: i32,
    uart_state: i32
)
pub trait @Readable {
    pub fn read(self) -> i32
}
```

在声明层，前导 `pub` 会把 import、const、fn、shape、trait、alias、nominal type 导出到模块接口中。不写 `pub` 时，声明保持模块私有。

在普通 `impl` 和 `trait` 块中，方法可见性写作 `pub fn`。没有 `pub` 的方法对定义模块 / 接口语境保持私有。

`@encap` 块还负责成员可见性控制。一旦某个 shape 拥有 `@encap` 块，它的字段在定义源文件之外默认私有，除非 `@encap` 可见性条目显式授权。

```toka
impl Device@encap {
    pub public_config
    pub(crate) crate_state
    pub(os/driver/uart) uart_state

    fn drop(self#) {}
    pub fn clone(self) = delete
}
```

`pub field` 全局开放指定字段。`pub(crate) field` 在 crate 内开放指定字段。`pub(path) field` 授权给指定模块路径。

`pub(path)` 中的 `path` 使用与 `import` 左半部分相同的模块定位路径语法，不包含 `::{...}` 内部名字选择。Toka 没有源码层 `mod` 声明；路径限定可见性锚定在 import resolver 归一化后的导入路径上，而不是任意子串匹配或 Rust 式模块树。

对于宽松的数据承载型 shape，`@encap` 块也可以使用通配可见性条目：

```toka
impl PublicRecord@encap {
    pub *
    pub * ! secret_key, internal_id
}
```

`pub *` 开放全部字段，`pub * ! field1, field2` 开放除列出字段以外的全部字段。通配条目通常是逐字段枚举授权的替代方案，不建议随意和更窄的授权混用。带括号的 `pub(crate)` 和 `pub(path)` 是 `@encap` 成员可见性条目，不是顶层声明修饰符。

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

当前语法没有 `while`；条件循环使用 `loop condition { ... }`。

`match` 支持字面量、变体、guard、通配符和 `default`。

```toka
auto value = match x {
    0 => { pass 10 }
    auto v if v > 0 => { pass v }
    default => { pass -1 }
}
```

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

解构带资源的值时，如果不希望移动原值，需要在模式里显式借用。

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
```

`copy` 捕获只用于可复制值或 handle。它不能复制资源所有权；资源值应使用
`cede` 转移，或者先显式 clone 到另一个值后再捕获。

当闭包转换为 `dyn fn` 时，任何捕获外部变量的行为都必须通过 `cede` 或 `copy` 显式写入捕获列表。这样可以避免 owned、可移动的闭包对象静默保存指向局部状态的借用引用。

普通 `fn` 闭包在局部范围内可以使用隐式借用捕获。如果这类闭包发生逃逸，例如
从当前函数返回，那么这些隐式捕获会作为生命周期依赖参与检查。需要让逃逸闭包
拥有或复制捕获状态时，应使用 `[cede ...]` 或 `[copy ...]`。

## 13. 字符串、文本与格式化

字符串相关值有几个层级：

| 形式 | 作用 |
| :--- | :--- |
| `"..."` | `str` 文本视图 |
| `c"..."` | C 字符串字面量 |
| `string::from("...")` | 拥有型可变 `string` |

格式化使用 `{}` 占位。

```toka
println("x={}, y={}", x, y)
```

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

## 15. 常见错误

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
| `fn id<'T>(x: 'T) -> 'T` | `fn id<'T>('x: T) -> 'T` | 在绑定位置，单引号属于绑定名 |
| `shape Box<'T>('data: 'T)` | `shape Box<'T>('data: T)` | 字段名保留形态，类型侧保持 `T` |
