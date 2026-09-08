# A/B 独立核心：三项增量修复

Date: 2026-09-08
Status: Implemented, pending independent review; no source activation

## 核心修改

1. **等价 ResultOps**：不再将 module-local `move_out/drop_live` 地址当作类型身份。
   保留双方完整 version/ABI/struct_size/non-null thunk 验证，以及 canonical type key、
   size、alignment 一致性。join/take 接受等价 descriptor，但执行始终使用 lease
   保存的 producer ops，不能用 consumer 提供的另一组函数替换清理行为。
   另一个 translation unit 提供同一 Value 的不同 thunk，分别覆盖 join、take、
   两者同时使用等价 descriptor；正常 move/drop exact-once，consumer thunk 未执行。
   不等价 type/size/alignment 和缺失 thunk 的 fatal 对照保留。
2. **packed packet**：该未激活核心现在明确拒绝 packed PacketType/CarrierType，
   输出 `ThreadHandoffPackedLayoutUnqualified`，在任何 module mutation 前结束。
   未扩展 unaligned by-address 参数 ABI，不给 offset=1 的 carrier 生成默认自然
   对齐 load。这是独立核心的资格边界，不宣布语言层 packed 类型永久不支持。
3. **fatal stderr 阻塞**：移除诊断 I/O 与 abort/用户信号处理器路径，直接 `_Exit(134)`。
   不分配、不刷新 stdio、不等待 stderr。fatal oracle 从 SIGABRT 改为明确的立即
   退出码；fatal 仍是进程终止，未变为成功或可恢复错误。公共预览同步说明无 fatal
   日志/core dump 保证、不承诺终止路径执行所有 Drop。

## 验证

- Runtime：31 lifecycle/layout/failure schedules + 14 fatal cases；增加堵满及关闭
  stderr 的 2 项有界退出测试。普通构建与 ASan/UBSan 均通过。
- Adapter：34 synthetic-plan 无 mutation 拒绝，包含原 offset=1 packed packet 与
  packed carrier；原 JIT/typed sret/目标布局/A+B runtime 联测均通过。
- Compatibility：旧 TKI／缓存／runtime 三类门禁保持通过。
- 本轮没有接真实 Sema，没有改 H/W 状态机、capture、引用计数或 raw_take。

## 独立 sys/sync 迁移

`sys_atomic_init_i32` 保留原 4 字节分配及 Addr 返回接口：将分配地址构造成
nullable `*[i32]#` 存储视图，只在**同一 handle**非空分支写入第 0 个 i32，返回
该 handle 的地址。失败返回 0，不读取／写入 null；新 raw 构造仍标记
`UnsafeCallerPrecondition`，没有新增 ownership/Drop 或静态可写证明。

使用单槽 storage view 是明确的第 0 元素访问，不通过普通 raw scalar binding
赋值的另一个 facts 缺口来放宽权限。没有改变分配尺寸、原子操作实现或其他 sync
构造器。普通 malloc Addr→non-null writable cast 的拒绝规则原样保留。

`test_sync_atomic_guard.py` 已验证 normal/shadow 诊断一致与 authority origin、实际
init/load/add/store/destroy，以及从真实生成 IR 中隔离原 helper、只替换其唯一
malloc 返回值为 NULL 的失败运行。此测试注入不替换进程 allocator，不改 compiler
或生产源码；它不是一次真实 OOM 事故。另有 nullable/readonly/PAL 的 6 个不产物
拒绝检查。此源码迁移单独提交，不与三项核心修复混在一起。

迁移后重新执行原受阻的 `toka_call_transfer_shadow_m1`：**1/1 通过，49.58 秒**。
这是该专项恢复，不是全量 PASS/FAIL、普通 std/thread 源程序或整个 binding 的资格。

所有既有未验收 binding 修改保留，未运行全量 PASS/FAIL，未推送或发起 PR/Actions。
