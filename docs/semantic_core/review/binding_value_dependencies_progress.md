# Binding 集成：owning string 与 borrowed str

Date: 2026-09-08
Status: Owning-string fact subpatch Accepted; borrowed binding integration in progress

后续递归审查修复及 callable 接线进展见
[binding_recursive_callable_progress.md](binding_recursive_callable_progress.md)。
下文保留上一轮验证记录，不将其冒充最新全量结果。

Enum 清理子补丁已按用户审查标记 Accepted；其上游声明解析／ABI 问题没有修复。
本轮没有修改 enum 清理、raw_take 契约／CodeGen 或 Vec remainder。

## Owning string 的类型事实

现有语言／编译器模型把 `string` 视为 compiler-recognized owned buffer。
不能把它内部拥有的 raw 字符缓冲区与 `str` 对外部字符存储的引用混为一谈。

新增只读类型事实查询 `hasCanonicalOwningStringStorage`：

- 仅接受 resolver 的 trusted toolchain `core/string` 模块中注册的确切 `string` 声明；
  同时匹配 declaration pointer、完整 nominal identity、零 generic arity。
- 要求已解析且一致的原生表示：nullable raw slice、标量元素、整数 length/capacity，
  以及该既有 owned-buffer 契约的清理声明。缺失／矛盾事实不猜测。
- 清理声明与表示检查只是完整性要求；不是“任何有 Drop 的类型都无依赖”，
  更不是匹配三字段 AST 结构就赋予 owner 语义。
- 借用视图、同名用户类型、普通 Drop-only raw holder 不获得该事实。

RawTake 和普通 call 的现有结构化依赖查询各接入该事实。没有改变原语的来源准入、
unsafe 初始化前置条件、Drop 责任、权限、生命周期或 CodeGen gate；也没有清除
实际的借用 referent。只有值本身的原生 owning-string 存储事实得到补齐。

## 定向证据

`toka_binding_value_dependencies` 验证：

- 2 个运行／normal-shadow 正例：字符串从 Vec 取出后跨容器析构存活并继续修改；
  alias 到 canonical string，以及包含它的泛型 Holder。
- 3 个拒绝／parity 反例：同名用户 string、有 Drop 但保留 raw 字段的 holder，
  同模板的 Holder<string> 与 Holder<str> 不能串用证明。
- 6 个 object/IR 无产物检查，零跳过。

它与 Vec gate 合跑 CTest 2/2（21.54 秒）。旧 string 拒绝行按这一个已补齐的事实
迁移为正向测试，没有批量修改 oracle。补上 object/IR 正向检查后的 Vec 脚本为
[18/18](owning_string_vec_report.json)，零跳过；borrowed str 拒绝行保留。

## 工具构建与下一个共同首错

一次 `toka-tools` 构建已经越过原来的 ElementDependenciesUnproven：
`toka` manager 生成成功，`--version` 运行返回 0，输出 `1.0.0-rc.13`。
整体 target 仍失败，因为 tokafmt 在 `tools/tokafmt/src/main.tk:541` 的
`Lexer::new(content.as_str())` 初始化缺少来源事实；Token 工厂调用有相同问题。
没有把 owning-string 事实授予 borrowed str 来绕过它。

两个独立最小反例已保存，本轮已恢复为成功，未伪装成合法负例：

- [borrowed factory](borrowed_binding_factory_gap.tk)：直接构造 Holder 有 actual owner
  root 并获准，经 Holder::new 返回的相同类型却丢掉 dependency roots，IncompleteFacts。
  [过滤后的实际 receipts](borrowed_binding_factory_gap.json) 保留该差异。
- [static borrowed field](borrowed_binding_static_gap.tk)：静态字符来源未传入聚合
  初始化计划，仍报 TemporaryTransferIneligible。静态字符与局部描述符存储不可混淆。

以上 JSON 是修复前的反例记录，不是当前门禁结果。

## Borrowed factory 来源子补丁（待独立审查）

- 初始化／整绑定赋值在正常 Sema 验证成功后，将 Structural factory 结果接入
  selected formal → actual 映射；未知 callee、未解析实参映射继续 fail-closed。
