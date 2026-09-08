# Thread A/B：环境交接与结果责任通道

Date: 2026-09-08
Status: Accepted for implementation — runtime and binding qualification still pending
Accepted revision: fdb938dae1b147cd156cb5b8d8de70d2625e1442
Acceptance date: 2026-09-08
Checkpoint basis: `9efb492ebc5418ec3c3af57c397c6f3252fec62e` plus unaccepted binding worktree

本稿只定义一个 native thread 的责任交接，不建立任务池、全局 registry、取消框架
或通用 executor。不改 capture 布局、dyn-fn 引用计数算法、raw_take 准入或 raw 权限。
下面的新增接口、句柄表示、错误策略和 compiler/runtime bridge 已获设计接受。
实现必须分别验证，设计接受不等于 runtime 或整个 binding 切片通过。

### 接受时补充的实施硬边界

1. 释放 W 必须是 trampoline 对控制块的最后一次访问；释放后不得读取状态、
   发出带 control 指针的测试回调，或访问从 control 借出的成员。
2. `move_out` 对合法 lease 不分配、不调用用户代码、不展开；领取结果后不能新增
   可恢复失败。typed move 是原责任的无回调交接，不是自定义 move/clone。
3. prepared/result lease 通过版本化私有 ABI 创建；NULL 是唯一 Empty 表示。
   输出槽由 caller 提供并初始化为空，opaque 对象由 runtime 分配并释放，
   具体入口与共享布局见 `lib/sys/toka_thread_handoff_v1.h`。
4. 新 compiler 接口启用时同步拒绝旧 TKI/缓存/runtime 混用，不延迟到最终发布。
   设计提交本身不改变接口键；独立 native 测试尚不构成 std/thread 的接口启用。

## 1. 选定方案及行为边界

采用 **每个控制块一个普通 mutex + 原生 join/detach**。不做无锁协议，也不先创建
detached thread 再用 condition variable 模拟 join：后者可能在 native thread 真正
退出前返回，会弱化现有 join 的完成边界。

- native `pthread_t` 保存在 C runtime 的控制块中，使用平台真实类型，不假设是 8 字节。
- creator/handle 和 worker 分别持有一个控制块责任引用；引用数受同一 mutex 保护。
- mutex 只保护状态、指针交换和引用数。**invoke、用户析构、typed move、分配／释放、
  native create/join/detach 都在锁外执行**。不持锁等待 worker。
- detach 不等待 worker 完成。若结果已经完成，detach 调用方可能执行用户析构；
  不承诺析构耗时有界，也不声称 detach 是 wait-free。
- 支持正常返回、准备失败和 native 错误返回。外部 `pthread_cancel`、用户直接调用
  `pthread_exit`、longjmp/跨 FFI unwind 不在首批范围；不得在这些路径伪造 Complete。
  语言 panic/析构展开若不能保证进入已验证清理路径，必须在 bridge 前拒绝或使用
  已有进程终止策略，不能让异常穿越 C callback。

