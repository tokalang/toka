# Binding 收口：按需摘要与 raw H/P 诊断

Date: 2026-09-08
Status: On-demand summary increment implemented, pending review; raw authority design authorized, integration pending

后续状态：raw 可写契约已获独立授权；本文第 2 节保留当时诊断记录，不再表示待裁定。
当前实现、已授权 string 迁移与 thread 剩余阻断见
[unsafe raw 进展](unsafe_raw_construction_progress.md)。按需摘要仍需独立增量复审。

此前 recursive completeness / borrowed-record factory 已 Accepted；callable 参数 `#`
传递已通过复审。本轮没有重开这两项、owning-string、raw_take 或 enum 清理。

## 1. 按需准备已选 callee 的摘要

- 状态明确为 Unprepared / Preparing / Valid / Invalid；不存在用空摘要表示成功。
- binding / return 在提交前显式请求准备；读取环境 facts 的查询仍然只读。
- 新检查仅针对尚未检查的普通函数定义，使用声明所属模块的词法 symbols/types。
  caller 的局部变量、PAL、narrowing、flow ceiling、unsafe、控制流及 capture
  上下文均隔离并恢复；不把 caller 的能力带给 callee。
- 完成的普通函数体由缓存复用，后续 module walk 不再执行该体。Generic instance
  继续使用其真实实例化阶段的既有结果；没有脱离实例化作用域重跑 generic body。
  已检查的 synthetic closure invoke 也不会被当作普通冷函数重新执行。
- Preparing 上的递归依赖稳定拒绝；Invalid 缓存不重复检查，不获得绑定权限。
- 按需普通定义可在 generic caller 中准备。它自己的 observation permit 与 caller
  candidate 隔离；完成的定义 journal 单独保存。若 caller 的 argument journal
  回滚删除其记录，module walk 只补回缺失的原始记录，不重建 plan、不重跑 Sema。
  外部 Evidence schema 未改变，CodeGen 仍消费原 AST 上的 validated carrier。

[原声明顺序反例](callable_factory_order_gap.tk) 现已成功。
专项增加：正反顺序、资源 exact-once、同一 body 单份记录、普通／generic cache hit、
generic body 首次触发冷 factory、有效依赖链、无效依赖及其 cache hit、递归、
caller 局部与 unsafe 隔离、拒绝后的 source 回滚，以及定义 journal 的保留。

另保存了 [generic 结果类型反例](callable_generic_result_type_gap.tk)：`dyn fn() -> T`
的返回 ascription 当前报 TypeIncompatible。这尚未完成归因，不是正式负例，也不
宣称 generic/morphic 返回类型或整个 binding 已冻结。本轮的 generic cache-hit
门禁使用 concrete callable result，避免把摘要缓存与该类型问题混为一谈。

## 2. normal thread 权限差异（诊断完成，授权逻辑未修改）

`std/thread.tk:67` 的诊断事实完整：

| 表达式 | actual H/P | source flow H/P | destination H/P |
| --- | --- | --- | --- |
| `box_addr as *ThreadStateBox<...>` | 0 / 0 | 0 / 0 | 0 / 1 |
| 试验显式 `box_addr as *ThreadStateBox<...>#` | 0 / 0 | 0 / 0 | 0 / 1 |

第二行的 actual type 已带 `#`，但能力查询仍按原 address source 计算。
`getAccessCapability` 与只读查询都把 cast 视为透明展示；目标类型里的可写要求
不是已有 source 权限的证明。不能直接拿目标 P 覆盖 source flow。

上游接口事实也没有提供 typed writable storage：

```toka
// lib/sys/libc.tk
extern fn libc_malloc(size: usize) -> nul *void
// lib/sys/thread.tk
pub fn sys_alloc_thread_env(size: usize) -> Addr {
    unsafe { return libc_malloc(size) as Addr }
}
```

分配行为本身不等于当前模型已经证明“该 Addr 对应的特定 typed payload 可写”。
内存 effect 的 allocate 记录同样不是这份 exact-source H/P 契约。

现有 `permission_flow_two_mode_rfc.md` §4/§6 将 unsafe raw authority 明确放在
safe-flow 契约之外，且不批准 arbitrary raw-pointer upgrades；`syntax.md` 的
`unsafe addr as *i32` 示例只明确 caller 对非零地址的断言。因此不能把这些文本
当成“任意 Addr 的可写权限已经静态证明”。下一裁定可以复用现有 `as` 拼写，
不必新增关键字，但必须明确 unsafe raw 构造的前置条件与事实来源。

已增加 capability mismatch 的 note，显示 actual/destination 与双方 flow ceiling。
保留 `raw_cast_write_requirement.tk` 作为契约诊断探针；它不是新批准的永久语言
限制。`raw_cast_readonly.tk` 的只读 raw identity 构造编译并运行成功。
为诊断做过的 thread cast `#` 改动已撤回；本轮没有改变分配接口、raw 权限规则或 ABI。

继续实现前需要裁定最小边界之一：

1. 显式 `unsafe (addr as *T#)` 是否是调用方承担可写前置条件的 raw 构造契约，
   并在计划中记录 unsafe assumption，而非伪装成静态存储证明；或
2. 使用具有明确可写 raw-storage 返回契约的分配接口，并保留 nullable/权限检查。

未得到该裁定前保持拒绝，不凭 unsafe、Addr、类型要求或标准库位置放行。
两项都不需要引入动态 initialized-extent 系统，但不能把新增权限契约称为纯 facts 接线。

## 路线判断与验证

相对已接受的 explicit-cede RFC，方向没有偏离：修复的是声明顺序无关性和权限证明
链。相对“只增加调用点关键字”的原始想象，工程范围确已扩张；因此必须把必要的
接线修复与新的 unsafe 契约裁定分开，不能以恢复全绿为由不断扩展语义。

本轮增量工具构建通过；相关门禁 6/6（97.34 秒）以及历史观察门禁 2/2（50.84 秒）
通过，包含 pure planner、CodeGen authority、return-source、binding、Vec、value
dependencies、call-transfer shadow、generic-body qualification。Vec 18/18。
最后增加 synthetic invoke 不重检保护后，重新构建并补跑 binding 定向门禁。

未跑全量 PASS/FAIL；未开始第三步源码／诊断批量迁移。没有提交、推送、PR、Actions
或冻结 refs 写入。整个 binding 切片仍未 Accepted。