- 保留被选 factory 的 `MemberDependencies` 字段对应关系。整体 `<- argument`
  记录为空字段键（整体结果契约），不据此虚构每个结果字段的来源。
- 映射正式参数的字段投影时，读取实际 binding 已验证的 FieldDependencySet；
  不把 `.left` 追加到所有 underlying owner 的字符缓冲区上。
- 无动态根的字段可从已检查且未失效的 initializer 中取得确切的 static-storage
  witness。binding 已改写、无法定位确切字段时不沿用旧初始化证明。
- 同一聚合允许同时保留动态根和独立静态字段 witness。静态 borrowed record 使用
  `BorrowCapture + NoSourcePlace + NoLiability`；不把它变成独立 owned temporary。
  直接静态 view 则使用 `CopyIdentity`。没有修改 `str` 的 ownership。
- 仅有某个 borrowed 字段的证明，不能补齐其他 raw 字段的来源；未映射 raw 字段
  的聚合仍拒绝。局部 descriptor 的地址不继承字符字面量的静态生命周期。
- 计划及内部 non-call record 保留逐字段动态／静态来源，pure planner 检查这些来源
  必须属于整体已证明的 dependency roots / static witnesses。

新门禁覆盖 direct/factory、动态／静态／混合字段、交换字段映射、正式参数字段投影，
以及错误字段依赖、owner 局部逃逸、聚合／str descriptor 地址逃逸、unknown callable
和未映射 raw 字段。未改 Capture、CodeGen、原语、enum 清理或 Vec remainder。

最终增量验证（2026-09-08）：

- `toka-tools` 完整构建通过；tokafmt 实际读取并格式化 `factory_fields.tk` 返回 0，
  未写回该文件。仅保留原 lexer 未使用可写权限的 W0401。
- pure planner、Vec、value-dependency 三项门禁全部通过；Vec 18/18，无跳过。
- value-dependency 脚本保留 owning-string 原有 2 runtime / 3 rejection / 6 不产物检查；
  新增 factory 4 runtime+parity+evidence / 6 rejection+parity / 12 不产物检查，无跳过。
- 与原七项一起运行的相关 CTest 为 **4/10**，94.05 秒；不是全量 CTest 通过。
  其中原七项是 **1/7**，六个失败见下一节。
- `git diff --check` 通过。HEAD 仍为 `9efb492e`；实现尚未提交，工作树不是 clean。

## 原七项集成门禁的共同首错

本轮恢复 `toka_pure_nominal_overload_probe_d4a`。其余六项尚未通过，不能宣称 binding
切片 Accepted；本轮没有修改这些脚本的历史 oracle：

| 测试 | 当前首错／分类 |
| --- | --- |
| call-transfer shadow、remaining routes（两项） | `std/thread.tk:50` 的 `closure#()` 为 E04592；exclusive callable 可写形参事实缺口 |
| method parameters | 失败 initializer 已回滚 source；旧断言仍要求 E0438。normal 实际 E04510 + E0402，历史 replay 仍 E04510 + E0438 + E0410；需要区分新绑定事务与冻结的 method 历史协议 |
| indirect parameters | fixture 的 closure 返回 `cede ^cell` 先触发已激活 return-source 的 E04656，尚未到调用参数 E04570；属于 canonical unique-return 源码迁移，不是裸参数被接受 |
| return source | `make()` 返回 dyn fn 的已验证环境事实尚未接入绑定；E04661 IncompleteFacts。不能仅因返回类型或无诊断而认定环境独立 |
| return matrix | 未知引用重绑定在 assignment 处先以 E04661 AccessCapabilityMismatch 拒绝；历史 replay 到 return 时才报 E0455。仍为拒绝，不能简单改成编译成功 |

完整工具 target 已恢复；先完成本来源子补丁的增量审查，再按以上共同原因处理
集成门禁。不运行全量 PASS/FAIL，不提交／推送、不移动冻结 refs。

因此没有开始修订原 7 项 CTest 的 oracle，也没有运行全量 PASS/FAIL。
owning-string 事实子补丁已 Accepted；factory 来源子补丁仍待复审，整个 binding 切片不能冻结。
所有修改未提交、未推送，既有冻结 refs 未移动，未创建 PR 或触发 Actions。
