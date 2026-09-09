# Binding 增量：递归 raw 检查与 callable 事实

Date: 2026-09-08
Status: Implemented increment; pending independent review, binding not Accepted

后续复审已接受 recursive completeness / borrowed-record factory，并通过 callable
参数 `#` 传递；下面的记录是当时的增量证据。摘要声明顺序的后续修复与 raw 权限
诊断见 [binding_on_demand_summary_progress.md](binding_on_demand_summary_progress.md)。

Owning-string 子补丁保持此前 Accepted。进展文档末尾相反的过时描述已删除。

## 递归完整性修复

审查反例 `nested_raw_field.tk` 原来编译成功；当前原文件与仓库回归均在 binding
处报 E04661 / IncompleteFacts。没有改反例来回避嵌套来源问题。

- 从已解析的完整类型递归检查字段、数组元素、generic 实例及 enum payload。
- 仅进行只读解析和当前实例替换；未知事实、无法闭合的递归、未映射 raw 仍拒绝。
- canonical owning-string 沿用已接受的独立存储事实；不从 Drop 推导独立性。
- 已有 borrowed identity 仍必须经过实际来源收集器，不因 raw 检查通过获得来源证明。
- 补充直接嵌套、数组、多层泛型、enum 四个反例，以及合法嵌套 scalar 正例。

Factory 门禁现为 5 个 runtime/parity/evidence 正例、10 个 rejection/parity 反例、
20 个 object/IR 不产物检查；owning-string 原有门禁全部保留，零跳过。

## Callable 参数的真实可写声明

`closure#` 的 lexer token 已有 HasWrite，但 closure 参数解析没有将它保存到
Permission.SoulWritable，Sema 又沿用 injected type 的 IsWritable，丢失了真实声明。

本增量仅保存这个已有 token 标记，并从最终参数 Permission 同步 IsValueMutable；
没有新增语法，也没有放宽 E04592。`callback#` 正例实际运行两次成功，省略 `#`
仍报 E04592，normal/shadow 一致且拒绝不产物。

单独迁移 `std/thread` dummy helper 的 `auto _ = err` 为 `auto _ = cede err`；
仅补齐具名 source 转移，不改变求值或原绑定作用域的清理时机。

## 已验证的 callable-return 环境摘要（尚未完整收口）

- 在真实 return 检查时收集现有 callable 环境 facts；只有函数体、控制流和
  obligation 检查通过，且所有返回分支的环境事实完整时，才发布函数摘要。
- closure 自己的 return 不混入外层 factory 摘要；嵌套函数检查使用独立 frame。
- 保留实际 referents / parameter-local bounds，按 selected formal → actual 映射。
  分支合并保留依赖的并集，不因其中一个分支无依赖就声称整个结果独立。
- Invalid/Unchecked specialization 污染正在收集的摘要；generic cache 消费还必须
  通过持久 Valid 状态，不能只靠本次新增诊断。摘要不从返回类型或“无报错”生成。
- 缺失摘要、未知 callee、无法映射的 source-less borrowed argument 继续 fail-closed。
  没有增加 TKI 环境摘要协议，也没有猜测 source-hidden factory 的 capture 布局。
- 原 return-source 三个运行用例恢复，包含 owned capture 与共享副本的 exact-once。
  新增 borrowed/mixed-return 运行与证据检查；局部逃逸 E0455、无效函数体及无摘要
  调用拒绝仍保持。没有改 capture 准入、CodeGen、carrier 或引用计数算法。

**仍有明确的工程缺口：**
[声明顺序反例](callable_factory_order_gap.tk) 是合法源码，但 factory 在 caller 后面时
当前尚无已验证摘要，仍报 E04661。它是待修正例，不是可冻结的语言限制或正式负例。
不能通过返回类型猜测环境，也不能为了收口把它加入成功的拒绝矩阵；摘要准备／消费
的阶段接线尚需闭合。

## 本轮验证与仍未恢复的路径

增量 Debug 构建：`toka-tools`、pure planner 通过。
相关 CTest **7/7**，145.62 秒：

- pure planner；
- call-transfer shadow；
- return-source；
- binding-transfer（保留 21 个原 fixtures / disposition faults，新增 2 个运行与
  parity、3 个拒绝与 parity、6 个不产物检查）；
- Vec（18/18）；
- value-dependency；
- signature-driven remaining routes。

这些不是全量 CTest / PASS / FAIL。尤其 call-transfer 的冻结协议使用历史 replay，
其通过不能证明当前 normal 模式的整个 thread 路径已恢复。normal
`g09_thread_example.tk` 已越过 E04592，但当前在 `std/thread.tk:67` 的 raw pointer
初始化报 E04661 / AccessCapabilityMismatch；本轮没有扩大 raw 构造的权限授权。

此前 method / indirect / return-matrix 的源码／诊断迁移继续按共同首错分类，未批量
修改 oracle。所有修改仍未提交／推送；HEAD 为 9efb492e，未创建 PR、发起 Actions
或移动既有冻结 refs。
