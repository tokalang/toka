# Unsafe raw 可写构造：实现进展与范围阻断

Date: 2026-09-08
Status: Not Accepted; integration blocked, no release qualification

设计边界见 [短契约](../unsafe_raw_writable_construction_contract.md)。
这是一项新 unsafe 权限契约，不是静态可写证明，不改变 ownership / Drop / ABI。

## 已实现

- 沿既有 `as` 解析，Addr → 显式 writable raw target 且处于 unsafe 时生成独立计划。
- 计划来源为 `UnsafeCallerPrecondition`；Sema 验证类型与已知限制，CodeGen 只校验
  exact AST edge、source/target type、authority、nullable 与完整/拒绝状态。
- Addr 的普通值 H/P 不作为 pointee 权限。缺少目标 P 的普通 Addr cast 不因
  destination 或 Addr binding 上的 `#` 获得这份授权。
- readonly/frozen、已知来源及 nullable 信息经 casts、bindings、聚合、合流和
  checked function-return 映射保留；部分未解析组合继续拒绝。
- capture 只增加限制事实的传递，没有修改 capture 准入、布局、retain/transfer
  或析构算法。
- 新构造不增加 ownership、Drop liability、initialized extent 或安全借用保证。
- 缺失／错误计划在 CodeGen 入口拒绝；未使用表达式也不能跳过门禁。

## 验证时间线（不能混成“当前全绿”）

1. 独立基础矩阵曾通过：6 个 runtime/parity 正例、12 个 rejection/parity 反例，
   24 个拒绝不产物检查及 32 个 fault 不产物检查。正例包含真实分配写入、
   fallible allocation + guard、泛型包装及带 Drop 目标不被 raw pointer 析构。
2. 同阶段相关旧门禁 5/5 通过，覆盖 binding、raw_take、dyn fn lifecycle、return
   source、value dependencies。
3. 进一步明确普通非 Addr cast 不取得新增的可写构造授权后，发现 `core/string`
   既有代码用 `c_str() → usize → writable raw` 绕过了正常来源链。
   该轮相关 CTest 为 1/8；其余 7 项首先被同一标准库问题阻断。
   这是迁移前的结果，不能与下面新增的源码迁移验证混用。
4. 撤回该限制的补丁被自动审查拒绝，理由是会放宽安全边界。补丁未应用，
   没有更换工具或间接绕过，检查仍保留。

原生 C++ 构建、pure planner executable 与 `git diff --check` 仍通过。
未运行全量 PASS/FAIL，未提交／推送，未创建 PR 或发起 Actions。

## 已授权并实施的最小源码迁移

2026-09-08 用户将源码迁移授权扩展到下面五处 `core/string`，未扩展 raw 权限规则：

| 方法 | 当前行 | 现有问题 |
| --- | ---: | --- |
| `to_upper` | 546 | 从 `res.c_str()` 取 readonly view，经 usize 变成 writable raw |
| `to_lower` | 562 | 同上 |
| `to_ascii_lowercase_in_place` | 581 | 从 `self.c_str()` 取 readonly view 后用于写入 |
| `replace` | 600 | 同上 |
| `reverse` | 675 | 同上 |

不能只把 `usize` 换成 `Addr`，因为这样仍试图洗掉 `c_str()` 的 readonly 来源。
可审查的安全路线是使用 owning string 已有的真实声明：
`nul *#buf#: [char]`，并从可写 `res#` / `self#` 访问它，保留 nullable 与空串处理。
保留原有拷贝次数、长度、容量、字节输出及 exact-once；不改 owning-string 类型
事实，不放松 raw 构造检查，也不重新设计分配接口。

实际补丁只改五个方法，共 9 行新增／5 行删除：将五个整数 cast 改为
`res.*buf.unwrap()` / `self.*buf.unwrap()`；前三个非 in-place 复制方法及
in-place 方法在长度为零时直接返回，reverse 保持既有 `len <= 1` 分支。
解包保留 nullable 检查，没有无条件去掉 nullable。原构造、循环、字节变换、
返回与清理代码保持不变。新结果仍来自 `from_with_len`，只写新结果的缓冲区。

新增 `toka_string_buffer_migration`：normal/shadow rc、stderr 一致并实际运行；
覆盖默认 nullable 空串、已分配空串、ASCII 大小写边界、非 ASCII、替换命中／
未命中／相同字符、单字节与按字节反转、结果不改写输入、in-place 地址和容量
不变、重复调用及 64 轮独立清理作用域。没有用跨编码字符反转替代原字节算法。
重复运行作用域通过不等于分配器计数证明；不变的分配／清理路径同时由最小 diff
检查约束。本项不宣称整个 binding 或 thread 生命周期已经 Accepted。

### 迁移后的验证（当前结果）

- 相关 CTest **9/9**，99.78 秒：此前被 string 首错阻断的七项全部恢复，
  另含 CodeGen authority 和新增 string buffer 专项。没有跑全量 PASS/FAIL。
- 七项为 generic-body qualification、dyn fn lifecycle、return-source、binding
  transfer、raw_take、unsafe raw construction、binding value dependencies。
- unsafe raw construction 当前完整矩阵：6 runtime/parity、14 rejection/parity、
  28 negative no-artifact、32 fault no-artifact，全部通过，无跳过。
- `ExplicitAddrConstructionRequired` 检查仍在；本轮没有修改编译器权限实现。
- `git diff --check` 通过。未提交／推送，没有创建 PR、发起 Actions 或移动 refs。

## Thread 的状态

在普通 cast 收口检查加入前，已将对应 writable 构造接到
`unsafe_thread_pointer<'T>`：该私有 helper 只做显式 nullable raw 构造、现有 null
guard 和 non-null raw 构造，不分配、不拥有也不清理存储。现有分配 API 未改。

两份 normal thread 用例越过了原来的 AccessCapabilityMismatch，但随后停在：

```toka
auto captured_box = box_ptr // MissingCedeForNamedSource
```

这是 raw 指针后面的 owned payload 转移，不是可写权限。不能靠新 P 契约给它
ownership / Drop authority，也不能改 capture 布局来掩盖。因此线程 exact-once
尚未运行验证，整个 binding 切片依然不能冻结。string 迁移后重新检查
`tests/conformance/std/thread_spawn_owned_state.tk`，确认首错仍是这里的
`MissingCedeForNamedSource`，已不再是 core/string 的权限转换错误。
本轮没有修改 thread 的 payload/capture/ownership 路径，也没有补 `cede` 或将
它改成 `NoSourcePlace` 来绕过 raw storage 的转移责任。后续必须单独接通合法
raw-slot extraction 与线程环境责任交接，不能用 raw 可写契约代替。

在上述 9/9 后，实际构建同一线程 exact-once 用例仍以 E04661 拒绝，目标可执行文件
未生成；因而没有运行线程，也没有宣称 drop counter 为 1。该用例保持正式正例，
未改为预期失败、未跳过，仍作为下一步责任交接的硬门禁。

按需 callable 摘要修复仍保留其独立增量复审状态。
