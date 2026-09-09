# raw_take：被拒补丁原文（仅供审查，未应用）

后续状态：用户已审查历史差异并授权制作新候选；参见
[当前候选与验证记录](raw_take_lowering_candidate.md)。下文“当前”均指本材料归档时，
历史 `.patch` 原文保持不变，未被重新应用。

[完整补丁](raw_take_rejected_2026-09-07.patch) 原样保存上一轮被拒的
`apply_patch` 调用参数，共涉及八个文件，使用该工具的补丁格式，不是 `git diff`
格式。它针对当时尚未提交的工作树，不是可直接应用到当前工作树的候选补丁。
本次只保存审查材料，未执行其中的修改。

拒绝原因原文：

> 该实现把 raw_take 结果作为 owned rvalue 直接加载，却没有实现槽位退休、live-set 更新或 remainder 清理衔接，可能造成原槽位与结果的重复析构/所有权冲突；这超出用户批准的安全契约。

用户现已再次确认：容器 len/live-set/remainder 及原字节是否清零均不是 raw_take
lowering 的职责。该确认不代表逐行批准此补丁，也不替代系统要求的人类审批。

## 原始差异包含的内容

- AST 计划保存精确 SlotEdge，Sema 附着该指针。
- CodeGen 验证计划、类型与 AST edge 后，通过 genAddr + load 取得完整值。
- 未使用 raw_take 结果接入现有表达式语句清理。
- 四种测试 fault：missing、rejected、mismatch、incomplete。
- non-call preflight 读取已验证 raw_take 的元素类型。

## 审查时注意历史状态

- 当时 `CodeGen_RawTake.cpp` 尚不存在，因此补丁是 Add File。当前该文件是
  E0701 拒绝桩；当前表达式分派也已接入这个拒绝桩。
- 当时 CodeGen.h 已有 `m_RawTakeFault` / `m_RawTakeFaultConsumed` 字段；
  它们随后被删除，所以原文不含这两个字段的声明。
- 原文使用 `DiagID::ERR_CODEGEN_ERROR`，但当前实际诊断枚举是 `ERR_CODEGEN`
  （E0701）。为保留被拒差异原貌，这个错误没有在归档补丁中偷偷修正。
- 该 diff 未构建、未运行；不包含运行期 exact-once 或 fault-gate 测试。
  现有前端门禁仅证明类型检查／拒绝路径，不能替代这些验收。

后续如获逐项审查通过，应另制作针对当前工作树的最小候选差异并验证；
不能直接套用这份历史补丁。当前拒绝桩、Vec 和既有冻结 refs 均保持不动。
