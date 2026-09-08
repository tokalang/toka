# Thread A/B 第一实现检查点

Date: 2026-09-08
Status: Implementation checkpoint — NOT runtime/source/binding Accepted
Design accepted: `fdb938dae1b147cd156cb5b8d8de70d2625e1442`

## 已实现与边界

1. 接受时的四项硬边界已纳入设计。共享版本化 C 描述位于
   `lib/sys/toka_thread_handoff_v1.h`：opaque prepared/control/result lease 均由
   runtime 创建／释放，NULL 为 Empty；输出槽和错误码义务明确；合法 take 无可恢复失败。
2. compiler interface 前置门禁已升级到 `0.9.9-18`；旧 `-17` TKI 及缓存不能被当作
   当前接口。native 使用 `toka_thread_require_compiler_0_9_9_18_v1` 强符号及完整 ABI key。
   **std/thread 尚未接入新协议**，不是将准备性版本门禁冒充整个新 API 已启用。
3. B 控制块为独立 POSIX C module：原生 pthread mutex/create/join/detach、H/W、
   prepared Starting 保护、独立 ResultLease、显式失败保留 handle、隐式失败 fatal。
   W release 是 trampoline 最后一次控制块访问；take 拒绝已知范围重叠。
4. A 是独立、未激活的 LLVM adapter emitter：精确输入 plan 检查、typed invocation、
   consuming/repeatable cleanup 分派、目标 DataLayout descriptor、纯 load/store move_out。
   **当前测试手工提供 synthetic qualified plans；真实 Sema producer 和既有 cleanup
   wrapper 的接线还没有完成。** 不宣告 A source qualification，不使用这些 synthetic
   facts 为任何普通 Toka 程序授予 authority。

没有修改 capture 布局、引用计数算法、raw_take 范围或五个 string 方法；没有用全局
registry 隐藏句柄变化。`toka_rt.c` / std/thread 暂不链接新模块或调用新接口。
公共 fatal/析构线程政策已写入 [实施预览](../../thread_handoff.md)。

## 定向验证

| 门禁 | 实际结果与证明范围 |
|---|---|
| B 独立 runtime | C11 `-Wall -Wextra -Werror`；28 个 lifecycle/layout/failure schedules + 14 个 fatal cases |
| B sanitizer | 同一矩阵经 AddressSanitizer + UndefinedBehaviorSanitizer 通过；不是 ThreadSanitizer 资格 |
| A 独立 emitter | 32 个 synthetic plan 拒绝且 module 不变；JIT repeatable/consuming/unstarted cleanup、真实 LLVM sret/by-address |
| A/B 物理 ABI 联测 | generated descriptors → 真实 B prepare/start/join/take，scalar/struct 通过；仍不是 Toka source/Sema qualification |
| 布局 | host C header offsets + 32-bit target descriptor 布局；没有宣称跨平台 native runtime 全部通过 |
| 版本 | stale TKI 两种不产物；stale cache 回源运行；source-hidden stale cache 拒绝；旧 runtime 强符号链接失败 |
| 既有增量 CTest | **6/7**，64.04 秒；失败见下，不宣称全绿 |

### 既有 call-shadow 集成阻断

扩跑 `toka_call_transfer_shadow_m1` 在 `tests/pass/g09_sync_condvar.tk` 中失败：

```text
lib/sys/sync.tk:120
auto ptr = libc_malloc(4 as usize) as Addr
auto *i_ptr# = (ptr as *i32#)
E04663 / KnownNullableSourceRequiresGuard
```

本轮没有改动该文件或 raw constructor 判定；新增 A/B 模块尚未进入当前编译器／
runtime 路径。此结果仍是实际集成阻断，不能沿用早前 9/9 的不同测试集合冒充全绿。
本轮不放宽规则、不改 oracle、不扩大到同步库迁移。

通过的六项是 generic-body qualification、CodeGen authority、dyn-fn lifecycle、
return-source、binding transfer、string migration。未运行全量 PASS/FAIL。

## 下一步（尚未完成）

- 真实 Sema handoff producer：实际 source/environment/result lifetime 与发送证明，
  同 revision/完整事务/实例 qualification，精确附着私有 intrinsic edge。
- 将既有 CodeGen carrier 清理真正接到 A 的 typed wrappers；禁止以测试 booleans
  代替此步骤。大结果 relocation 的各目标机器码资格尚未覆盖。
- 再将新 native module 接入 runtime 构建与打包，source-hidden TKI/旧缓存/旧 runtime
  联合拒绝测试必须随该启用提交，而非留到最终发布。
- 完成 std/thread 句柄／错误返回迁移与真实源程序的 create-failure、join/detach、
  exact-once；恢复 call-shadow 集成阻断后再申请整体资格。

独立测试脚本：`test_thread_handoff_runtime.py`、`test_thread_handoff_adapter.py`
与 `test_thread_handoff_compatibility.py`。本检查点阶段提交只收本轮新增实现和门禁，
不混入此前未验收 binding 工作树；不推送、不创建 PR/Actions、不移动冻结 refs。
