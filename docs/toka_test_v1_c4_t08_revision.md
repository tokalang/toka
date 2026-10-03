# C4/T08：回收后 EPERM 的有界恢复修订

状态：独立契约修订，待复审。SDK 保持 Preview；I2-C 整体及 Q0 未通过。

此修订处理 I2C-LIFECYCLE-P2-1。旧冻结契约要求存在性探测遇到 EPERM
立即返回 infrastructure_error/2、停止后项。SDK `8341ab9b` 的现有实现允许
回收后的存在性探测有界重试。对正常 exit 0，两者可分别得到 2/not_run 与
0/passed；这是退出码、报告和调度政策的变化，不能称为原契约不变。

## 允许恢复的唯一边界

1. 直接 child 已由当前监督对象 wait/reap，原始退出码或信号已经保存。
2. 仅组存在性探测收到 EPERM；仅在原退出确认的 5000 ms 截止时间内重试。
   回收之前已消耗的确认时间仍计入，不创建新的预算，不延长超时。
3. 后续真实 OS 探测须明确返回 ESRCH；EPERM、无枚举成员或信号发送成功
   均不是消失确认。全部输出也必须排空并关闭。
4. 回收后禁止发送任何取消信号。身份失效不得扫描、猜测或补杀其他 PID。
5. 短暂探测失败保留在原始监督回执的 `cleanup.confirmation_probe_errors`，
   包含 errno 与消息；最终 C6 的 cleanup 记录实际 confirmed/failed。

持续 EPERM、未获得 ESRCH 至截止时间、身份失效、wait 失败、日志无法完成
以及回收前权限错误均返回基础设施错误 2 并停止后项。已有其他清理错误不
因后来 ESRCH 而恢复。基础设施错误优先于测试失败和中断，原原因继续保留。

## T08 变体与可观察结果

变体是 T08 行的细分，不新增或重编号现有 120 行验收台账。下面的受控注入
用真实 SDK 编译器和无副作用测试程序；它们不证明原 Darwin EPERM 的底层成因。

| 变体 | 观察条件 | CLI / 当前项 / 后项 |
| --- | --- | --- |
| normal-recovery 对照 | 正常退出，回收后一次 EPERM，再真实 ESRCH | 旧 SDK 2 / infrastructure_error / not_run；新 SDK 0 / passed / passed |
| timeout-recovery | 超时后回收，一次 EPERM，再真实 ESRCH | 1 / timed_out / passed；不能改成通过 |
| interrupt-recovery | SIGINT 后回收，一次 EPERM，再真实 ESRCH | 130 / interrupted / not_run |
| persistent-permission | 回收后的探测持续 EPERM | 2 / infrastructure_error / not_run；errno 1、cleanup failed |
| deadline-without-esrch | 在原截止时间内一直不能确认组消失 | 2 / infrastructure_error / not_run；无新预算 |
| identity-unavailable | 回收后监督身份不能安全确认 | 2 / infrastructure_error / not_run；不再发送信号 |
| permission-before-reap | 回收前组信号权限错误 | 2 / infrastructure_error / not_run；不适用探测重试例外 |

验收脚本 `tools/scripts/test_toka_test_eperm_policy.py` 分别加载旧 SDK
`1f83447b` 与冻结候选 `8341ab9b` 的安装 helper，记录原始阶段回执、C6
stdout/stderr、真实 ESRCH 探测、回收前后信号及后项启动标记。持续权限与
无 ESRCH 两项故障注入将固定确认预算缩为 100 ms 以稳定验证边界，明确
标记这项控制；不是用户可配置的清理预算，也不代表默认预算性能测量。

## 身份与发布边界

本增量只修订文档与对照脚本，不修改 SDK 实现。三平台仍复用生产运行
37089177117、attempt 1 的 `8341ab9b` 原包；已通过定向 CI 是运行
37090627500、attempt 1，原脚本 SHA `b047754b`。新对照脚本的 SHA 单列，
本机注入证据不冒充三平台新执行，也不触发九作业重跑。

三平台候选覆盖 70/70/69、开放 25/25/26 不因文档修订而增加；T08 的
全平台闭合仍按完整变体验收证据判断。首次失败记录、未知 EPERM 成因、
旧契约下的 2/not_run 以及并发归档失败均保持不变。独立复审通过前，本批
仍未 Accepted，Unicode/P01 与 macOS D27 继续独立开放。
