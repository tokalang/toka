# Thread 环境责任交接：最小差异审查稿

Date: 2026-09-08
Status: Responsibility analysis complete; implementation not accepted or applied
Scope: thread environment handoff only. String/raw permission/capture layout remain frozen.

## 1. 当前代码的确切对象

`std/thread.tk` 中 `box_ptr` 是完整 `ThreadStateBox<S,T>` payload view，
`*box_ptr` 是 raw handle。两者不能互换。
`sys_thread_spawn(real_invoker, box_addr)` 发布的是 `box_addr`，不是
`tramp_fn` carrier 内记录的实际环境指针。

`genClosureExpr` 在独立环境中存储捕获；显式 `cede` capture 还会清空／取消原
binding 的清理。不能仅凭“单字段包裹通常布局相同”证明另一块 box 是这份环境。
因此不得先退休 box，再把旧地址当作活 capture；也不得改成 handle capture 后
继续把原 payload 地址传给相同的 invoke。

来源：`lib/std/thread.tk:77-111`、`src/CodeGen/CodeGen_Expr.cpp:8110-8230`。

## 2. 必须成立的责任表（目标，不是现有实现事实）

| 时刻／分支 | box、state、entry | 实际发布环境及 allocation | 返回 T |
|---|---|---|---|
| 构造成功、尚未发布 | creator 持有完整 initialized 对象的唯一清理责任 | creator 持有真实环境；native 借出的地址必须指向此活对象 | 尚未构造，无清理责任 |
| 正在调用 native spawn | 为可能立刻启动的 worker 预留唯一消费权；creator 不得同时清理 | 地址和所选 invoke 的实际环境类型／布局必须匹配 | 尚未构造 |
| spawn 成功 | creator 不再访问／清理交出的 payload；worker 只消费一次 | worker 完成环境内所有访问后释放；不能让含 reclaimer 的某次 move 提前释放仍在使用的环境 | worker 完整构造一次，再交给结果责任持有者 |
| spawn 失败且 worker 未启动 | 撤销 worker 的预留责任；creator 清理仍 live 的 state、entry、box 各一次 | creator 释放一次，不能只释放承载 function pointer 的临时存储 | 不构造、不析构 T |
| join 成功 | worker 的输入责任已结束 | 输入环境不再被 join 重复释放 | join 取得完整 T 后退休结果槽；随后仅释放空存储，将 T 交给 Result::Ok |
| join 失败 | 不虚构 worker 已完成 | 保留仍需等待／清理的责任；不能仅把 id 归零后丢失责任 | 未证明取得结果时，不读取、不释放结果槽 |
| detach 早于完成 | worker 仍拥有输入责任 | worker 完成后清理完整环境 | worker 丢弃完整结果并释放存储；不能将返回地址无人接收地遗失 |
| detach 晚于完成 | 输入已经清理 | 不重复清理输入 | 从已完成结果中唯一领取丢弃责任，与 join／worker 互斥 |

“spawn 成功后再转移”不允许存在 worker 已开始而 creator 仍可析构的窗口。
native create 成功／失败结果只决定已预留责任归属，不能靠时序假设延迟赋予责任。
此处讨论正常完成和 native 错误返回，不承诺从进程 abort 或 unsafe 违约恢复。

## 3. 当前缺口（静态核对；不冒充运行时反例）

1. `sys_thread_spawn` 在 create 非零返回时返回 0；std 调用方未分支处理，
   只释放 `tramp_buf` 存储。没有对应的 initialized box/state/allocation 失败清理。
2. worker 总是分配并返回 `T` 的地址。`JoinHandle::detach` 和 drop 只调用
   `sys_thread_detach`，没有结果责任通道；没有代码领取 detached `T` 的析构。
3. join 在调用 native join 前就把 `id` 归零；失败路径没有保存后续收尾能力。
4. join 的 `cede box_ret` 是 raw payload extraction，并没有因为 raw P 获权。
5. raw_take 首批依赖查询显式拒绝 fn/dyn fn、raw/reference 等未证明实例。
   整个 ThreadStateBox 同时有 entry callable 和 reclaimer，不能因 @Send、名称
   或 raw-pointer 可写而把它当作 dependency-free。见 `Sema_RawTake.cpp:99-133`。

## 4. 最小差异边界与尚缺的机制

### 不应制作的“一行修复”

