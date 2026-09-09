# raw_take lowering 当前候选

Status: Accepted — first-batch raw_take lowering only

用户独立复审接受：仅覆盖本文列出的来源、元素与门禁；不代表 binding 整体、
通用泛型 ABI 或 Vec 对接已经接受。普通函数／方法／consuming 参数接收结果的
独立 exact-once 补测也已通过。后续 Vec::pop 对接单列验证，不重开本原语。

本候选在当前 binding 工作树制作；没有应用历史被拒补丁，也没有提交／推送。
HEAD 仍为 `9efb492ebc5418ec3c3af57c397c6f3252fec62e`，不是本候选的冻结 SHA。

## Lowering

- `RawElementTakePlan` 绑定 slot/base/index AST edge、完整 storage/element/index 类型、
  Copy proof、result cleanup、非空证明和已接受的 unsafe 前置／后置责任。
- Production 正向表仅接纳 `CopyValue`、`MoveOwned`、`TransferShared`，并核对 Copy
  proof 与清理责任组合；`None`、`BorrowCapture`、`CopyIdentity`、`ConsumeTemporary`
  及未知枚举均拒绝。清理 disposition 也使用正向表。
- 不调用通用 `genAddr(slot)`。对首批已证明的 raw 变量基址，先读取 handle identity
  一次，再计算索引一次；只用已验证元素 LLVM 类型生成 GEP，不存在 i8 步长兜底。
- take 本体是完整值 load，不 retain/clone/free，不替容器写 len、清零槽位或维护
  remainder。结果进入现有绑定、直接丢弃、临时成员基址和提前退出的清理路径。
- CodeGen 使用 `ERR_CODEGEN`（E0701）。fault 字段、入口和注入仅存在于测试构建。

## 接入修正（限定新原语）

- `auto 'result = raw_take ...` 的已验证 unique/shared 结果，将完整 morphology 传给
  现有绑定推导；不会新增 binding 声明没有请求的可写权限。
- shared 的只读 flow ceiling 不禁止转移现有引用，但继续阻止结果获得 payload write。
- Evidence 的 source slot 保留 constant/dynamic index 视图，独立于 source-less 结果；
  initialized storage 仍明确标注 `UnsafeCallerPrecondition`，不是静态 extent 证明。

## 门禁范围

`tools/scripts/test_raw_take_frontend.py` 已由前端开发期围栏升级为原语专项，覆盖：

- 11 个拒绝场景的 normal/shadow 对照和不产物，包括 managed raw alias、活跃借用、
  权限提升、nullable 未收窄、未知元素依赖及旧 cede raw-index。
- 6 个实际运行程序：Copy、带 Drop 值、接收／直接丢弃／成员临时值／提前退出／
  remainder、concrete unique/shared exact-once、无 Drop 的 NonCopy 与非字节步长、
  现有 nullable 分支收窄。
- 13 类错计划 × 2 类元素 × object/IR：52 个 E0701 且无 artifact 的检查。
- IR 检查 base load → index call → typed GEP 的顺序；带副作用索引运行一次。

故意重复 take、未初始化槽位及错误 live count 不列为安全正例，也不以偶然运行
成功证明安全。fault 注入测试不等于支持了尚未证明的 source 或 dependency 路由。

## 独立发现与保守边界

测试发现同步 consuming morphic shared 形参的旧 lowering 不完整：签名接收
handle 地址，但初始化形参存储时只写一个 pointer，未构成完整 shared carrier。
原 raw_take roundtrip 实跑 SIGSEGV；[不含 raw_take 的独立反例](generic_shared_parameter_without_raw_take.tk)
在当前编译器和冻结 standalone 编译器中均编译成功、运行返回 1（期望 0）。

本候选没有修改该 ABI。新原语在这种尚未 qualification 的 specialization body 中
明确拒绝 `UnqualifiedSharedGenericParameter`，不把崩溃当通过，也不默许带错 carrier
的原语计划。该限制单独有不产物回归。concrete shared 形参／raw-storage 元素的接收
与丢弃均有独立运行正例；不应把它们的成功误读为 morphic shared 参数 ABI 已修复。

原语当前仍限制在可证明的 raw 变量基址；raw member/projection base 和未证明的
依赖不因此放行。Vec、TKI、ABI、interface key、既有冻结 refs 均未改变。
完整 binding 集成和全量 PASS/FAIL 不属于本轮候选验收结果。

## 最后验证记录

tokac 增量构建通过；定向 CTest 6/6（104.38 秒）：pure planner、dyn fn lifecycle、
standalone、binding、negative-termination、raw_take 原语专项。原语专项含上列
11 个拒绝场景、6 个运行程序和 52 个 fault/no-artifact 检查，无跳过。
这不是完整 CTest，更不是全量 PASS/FAIL 或四平台发布资格。

`git diff --check` 通过；无提交、推送、PR 或 Actions 触发，Vec 未修改。