POSIX 允许新线程在 creator 取得 ID 前开始执行；create 失败不创建新线程。
见 [pthread_create](https://pubs.opengroup.org/onlinepubs/000095399/functions/pthread_create.html)。
join 保留 native 完成语义，见 [pthread_join](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/pthread_join.html)。
detach 的 native 错误不能忽略，见 [pthread_detach](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_detach.html)。

## 2. A：真实环境与 typed 适配器

不再把 `ThreadStateBox` 冒充另一个 closure 的环境。编译器根据**已选中且已验证的
实际 callable/state**准备一个拥有型 `EnvLease`，其中包含真实 carrier／环境及
精确的 `EnvOps`。runtime 只保存 lease，不手工读 capture 偏移。

| 工件 | 必须来自的事实 | 消费规则 |
|---|---|---|
| EnvLease | 实际已初始化的 owned wrapper、真实 carrier、环境依赖、move/drop plans | 唯一责任；不复制，不暴露为普通 Addr 所有权 |
| `run_once(packet, result_storage)` | 精确调用签名、receiver mode、return lowering、完整 capture cleanup | 调用一次；正常结束时消费并释放 packet，完成 T 构造；不再调用 unstarted cleanup |
| `drop_unstarted(packet)` | 同一 packet 的已验证完整 Drop | 尚未启动时，清理其仍 live 的字段、环境及 allocation 一次；不调用用户函数 |
| ResultOps<T> | 完整实际 T、size/alignment、morphology、依赖、typed move/drop | 不用类型名、@Send 或“没有诊断”推导 |

`run_once` 是 compiler-generated C-signature adapter，不是将 Toka invoke pointer
强转为 `void *(*)(void *)`。它按既有 SRet/值返回规则执行真正的 typed 调用。
对 consuming callable，复用“消费 capture 后仅释放环境存储”的既有路径；对
repeatable callable 的一次拥有型包装调用，调用后按正常完整 Drop 清理。
不能额外调用 drop hook 导致 consuming capture 重复析构。

`thread_spawn_with_state` 的 state 与 entry 一起进入经 Sema 验证的拥有型 wrapper；
entry 即使来自 `fn` 也必须保留实际环境依赖，不能因 API 文案称其 non-capturing
就视为无环境。`thread_spawn` 保留实参真实 consuming mode，不向普通 callable 降级。

必要的编译器接线是一个**私有且有唯一 intrinsic identity 的 thread handoff 边界**：
Sema 绑定准确环境/调用/清理工件，CodeGen 消费 destination-matching validated plan。
不凭用户函数名识别，不为任意 raw pointer 授权；不用新增公开语法，不改变 capture
准入或布局。该接线是新增实施范围，不伪装成普通库改名或已实现能力。

### 类型擦除后的 T

ResultOps 使用永久存活的静态 descriptor，带版本、完整 canonical type/ABI identity、
size、alignment、`move_out`、`drop_live`。跨模块一致性使用受链接规则约束的语义
identity；不靠裸类型字符串、hash 相同或函数指针碰巧相等证明兼容。

- Sema 在擦除前验证实际环境与结果的依赖闭包及 @Send；两者是独立检查。
  结果不得借用将被 worker 清理的输入环境，或任何可能先结束的 creator 存储。
  静态来源须有真实 witness；未知 callable/raw/reference 依赖继续拒绝。
- 清理 thunk 使用完整 T，而非重建帽子、猜 sizeof 或跳过不可见字段。其机器码
  生命周期覆盖所有控制块及 result lease；首批不支持动态卸载该代码所在模块。
- `move_out` 只接受由同一完成协议发出的 live ResultLease，目标类型必须匹配。
  thunk 使用冻结 transfer/cleanup 规则，退休原结果槽，不 clone/retain，并交出
  原有责任；随后仅释放空存储。它不是通用 raw_take 放宽，也不是裸地址的安全 move。
- 正常 Sema 未证明时拒绝；carrier/descriptor 缺失、错误、未 qualification 时
  CodeGen E0701 且不产物。runtime 不重新推断 ownership/dependencies。

## 3. 控制块与寿命不变量

控制块包含 mutex、平台 `pthread_t`、native 状态、result 状态、EnvLease、对齐的
未初始化结果存储、ResultOps、引用数。结果存储在 create 前分配，分配失败不启动
worker；零尺寸 T 也有稳定 lease 身份及可用的最小分配，不代表有 Drop。

两维状态彼此独立，避免把“worker 已完成”误认为“native 已 reap”：

```text
Native: Starting | Open | Joining | Detaching | Joined | Detached | StartFailed
Result: Pending  | Ready | Taken | DiscardClaimed | Disposed
```

| 控制块责任 | 起点 | 终点 |
|---|---|---|
| H（creator → 唯一 handle／正在执行的 handle 操作） | create 前建立 | 成功 join/detach 的所有锁外清理结束后释放；失败操作仍保留 |
| W（预留 → worker） | create 前建立，尚无 worker 也计入 | worker 发布/丢弃结果、清理环境完成后释放；create 失败由 creator 撤销 |

起始引用数为 2。worker 即使在 create 返回前完成并释放 W，H 仍使控制块存活；
worker 不读取尚未发布的 tid。create 返回成功后 creator 才把 H 交给返回句柄。
失败时确认未启动 worker，撤销 W；EnvLease 归还准备者，清理未初始化结果存储和
控制块后返回错误。准备者在锁外销毁未交出的环境，输入不会恢复为可再次使用。

`JoinHandle` 不 Copy、不 Clone、不 Sync；其 `self#` 操作要求独占，移动只转移 H。
不支持 safe 代码从一个 handle 同时发起 join 和 detach；runtime 仍保留 Busy 门禁。
私有 native API 的调用者必须持有 H，不能让无引用的裸指针参与 retain 或等待锁。
未获准的 raw alias/stale pointer 不属于句柄 API 的内存安全承诺。

操作期间 H 留在操作本身，成功时先清空语言句柄槽，再运行锁外 cleanup，最后释放 H。
失效槽的重复调用只返回 Closed，不进入 native API。ResultLease 若交给 join 调用方，
拥有独立结果存储与静态 descriptor，**不依赖控制块存活**。

引用减至零时，必须已无环境、无控制块拥有的 live result、无进行中的 native 操作；
最后持有者在解锁后销毁 mutex 和控制块。这里没有用户析构。环境/result 的锁外
cleanup 任务各由 H 或 W 保活，不能先 release 再执行需要控制块的收尾。

## 4. 状态转换表

所有 reserve/commit/rollback 和 result lease 转移在 mutex 内；下列 native 调用及
cleanup 在锁外。Worker 允许在 Starting/Joining/Detaching 下完成，不能被这些状态阻塞。

| 事件及前置 | 锁内变化／预约 | 锁外动作与完成 |
|---|---|---|
| 准备成功 | Starting/Pending，H=W=1；准备者 EnvLease 暂停清理权 | create 使用控制块作为真实 C trampoline 参数；trampoline 从中取得匹配 EnvLease |
| create=0 | Starting→Open，保存 tid；Ready 可以早已存在 | 清空准备者 lease，返回持 H 的 handle；creator 不再访问 env |
| create!=0 | Starting→StartFailed，W 从未执行 | 撤销预留，归还 env；creator cleanup，返回 Create(code)，不返回伪 id=0 句柄 |
| worker 开始 | 从控制块独占领取 env，control.env 清空 | `run_once` 构造 T、清理实际输入及 packet，均不持锁 |
| worker 完成，Native≠Detached | Pending→Ready（完整 T 已构造） | 释放 W；Joining/Detaching 的未决操作不改变此规则 |
| worker 完成，Native=Detached | Pending→DiscardClaimed，独占移出结果 lease | worker typed drop + 释放结果存储；标记 Disposed，释放 W |
| join，Open | Open→Joining；H 保留，校验 expected ResultOps | `pthread_join(tid, NULL)`，不持锁；不通过 C 返回指针传 T |
| join native 失败 | Joining→Open；Result 保持 Pending 或已发生的 Ready | 返回 Join(code)，句柄不清空；不读取结果，不撤销 worker 的真实完成 |
| join native 成功 | 必须 Ready 且 env 已空；Joining→Joined，Ready→Taken；移出 lease | 清空 handle；typed move 到 Ok(T)，释放空结果存储；释放 H |
| detach，Open | Open→Detaching；H 保留 | `pthread_detach(tid)`，不等待 worker |
| detach native 失败 | Detaching→Open；不领取结果，不覆盖并发产生的 Ready | 返回 Detach(code)，原 handle 仍持 H，可 join 或重试 detach |
| detach native 成功，Pending | Detaching→Detached；句柄关闭 | 释放 H；W 保活并在完成时负责 T 的丢弃 |
| detach native 成功，Ready | Detaching→Detached，Ready→DiscardClaimed；移出 lease | 关闭句柄；调用方 typed drop + 释放存储，标记 Disposed，释放 H |
| 重复 join/detach | 空句柄→Closed；进行中→Busy（不调用 native） | 无状态/责任变化；成功后的重复 detach 不是静默成功 |

native 失败只回滚**操作预约**，不回滚 worker 并发发生的 Ready/env cleanup。
本层独占 native tid，不允许外部再直接 join/detach；对错误/失效 tid 的调用可能不
具备可恢复的平台语义，不能拿这些违规调用作为正常故障注入。

### 隐式 handle drop

空句柄为 no-op；活句柄复用相同的 detach 算法，成功时按上述规则领取/交出 T 清理权。
**提议：隐式 drop 的 detach 失败采用无分配 fatal 终止进程，且先释放内部锁。**
这是已接受的显式设计选择：没有错误返回位置，也没有获准的 orphan owner。
不能静默漏掉控制块，不能无限重试，不能阻塞 join，也不增设 reaper/registry 隐藏责任。
显式 detach/join 的 native 错误则始终返回并保留 handle。用户可处理/重试；若随后
仍让失败句柄隐式销毁，适用上述 fatal 政策。mutex 损坏等 runtime 不变量失败同样
fatal，不报告普通 Closed 或伪装成功。fatal 测试只断言确定终止，不声称进程终止
路径仍完成了用户 Drop 或普通 exact-once 收尾。

### 析构执行线程

| 资源 | 析构线程 |
|---|---|
| 准备/create 失败的输入和环境 | creator |
| 已启动调用的 state/captures/packet | worker |
| join 交出的 T | 最终拥有 T 的调用方，按正常作用域 |
| 早 detach／早 handle drop 的 T | worker |
| 晚 detach／晚 handle drop 的 Ready T | 执行 detach/drop 的线程 |
| 控制块空存储 | 最后释放 H/W 的线程；没有用户 cleanup |

不提供“析构总在创建线程”的承诺；实际类型的发送与析构线程要求必须满足既有
trait/生命周期契约，不允许把有线程亲和性的对象仅靠 raw cast 送入此路径。

## 5. 接口与表示的具体提案（不是当前 API）

### 公共 Toka 层

| 入口／表示 | 当前 | 提议 |
|---|---|---|
| `thread_spawn` / `thread_spawn_with_state` | 返回 JoinHandle，失败可能得到 id=0 | 返回 `Result<JoinHandle<T>, ThreadError>`；保持各自输入消费契约 |
| `join(self#)` | `Result<T, Error>`；提前清空 id | `Result<T, ThreadError>`；只在成功时关闭 |
| `detach(self#)` | 无返回值，忽略错误 | `Result<(), ThreadError>`；成功关闭，失败保留 |
| `JoinHandle<T>` | `id: Addr`，被当作 native tid | 私有 opaque control handle，唯一 H，完整 T identity 与 typed Drop；不公开裸 native id |
| handle drop | detach 错误被忽略 | 尝试非等待式 detach；错误 fatal（待裁定） |

`()` 表示 unit 语义，最终源码沿用现有 unit 拼写。ThreadError 提议为无分配的
`kind + native_code: i32`，kind = Allocation/Setup/Create/Join/Detach/Closed/Busy；
非 native 错误 code=0，native 错误原样保留，不经过可能失败的字符串分配。
描述文本可由独立显示方法生成，不参与责任交接。Descriptor/plan mismatch 不是
用户可忽略的线程运行错误：编译阶段拒绝，runtime 不变量破坏走 fatal。

这明确改变公共返回类型、错误类型、可构造性和 `id` 的含义，**不能因为仍可能
物理上一个指针宽就声称 ABI/接口未变**。不保留制造 invalid handle 的兼容入口。
调用点迁移应随该独立补丁列明；既有正例保留成功目标，不整体改成负例。

### 私有 C/runtime 层（建议新命名空间 `_v1`）

```c
// 结构只示责任；实际平台 ABI 由同一生成/校验描述约束。
typedef struct TokaThreadControl TokaThreadControl;
typedef struct TokaThreadPrepared TokaThreadPrepared; // owns EnvLease + static Ops
typedef struct TokaThreadResultLease TokaThreadResultLease; // typed live result, not plain pointer

int32_t toka_thread_prepare_v1(const TokaThreadEnvOpsV1 *ops, void *packet,
                              TokaThreadPrepared **out);
int32_t toka_thread_start_v1(TokaThreadPrepared **inout, TokaThreadControl **out,
                            int32_t *native_code);
int32_t toka_thread_join_v1(TokaThreadControl **inout, const TokaThreadResultOpsV1 *expected,
                           TokaThreadResultLease **out, int32_t *native_code);
int32_t toka_thread_detach_v1(TokaThreadControl **inout, int32_t *native_code);
void toka_thread_drop_handle_v1(TokaThreadControl **inout);
void toka_thread_dispose_prepared_v1(TokaThreadPrepared **inout);
void toka_thread_take_result_v1(TokaThreadResultLease **inout,
                                const TokaThreadResultOpsV1 *expected, void *destination);
void toka_thread_drop_result_v1(TokaThreadResultLease **inout);
```

- start 的 out 入参须空；成功清空 prepared、转移 H 给 out；失败 out 仍空，prepared
  恢复自身环境清理权。调用期间 prepared 不得被调用方销毁；worker 可能已完成。
  对应用程序而言 cede 输入已经消费，失败只在 std wrapper 中清理，不“复活”原变量。
- join 的 result out 必须空；失败 inout/out 均不转移责任；成功 inout 清空、out
  唯一拥有完整结果 lease。typed unwrap 移动后释放空存储；lease 若未 unwrap，必须
  可用同一 ResultOps 清理，不能是没人负责的裸 C out-pointer。
- detach 成功清空 inout；失败不清空。drop_handle 复用 detach 并落实 fatal 政策。
- C 返回值固定为 Ok=0、Allocation=1、Create=2、Join=3、Detach=4、Closed=5、Busy=6、Setup=7；
  `native_code` 在进入时清零，仅 native 错误写入原始非零错误码，避免混用 errno。
  Setup 保留 create 前 mutex 初始化等平台准备错误；不把它伪装为 create 已执行。
  未知返回码为 runtime 不变量失败，不降格为 Closed。
- dispose_prepared 仅允许 Ready 或 Empty；Starting 不能由调用方析构。drop_result
  使用其静态 typed Drop 后释放存储，清空 lease；Empty 是 no-op。正常 typed unwrap
  则由经验证的 `move_out` 退休源，释放仅剩存储，清空 lease，不能再 drop_live。
- runtime 内的 start trampoline 严格是 `void *(*)(void *)`，参数指向 control；
  它调用绑定的 C adapter，不直接调用/猜测原 Toka carrier 布局，正常返回 NULL。
- 原 `sys_thread_spawn/join/detach` 低层入口不静默改变语义；std 改用上述新协议。
  `unwrap_fat_pointer` 不再承担 std thread 的环境证明。不用全局 registry 转换 id。

新增 private runtime ABI、compiler-generated adapters 和公开 JoinHandle 合约已获
实施授权。按接受时补充的硬边界，新接口启用时就必须同时拒绝旧模块／缓存／runtime；
不得等到后续发布。设计接受不代表实现资格，具体改动和测试按增量记录。

## 6. 故障与竞争矩阵（未来门禁，不是已通过结果）

采用测试专用 native wrapper 和 barrier；不以无效地址、重复 native tid 操作或 sleep
触发失败。非测试构建无可用 fault 开关。每项记录 state/env/result Drop 次数、分配
释放次数及线程 ID；允许用户 cleanup 重入无关线程 API，以证明不持内部锁。

| 注入／调度 | 断言 |
|---|---|
| control/result/packet 分配失败、mutex 初始化失败 | 不启动 worker；prepared/输入 creator 清理一次；无初始化 T Drop |
| create 失败，native wrapper 不创建线程 | EnvLease 归 creator；无 worker invoke，输入各一次，H/W 无遗留，返回原错误 |
| barrier 强制 worker 在 create 返回前完成 | 控制块由 H 保活；结果 Ready；没有访问未发布 tid；之后 join/detach 都正常 |
| 正常 join + owning/unique/shared/已证明 callable T | 正确返回及 source retirement；worker 输入一次；结果直到最终 owner 销毁才 Drop |
| join 预约后 native 失败，worker 分别 Pending/Ready | Open 恢复，但不抹掉 Ready；handle 可重试或 detach；无结果提前读取/释放 |
| detach 预约后 native 失败，worker 分别 Pending/Ready | 无 discard lease；恢复 Open，结果责任仍完整；成功重试只有一个丢弃者 |
| detach 成功后 worker 完成 | worker 丢弃 T；H 已释放也不会使 control 提前释放 |
| worker 先完成后 detach 成功 | caller 领取唯一 discard lease；W 可先释放；caller cleanup 后释放 H |
| implicit drop 的早/晚完成 | 与 detach 两条路径相同；析构线程符合表，不等待 worker |
| implicit drop 的 native detach 失败 | 子进程确定 fatal，无死锁/重试循环；不把 fatal 当普通 exact-once 成功 |
| 重复 join/detach、closed handle drop | Closed/no-op，不再调用 native，不碰已释放 control |
| Busy / 操作重入、合法 handle move | 不产生第二个 H；不持锁调用用户代码；原 handle 无效，新 handle 责任完整 |
| join 结果 lease 未 unwrap 即提前退出 | typed T Drop 与存储 free 各一次；不能泄漏或让 control 随 lease 悬空 |
| typed descriptor错配、plan缺失、未完成specialization | 编译 E0701 不产物；运行时受信边界损坏 fatal，不调用错误 thunk |
| 隐藏 callable/raw/ref 依赖、返回借用 worker state | 擦除前拒绝，不因 @Send 或 descriptor 存在获得 static/owning 权限 |
| 地址、大小、对齐、零尺寸、多字段/enum结果 | 使用完整目标布局，正确 cleanup；不走 i8/首字段猜测回退 |
| 用户 invoke/Drop 重入、长耗时 Drop | 无锁内用户代码；detach 不等待 worker，但不虚报用户析构有界 |
| 正反跨模块/source-hidden TKI、旧新runtime混用 | 实际契约完整才接纳；混用稳定拒绝，不掉回旧 pthread pointer 路径 |

首批不实现 native cancellation/reaper/线程池/锁无关算法；不把不支持路径作为
通过的运行期用例。未知事实拒绝、所有 qualified 工件的精确匹配是独立门禁。

## 7. 本轮交付与待接受决定

本轮仅提交设计，不应用接口/句柄/ABI/同步实现，不改五个 string 方法或既有权限。
需审查的实质决定是：mutex + 原生 join/detach；唯一 H/W 责任；typed 环境/结果
adapter；返回 Result/ThreadError 与 opaque JoinHandle；隐式 detach 失败 fatal。
这些决定已经 Accepted，按共享 ABI 固定 → A/B 分别定向 → std 接线 →
定向故障矩阵实施。候选收敛后再跑一轮集成，不新增通用线程框架。
