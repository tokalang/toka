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

```toka
import std/io::println
import core/types::{usize, Addr}

fn main() -> i32 {
    println("hello")
    return 0
}
```

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

## 5. 函数、参数与 `cede`

函数参数需要显式类型。

```toka
fn add(a: i32, b: i32) -> i32 {
    return a + b
}
```

普通对象参数使用逻辑上的原地捕获。函数需要 payload 视图时，使用 `x: T` 或 `x#: T` 这类形式。

只有函数确实需要 handle 本体时，才在参数名侧加帽子。例如 `*p: T` 表示 raw handle 参数，`*#p: T` 表示被调函数需要重绑定该 handle。

`cede` 是显式资源转移契约。

```toka
shape Resource(val: i32)

fn keep(cede r: Resource) -> Resource {
    return cede r
}
```

如果参数声明为 `cede`，函数体必须显式完成这条资源转移：消费、转发、存储、返回，或以其他方式结束资源路径。

默认参数受到支持。调用方选择默认值时，调用参数列表需要包含 `..`。

```toka
fn test_val(x: i32 = 42) -> i32 { return x }

auto a = test_val(..)
```

引用返回可以用 effects 路由描述。

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

`@encap` 用于显式资源与可见性控制。持有资源的 Shape 应在 `impl Type@encap` 块中定义生命周期行为。

`@encap` 块中的典型生命周期方法包括 `fn drop(self#)` 和 `pub fn clone(self) = delete`。

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

unsafe 代码应尽量留在系统与 FFI 边界。公共 API 不应暴露裸指针，除非 API 名称明确表达 unsafe 或 raw 语义。

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