- `auto captured_box = *box_ptr`：改变捕获对象，却不匹配发布地址。
- `raw_take box[0]` 后仍发布原 box 地址：发布已退休槽位。
- 仅在失败分支 `free(box_addr)`：泄漏／丢弃活 payload 的析构责任。
- 仅为 ThreadStateBox 加 dependency-free 白名单：伪造事实。
- 仅令 detach 清理当前结果指针：worker 尚未完成时存在竞争；晚完成结果仍泄漏。

### 推荐的最小完整分解（尚未授权为实现差异）

**A. 一个经过验证的 native 环境交接适配层。**
准备真实的、已通过 capture/dependency 检查的环境，保留准确 invoke 与 cleanup。
成功把同一环境的责任交给 worker，失败保留在 creator。不得手工猜测 capture
偏移；不改 capture 布局或引用计数算法。现有 raw_take 仅用于已经具备完整事实的
实际槽位，不通过增加类型白名单替代环境 export/transfer 的证明。

**B. 一个 join/detach 共用的结果责任通道。**
至少区分 RunningJoinable、ReadyJoinable、RunningDetached、Taken/Disposed；
结果发布与 detach 必须同步，保证恰好一个 T 消费者。保留非阻塞 detach，不能
悄悄改为 join。只读 `id` 和 pthread 返回值无法独自表达这个通道。

这会要求确定控制块如何由 JoinHandle 持有，以及 native adapter 如何取得完整
环境／清理契约。新增 runtime 入口或改变 JoinHandle 布局／id 的含义，都不是本轮
已获批的源码迁移，应先单独审查。不能为规避布局变更偷偷引入全局线程 registry。
原 pthread ABI可以保持不变，但这不足以自动授权新增 Toka/runtime 边界。

**C. 仅在 A/B 确定后做 std/thread 最小接线。**
删除独立 box 与薄闭包之间的布局冒充，添加 spawn 失败清理；join 用合法完整
payload extraction 退休结果槽，detach 领取结果丢弃责任。依赖未知的具体实例
继续拒绝；既有 callable 正例保持目标正例，不通过改负例或跳过来收口。

本轮不应用未闭合的实现补丁：当前没有一份仅增加 cede/raw_take、同时满足上述
三项责任与现有边界的可验证差异。需要的范围扩展明确为 A/B，而不是重新讨论
raw 可写权限、分配 API、capture 语义或通用动态 initialized-extent。

## 5. 实施后的定向验收矩阵

| 用例 | 必须观察 |
|---|---|
| 正常 state / callable，creator 栈覆盖 | worker 使用实际有效环境；输入资源各析构一次 |
| create 强制失败且不运行 callback | state/env/allocation creator-side exact-once；结果未构造 |
| worker 在 create 返回前完成 | 不提前释放、不双重消费，成功句柄可 join |
| join 的 owning 返回值 | T 从 live 槽取出一次；释放仅剩存储，调用方销毁 T 一次 |
| join 强制失败 | 不读取未取得的结果，不丢失后续合法清理责任 |
| detach before/after completion，隐式 drop | 使用同步屏障控制两个顺序；T 和输入均 exact-once；无 sleep 猜测 |
| callable/raw/ref 隐藏依赖、借用逃逸 | 缺失／不允许事实拒绝且不产物，不从 @Send 推断完整 |
| export/take 缺失或错计划 | E0701，不产物；CodeGen 不重建事实 |

故障注入须只用于测试构建／测试链接替身；不能通过无效指针触发 create 失败。
以上是验收要求，不是已经通过的计数。当前 normal thread 的第一阻断仍在 binding
初始化，不能运行上述 end-to-end 资格。未跑全量、未提交／推送、未移动冻结 refs。

## 6. 本轮已执行的隔离验证

- [callable slot 探针](thread_handoff_callable_slot_probe.tk)：类型检查和 object
  构建均以 `E04662 / ElementDependenciesUnproven` 拒绝；未生成 object。
  这确认现有机制的边界，不把目标线程能力改成永久负例。
- [result slot 对照](thread_handoff_result_slot_control.tk)：实际编译运行退出 0。
  从 raw 形参所指的已初始化 Token 槽取出完整值，`free[0]` 后结果仍可读取，
  结果作用域结束后 drop counter 恰为 1。这只证明可复用的局部交接，不证明
  native publication、callable 环境、detach 或通用泛型结果已经 qualified。
- `git diff --check` 通过。本轮仅新增本文与两个隔离探针，没有修改 thread、
  compiler、string 或 runtime 实现；既有未提交修改保留。
