# 真实 Sema producer 与清理 wrapper：工作树候选

Date: 2026-09-08
Status: Accepted private source subset only; public std/thread and binding pending
Prerequisite core accepted: `37efcf91`, sync migration accepted: `2876f03c`

2026-09-09 更新：实际 invoke mode、nominal/结构 Drop 与非测试构建的三个 P1 已作
增量修复并通过独立子集审查，见 [P1 复审记录](thread_handoff_source_p1_review.md)。
以下测试计数保留为首次候选历史，不代替最新记录。

本轮不改变 A/B 独立核心、capture 布局、引用计数算法或 raw_take；不替换 std/thread。
实现依赖当前仍未整体验收的 binding 工作树，验证结果不能冒充干净 HEAD 的发布资格。

## 源入口与证明生产

- 工具链模块 `core/intrinsics/thread_handoff_probe` 中的两个私有入口，由受信模块
  coordinate/origin 和解析后的 FunctionDecl 身份联合标记。相同用户函数名不获权。
- `--thread-handoff-source-probe` 仅在测试构建 CLI 开放；普通模式导入该模块报错。
  私有入口不是公共线程 API。它们的 `<R,F>` 都显式提供并与实际 callable/结果匹配。
  F 是消费型泛型值参数，保留实际 callable 的 consuming/shared mode；不通过 Parser
  将 `cede fn` 重新解释为普通 fn，不增加一般泛型参数的行为激活。
- Sema 的私有 checker 处理固定 intrinsic schema；不执行声明中的 unreachable 占位体。
  实参表达式、CedeExpr、closure capture/body、PAL、结果 return 检查仍走正常 Sema。
  检查真实源码，而不是由 C++ 测试填写资格 booleans。
- 在实参求值前捕获完整 AnalysisState；检查 arity/type/mode、活跃且已初始化的源、
  source authority、PAL、实际 capture sender summary、完整环境依赖和独立结果图。
  失败由调用级 guard 回滚；不能在丢失事实时以“没有诊断”代替独立证明。
- `ThreadHandoffSourcePlan` 构造函数及字段封闭，仅 Sema 生产，发布为 const carrier。
  CodeGen 检查 exact call/argument/declaration、实际 callable/结果和完成状态后才使用。

当前接纳当前函数内具名完整 dyn-fn binding 与可验证完整临时 closure；只支持
nullary shared/consuming。opaque 参数、projection、非本地 source、未知 factory
发送摘要、mutable callable、依赖不完整的结果仍拒绝。precompute/speculative context
及禁用 borrow checking 不授予资格。没有声称所有 generic/source-hidden 路由已完成。

Unit 的普通存储是 i8，但其 invoke ABI 是 void；当前冻结核心没有这个单独转换。
因此源层明确 `UnitResultABIUnqualified`，不能把 void invoke 当 i8 invoke 放行。
这不是 Unit 类型的永久语言限制，也不是通过删除正例宣告完整 std/thread 已完成。

## 清理及实际 ABI 接线

- named carrier 走既有 cede lowering；直接 closure 用既有 `emitDynFnClosureValue`
  物化真实 carrier。没有把另一个手工 box 冒充 closure environment。
- packet 的取消清理复用 `emitDynFnRelease(..., true)`；运行后，consuming 使用
  release(false)，repeatable 使用完整 release(true)。结果清理复用 `emitDropForType`。
- 使用与普通 callable/genFunction **相同的 `shouldReturnSRet`** 判定，不用 struct
  类型猜 ABI。源测试同时覆盖小 struct 值返回与 string 的实际 sret 返回。
- 私有 driver 将 prepared 交给真实版本化 C runtime：discard，或 start→join→take。
  源测试显式链接独立 runtime.o；没有将新接口偷偷接入旧 std/thread。
- driver 的 native 失败路径清理尚持有的 packet/prepared/handle 后 fatal；这只是
  编译器测试 driver，不冒充已迁移的公共 Result/ThreadError 接口。
- missing/source/result/cleanup fault 修改局部 plan 副本，再经过相同验证门禁；不是
  给 fault 标签无条件制造一个错误后宣称验证器通过。错误计划不产物。

## 定向验证

`test_thread_handoff_source.py --build-dir ...`：

- 4 runtime/parity fixtures：五个捕获资源、repeatable/consuming 的运行与放弃、保留
  副本仍可调用、value/unique/shared/string 返回、临时 closure、同名 spoof 不获权。
- 11 source rejection/rollback fixtures：裸传、R/F/mode/arity 不匹配、借用／未知环境、
  non-Send、opaque 参数、嵌套 capture 失败回滚、显式 cede 临时值、Unit ABI。
- 22 negative no-artifact 与 8 fault no-artifact 检查；关闭私有入口、关闭借用检查、
  缺少新 runtime 的门禁。
- 源程序 normal/private-profile 与 non-call-shadow 的 rc/stderr 保持一致。

相关增量 CTest 曾在本轮运行 **7/7，73.04 秒**：call-shadow、generic-body、CodeGen
authority、dyn-fn lifecycle、return-source、binding transfer、string migration。
未跑全量 PASS/FAIL，不宣称 std/thread、整个 binding 或跨平台发布 Accepted。

## 提交边界

AST/Sema/CodeGen/main/CMake 的共享文件同时带有未整体验收的 binding 实现。
接受后按用户授权将源码子集及其基底一起保存为可重现检查点，而不是把整个提交
标为 binding freeze。依赖、接受范围及复现命令见
[检查点说明](thread_handoff_checkpoint_2026-09-09.md)。
