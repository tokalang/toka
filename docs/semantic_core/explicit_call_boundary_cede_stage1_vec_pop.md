# 最小 Vec::pop 对接：集成候选

Status: Blocked — not Accepted

2026-09-08 更新：用户另行授权的 enum owning-handle 清理修复已使本门禁恢复
17/17，见[修复候选](review/enum_payload_cleanup_candidate.md)和
[新结果](review/enum_cleanup_vec_report.json)。下文 14/17 和 Option 漏析构描述为
该独立修复之前的集成记录；候选仍待审计，整体 binding 集成问题未因此关闭。

raw_take 首批 lowering 已独立 Accepted。本轮不修改该原语、编译器、ABI、
TKI 或接口键；唯一库实现变更是 std/vec 的私有助手与 Vec::pop 接线。
所有改动仍未提交，既有冻结 refs 未移动。

## 已实现的责任交接

`Vec::pop` 把 raw handle 与可写长度交给私有 `vec_pop_raw_tail`；这里的 `length#`
是 Toka 现有的可写捕获契约，不新增 reference 或 unsafe 语法。

1. 长度为零时返回 None，不取出元素、不改变容量。
2. 长度非零时，检查 nullable storage；空地址在长度更新前 abort。
3. 计算 tail，随后把 live-prefix 长度改为 tail。
4. 立即把 `unsafe raw_take storage[tail]` 的完整临时结果交给 Some，再返回 outcome。

没有为 self.buf 添加 AST 白名单，没有重新放行旧 cede raw-index。
extent/remainder 仍由容器维护，不作为编译器已经证明的初始化事实。
IR 门禁验证检查、tail 准备、长度 store、raw_take 的顺序，并禁止 store→take
区间出现 call/invoke/callbr/coroutine 操作。

最初的具名 morphic 中间值会在后续 cede 转发时触及旧的 handle 解引用路径。
本轮只在库助手中改为直接传递 raw_take 临时结果，没有修订已接受原语或 ABI；
生成 IR 已确认原始完整 handle 直接写入 Some 的 payload。

## 初版定向验证：14/17 检查通过，零跳过

[完整结果](review/vec_pop_directed_report.json)。这里的 17 是检查项，不是 17 个运行程序。

- Copy、无 Drop 的 NonCopy、带析构的完整值：3 个 pop 运行程序通过。
  覆盖空／预留空容量、单元素、多元素 LIFO、取出值跨容器析构存活、直接丢弃、
  提前返回与 remainder exact-once。
- 4 个 pop 程序 normal/shadow 类型检查与诊断对照通过。
- string、str 的依赖尚未证明：2 个拒绝场景及 object/IR 无产物检查通过。
- IR 责任交接顺序检查通过。
- handle pop 运行失败；另外 2 个独立 Option 清理对照也失败，仍计为失败。

## 阻断：Option 的 owning-handle payload 被跳过清理

`CodeGen_Memory.cpp` 的 enum payload 清理按字符串首字符识别 pointer，并把
`^` / `~` 与 `*` / `&` 一起跳过。因此结果放入 Option 后，owning handle 不会
递归析构／释放。这不是 raw_take 的 remainder 责任，也不是前述 shared 泛型形参 ABI。

最小独立对照（都不含 Vec 或 raw_take）：

- [unique Option](../../tests/semantics/stage1_vec_pop/option_handle_cleanup_baseline.tk)
- [shared Option](../../tests/semantics/stage1_vec_pop/option_shared_cleanup_baseline.tk)

两份对照在当前及冻结 standalone 编译器中均编译成功、运行返回 100（析构计数 0，
期望返回 0、析构计数 1）。unique 对照在首个失败处退出；shared 单独对照验证 shared。
handle_pop 在直接临时值接线后同样返回 100。没有把泄漏视为 unsafe 调用方违约，
也没有把它改成成功 oracle。

初版 Vec 对接未修改通用 enum cleanup，随后已获授权制作独立 owning-handle 清理补丁：
保留 raw/reference 不拥有 pointee 的边界，不改 ABI、原语或动态 extent 规则。
独立补丁通过审计前，本 Vec 对接不能冻结或作为可发布实现。

## 一次集成检查

- [4 个原 Vec 阻断样本](review/vec_pop_sample_report.json)：3 个现在通过 check-only；
  `g08_for_alias_writable_vec_handles.tk` 推进到自身第 20 行 TypeIncompatible。
  这是 4 个样本，不是 259 项总体恢复统计，也不是完整 PASS 执行。
- [工具构建](review/vec_pop_tools_build.txt)：tokac／tokalsp 构建成功；toka、tokafmt
  因 Vec 助手中的 ElementDependenciesUnproven 失败。没有放宽元素依赖门禁。
- [此前失败的 7 个 CTest](review/vec_pop_failed_ctest.txt)：0/7，55.37 秒。
  仍涉及 thread callable、旧 rollback/诊断 oracle、间接参数、return/source 与
  nominal probe 等问题；未在本轮改动这些路由或更新旧断言。

实测尚未接纳 string、str；raw/reference/callable 或借用字段元素仍服从已冻结原语的
实际依赖证明要求。本轮没有扩展这些能力。某些拒绝发生在 Vec 实例化阶段（即使
用户尚未执行 pop），不能把这类路径描述为“容器已全面可用”。

初版候选没有收敛，因此没有启动一轮全量 PASS/FAIL 或四平台 CI。当时新门禁
`toka_stage1_vec_pop` 保持失败，未隐藏上述三个运行期失败项；新一轮实际运行
已经恢复 17/17，成功 oracle 未被降低。
