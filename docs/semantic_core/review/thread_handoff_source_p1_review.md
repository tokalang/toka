# Source producer：三个 P1 增量修复

Date: 2026-09-09
Status: Accepted — three P1 fixes and the documented private source subset only

Acceptance date: 2026-09-09. The independent review confirmed mode-ascription
rejection/parity, nominal result cleanup, a non-test Release build, and the
registered source CTest (1/1, 48.21 seconds). This is not acceptance of the
binding base, public std/thread, Stage 1, or release qualification. See the
[reproducible checkpoint boundary](thread_handoff_checkpoint_2026-09-09.md).

## 1. 实际 invoke/cleanup mode

不再仅从最终 callable 类型标注取得清理模式。Sema 从已检查的 closure invoke
声明取得实际 receiver mode，并保存该声明到 sealed source plan；CodeGen 再次检查
它与计划的模式一致。mode/signature 改变的标注分别报 ActualInvokeModeMismatch /
ActualInvokeSignatureMismatch，不把普通 invoke 交给 consuming 的 release(false)。

只沿当前函数中未修改、未失去来源的 local initializer/复制/转移链追溯；转换、
未知来源、后续 shadowing 或 stale initializer 不获权。追溯不重新执行 closure body，
不改 Parser、capture 规则或引用计数算法。合法同模式标注和多次转移保持可运行。

新增 mode_ascription 原形反例、结果类型改标注反例，以及保持模式的运行对照。
原审查文件 `mode_ascription.tk` 现以 ActualInvokeModeMismatch 拒绝。

## 2. Nominal/结构 Drop 事实

source producer 不再使用带 TimerHeap 等短名特判的 ownership 查询来判定结果 Drop。
清理责任与结果依赖图一起从实际 nominal declaration 的 HasExplicitDrop、完整
physical fields、enum payload、固定数组及 unique/shared 元素递归取得。
canonical owning-string 继续使用原已接受的声明 witness。未知或不完整结构仍拒绝。

新增私有测试入口 `__toka_thread_probe_run_drop<R,F>`：与正常 run 使用同一 producer
和 prepare/start/join，只是不 take 结果，而是调用已有 runtime drop_result 消费 lease。
没有新增 runtime ABI，也不改变 std/thread。它实际执行此前漏测的 typed `.drop` wrapper。

`result_drop_nominal.tk` 验证用户 TimerHeap 的 take 对照、runtime 直接丢弃、外层
Holder 的结构递归清理，均 exact-once；Plain 无资源对照不增加析构次数。

## 3. 非测试构建与 CTest

`threadHandoffSourceProbe` 的默认 false 声明移出测试宏，选项解析和 fault 注入仍只在
测试构建开放。独立目录 `/private/tmp/toka-thread-source-release.pi2Hj8` 使用
`BUILD_TESTING=OFF` + Release，完整 tokac 构建通过；普通源码检查通过，测试选项
被 unknown-option 拒绝。没有切换或覆盖原测试构建。

POSIX 源运行资格已注册为 CTest `toka_thread_handoff_source`；`ctest -N -R thread`
现在列出 1 项，实际执行 1/1 通过（48.09 秒）。源码脚本报告：

- 6 个 runtime/parity 正例；
- 13 个 source rejection/rollback 反例；
- 26 negative + 8 fault 不产物检查；
- private-disabled、borrow-check-disabled、missing-runtime 门禁。

`git diff --check` 通过。没有重跑全量 PASS/FAIL，没有改 A/B 核心、std/thread、
raw_take 或冻结权限规则。候选仍位于带有此前 binding 基底修改的工作树，不宣称
干净 revision 的整体 source/binding Accepted。以上是送审时的验证记录；接受后的
源码与未验收基底一同保存为检查点，接受范围不随提交文件范围扩大。
