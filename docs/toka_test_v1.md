# toka test v1 契约

状态：2026-10-02 采纳的 0.12 设计基线，尚未实现。规范用语“必须”对应
[验收矩阵](toka_test_v1_acceptance.md)中的用例；通过设计审查不等于通过实现验收。
版本范围与独立平台迁移见 [0.12 开发线](0_12_development_line.md)。

## C1 命令、项目与入口

```text
toka test [入口文件 ...] [--filter <字面子串>] [--allow-empty] [--json]
          [--compile-timeout-ms <正整数>] [--run-timeout-ms <正整数>]
```

`toka test --help` 单独使用时打印帮助、返回 0，不发现/执行测试，也不生成执行
报告。--help 与任何其他 test 参数同时出现为配置错误 2；有 --json 意图时仍
输出单一错误报告。除重复入口与 --filter 外，其他选项只能出现一次。

- 入口是有普通 `main` 的完整测试程序；退出 0 表示测试程序成功。v1 不引入
  测试宏、函数扫描、属性测试、覆盖率或新的语言入口。每个程序独立编译、运行。
- 从调用目录向上找最近的 `package.tk`，其目录的规范路径为项目根。找不到，
  或根目录不可读，返回配置错误 2。v1 一次处理一个项目，不自动遍历 workspace。
- 不给入口时，只在项目根的 `tests/` 下递归发现大小写敏感的 `*_test.tk`。
  `helper.tk`、`main.tk` 等不被隐式执行。目录中出现另一个 `package.tk` 时，
  该目录及其子树属于另一个项目，不进入默认发现，并记录 nested_project。
  隐藏目录没有额外排除规则，满足命名规则且不跨项目的文件仍发现。
- 给出一个或多个入口时，**替换**默认发现集合。显式入口必须是存在的普通
  `.tk` 文件，可以不匹配 `_test.tk`，不要求位于 `tests/` 中。跨越根内另一个
  package.tk 的入口也拒绝。目录、丢失文件、
  非 `.tk` 文件是配置错误；不能将其当作“无匹配”而用 `--allow-empty` 隐去。
  有效入口文件缺少 main 或 main 签名非法，由正常编译/链接诊断处理，结果为
  compile_failed/1；发现器不重新解释或扫描语言函数。
- 相对入口按调用目录解析，绝对入口也可用。规范化 `.`、`..` 后必须在项目根
  内；规范路径在根外则拒绝。入口及根内所经路径组件不得是符号链接。默认发现
  不跟随符号链接文件、目录，并在 `selection.excluded` 中记录 `symlink`。
  项目根本身按启动时解析的规范路径定位；这不授权入口中的链接穿越。
- 入口 ID 是规范化后的项目根相对路径，分隔符为 `/`。路径中的空格、Unicode
  字符按普通参数处理；不得通过 shell 字串拼接或按空白拆分文件列表。
  接受有效 UTF-8 路径；无法无损编码的路径返回配置错误 2，不悄悄替换字符；报告用 raw_input_base64 保留不可编码的原输入。
- 按 ID 的 UTF-8 字节序升序排序，再按规范路径去重。不同硬链接路径是不同
  入口，不按 inode 合并；同一文件的相对/绝对路径别名只执行一次。
  大小写不敏感宿主也以目录实际名称生成 ID，大小写路径别名不能制造第二项。
- `--filter` 是大小写敏感的 **字面子串**，只匹配入口 ID，不是 glob 或正则，
  不解析程序内容。重复 filter 取 OR；在入口规范化、去重之后应用，再排序。
  空 filter 拒绝；`*` 是普通字符。路径可放在 `--` 后以免被解析为选项。
  标量选项重复、未知选项或缺值返回 2。v1 不向测试程序透传任意 CLI 参数。
- 显式入口中有一个非法路径时，整个选择阶段失败，任何入口都不启动；报告仍
  记录已知输入与错误。默认发现遇到非链接的不可读子目录/候选文件也返回 2，
  不以忽略文件的方式制造成功。选定文件在编译前消失，归为基础设施错误。

`selection.mode` 为 `discovery` 或 `explicit`，分别记录过滤前候选数、过滤后
选中数、filter 列表和被排除路径。无 `tests/`、无命名匹配为 `no_tests`；有候选
但筛选后为空为 `no_matches`。二者默认返回 2，`--allow-empty` 返回 0 且结果为
`empty`：`total=0`、`passed=0`，不得报成“测试通过”。

映射：D01–D27、R11。

## C2 项目依赖、工具与运行环境

顺序固定为：解析选项/定位项目与报告目录 → 完成入口选择 → 非空集合的工具/监督
能力检查及身份记录 → 依赖准备 → 串行执行 → 最终报告。空集合在选择结束即报告，
不启动依赖 helper/编译器；允许为空也不借此证明工具或依赖有效。选择完成后的
准备错误须保留全部选中入口为 not_run。`--json` 意图独立识别，不能因前面的未知
选项使配置错误突然改成人类输出。只有 -- 分隔符之前的 --json 被视为模式
选项；分隔符后的同名文本是入口路径，不能偷偷切换报告模式。

编译必须复用 `build/check` 的项目上下文：同一 `package.tk`、锁定包映射、
package-node 身份、workspace 根及 SDK 定位。不得维护另一套测试专用解析器。

有依赖的项目必须有与 manifest 一致的有效 `package.lock`。缺失、损坏或
过期的 lock 返回配置错误 2，提示显式 `toka fetch`；`test` 不隐式生成/升级 lock。
无依赖项目允许无 lock。已锁定包缺少本地缓存时可以按锁文件获取原版本并校验
摘要；离线模式只使用已校验缓存。本地依赖也保持锁定节点身份。解析、获取或
校验失败返回 2，尚未开始编译的入口不启动。运行前后 lock 的字节必须一致；
该承诺约束工具自身，测试程序和外部进程主动修改项目文件不受保护。

SDK、编译器以及任何调度 helper 均须从安装 SDK 自定位，遵循已有显式覆盖规则；
不得回退到维护者源码目录。缺少 Python 等实际所需的宿主工具时，报告工具名称
及启动原因，返回 2；不要求用户补隐藏 `-I` 或 `TOKA_LIB` 路径。执行前保存工具
路径、版本、编译器摘要、runtime 摘要、lock 摘要和可确认的 SDK 身份；无法取得
必需身份返回 2。尚未到身份阶段则 identity.status=not_checked，
字段为 null，不另外编造“缺身份”错误；记录完成为 complete，记录失败为 failed。
SDK 源 SHA 无可靠记录时为 `null`，不得从当前工作树猜出。

编译、链接、测试的 cwd 均为项目根；从子目录调用不改变资源寻址规则。
stdin 接空设备，不能让 CI 等待交互。环境继承调用者环境，再加入项目运行目录
`TOKA_TEST_RUN_DIR` 和单项产物目录 `TOKA_TEST_CASE_DIR`；报告不保存整个环境或
秘密值。项目内串行运行，普通编译失败/运行失败/已成功清理的超时后继续下一项。
不存在隐式重试或“重新编译直到通过”。基础设施失败和用户中断停止后续调度。
共享依赖缓存的物化必须串行化写入或原子发布，不能让另一调用读到半写目录；
复用项目上下文不等于自动证明现有 helper 已并发安全。共享缓存写入权必须有可
释放的所有权，并在持有进程退出后释放；等待写入权采用 compile_ms 的独立上限，
超限返回 infrastructure_error/2，记录 dependencies.lock_wait_ms，不启动测试。

映射：P01–P14、R01–R14、A01–A05。

## C3 产物与调度基础

每次调用原子创建全新的 `.toka/test-runs/<不透明 run_id>/`，不得复用已有目录。
每项按排序位置使用独立目录（如 `000001/`），不把入口路径直接拼成产物文件名。
编译输出只写此目录；不得再使用项目根下的固定 `.toka_test_exe`、日志或脚本。
同时执行两次 CLI 的目录、可执行文件、日志和报告互不覆盖。
该隔离只涵盖工具管理的产物与安全发布的依赖缓存；不隔离测试程序主动写入的
项目文件、端口和外部服务。测试临时资源应使用 TOKA_TEST_CASE_DIR。

每项保存 `compile.stdout`、`compile.stderr`、`run.stdout`、`run.stderr` 原始字节，
包括非 UTF-8、无换行和大量输出；未开始的阶段没有伪造日志。日志读取必须避免
管道饥饿造成死锁。默认人类模式的实时输出逐阶段转发到 stderr；JSON 模式也只
能向 stderr 转发。报告记录文件路径和转发是否可用，不用控制台输出替代原日志。

v1 对成功、失败和中断调用均保留已创建的产物，报告位置明确；不增加自动淘汰策略。
磁盘创建、日志写入/关闭或最终报告持久化失败为基础设施错误 2。已有产物尽力
保留并注明不完整；不得仅因测试退出 0 就覆盖基础设施失败。

映射：A01–A06、J01–J07。

## C4 超时与受控进程范围

### 支持范围已确定

Linux x64、Linux ARM64、macOS ARM64 的 v1 采用 `posix_process_group` 后端；
尽力支持的 macOS x64 使用同一 Darwin 机制，其验证等级仍按平台政策记录。
工具身份检查、依赖准备等外部 helper 也使用同一监督边界。
每个编译/链接阶段和运行阶段均必须在开始实际执行前建立独立 session/process
组，避免把调用者 shell 放入取消范围。报告保存 leader PID、PGID、后端及监督范围。

**受控范围是直接子进程与仍属于该组的进程，不是任意“全部后代”。** 主动调用
`setsid`、改变组、通过外部服务另启进程，或改变权限而不再可监管的进程不在保证内。
这不是恶意程序沙箱。夹具和项目测试不得以逃离受控组的方式留下后台服务。
直接 child 的 `wait` 与组信号是不同义务；只发出信号不能记为清理成功。
Linux 的 [kill](https://man7.org/linux/man-pages/man2/kill.2.html)、
[setsid](https://man7.org/linux/man-pages/man2/setsid.2.html) 和
[Darwin kill 文档](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kill.2.html)
说明这些组操作的作用范围；v1 的确认策略如下，不能借 API 名称扩大承诺。

Windows GNU x64 保持源构建/dogfood 身份。**本版不承诺 Windows 的 managed test
后端**；非空选中集合没有该能力时，`toka test` 在启动入口前返回 2，原因
`unsupported_supervision`，报告 `backend=unsupported`。不得只取消 PID 而宣称
实现了进程组契约。未来 Windows 后端须另行验收其 Job 成员范围和退出确认；
[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
的成员规则也不等于任意后代都被监管。允许为空的调用无需启动监督后端，
仍按 C1 返回 empty。这一边界不是待实现时再选择的降级开关。

现有 `std/process` 的单个 `Child` 取消/等待能力本身不足以满足本节；本节定义
新增执行监督能力的验收条件，不声称现有 API 已经具备它。

### 超时触发与清理

- 超时使用单调时钟、每个阶段独立的 wall-time deadline。计时从发起该阶段的
  受控启动开始，含启动和等待，不含前置依赖准备。调度器必须能在 child 未退出时
  检查 deadline，不能在无限阻塞 wait 返回后才发现超时。身份检查工具调用使用
  compile_ms 的独立预算；身份工具超时是准备失败/2，不记测试 timed_out。
  依赖获取不计入 compile/run 预算，沿用 fetch 自身的
  网络超时/离线规则；外部 helper 仍必须响应中断并按同一机制清理。
  I2-A 增加调用级准备保护：依赖 context worker 为 **180000 ms**，native
  worker（native plan、pkg-config、宿主 C 编译及对象准备合计）为 **120000 ms**。
  它们是独立保守政策上限，不是 B0 推导值，不占用每项 compile_link/run 预算；
  触及保护上限且清理成功也返回基础设施错误 2，全部测试保持 not_run。
  原 fetch 网络超时和离线规则仍生效。native helper 内部工具继承同一个受控组，
  共享 native 保护预算；编译器身份探测则单独受 compile_ms 约束。
  本批没有增加用户可覆盖或无限准备预算选项。
- `--compile-timeout-ms` 和 `--run-timeout-ms` 接受正整数毫秒，最大
  `2147483647`；0、负数、非整数和越界返回 2。没有无限超时选项。
  编译器只提供一次编译+链接调用时，前者涵盖完整 `compile_link` 调用；如果后续
  能分别调用，其预算仍是该入口编译与链接的共享预算，不因拆阶段翻倍。
- B0 的数值建议为 **compile_ms=30000、run_ms=5000**，来源见
  [基线与推导](toka_test_v1_baseline.md)。机制、阶段含义和 CLI 覆盖方式不变；
  本批仅提交 B0 证据与契约修订，复审通过前不进入 I1，不标为实现冻结稿。
- 超时、中断或组内进程残留时，先停止启动新进程，向尚存在的受控组发送
  `SIGTERM`，最多给予 **2000 ms**；仍未确认退出则发送 `SIGKILL`，再最多等待
  **5000 ms**。这两个清理上限是 v1 固定值，与测试超时默认值分开。
- 成功确认必须同时满足：直接 child 已 wait/reap，受控组已消失（存在性探测得到
  `ESRCH`），以及本阶段捕获输出已排空并关闭。`EPERM`、无法确认组消失、wait
  失败或日志无法完成均为清理/基础设施错误；信号发送成功不够。僵尸/内核不可
  中断等待等导致超出确认上限，也明确返回 2，而不虚报全部退出。
- 必须保持监督对象的生命周期，避免组/PID 回收后重新指向别的调用。身份不再
  可信时，停止发信号并报告 2，不靠扫描系统进程或模糊 PID 列表“补杀”。
  需要发出的组信号必须在 leader 身份被回收前完成；最终 wait/reap 与组消失
  确认后不再向旧 PGID 发信号。I2-A 使用 waitid(WNOWAIT) 保留 leader 的
  wait 权，每次信号前重验 wait 权；失去身份时禁止继续发送信号。Linux /proc
  与 Darwin proc_listpids 的组成员读取只用于判断残留，且在 leader 尚未回收时
  使用；不向枚举所得 PID 发信号。实现须以独立监督对象/保留身份等机制满足这一
  条件，不能把已经 wait 回收的裸 PID 当作安全取消句柄。
- 普通结束也执行组边界检查。leader 正常退出但组内有存活进程时，同样按上述
  限额收尾；清理成功后该项为 `run_failed`、原因 `residual_process`，不是 passed。
  编译阶段出现这种残留也属于对应阶段失败，不继续运行该项。
- 超时与正常结束同一观察周期出现时，已确认的正常终态优先；否则触发 timeout。
  观察到的用户中断优先于该周期 timeout；已触发事件保留在报告中，不被后来的
  SIGKILL 状态覆盖。基础设施错误具有最高退出码优先级。

I2-A Preview 的完成边界：汇总和暂存回执写入后，屏蔽 SIGINT/SIGTERM 并收集
已观察/待处理信号，固定最终结果快照；边界前的中断统一改变调用结果，基础设施
错误 2 优先。最终回执与 CLI 使用同一快照；边界后的事件不再回写已完成结果。
暂存回执 finalized=false 不能作为完成结果，成功提交后 finalized=true。

用户中断指 runner 收到 SIGINT（不是测试自身收到信号）；runner 收到 SIGTERM
也按同一受控中断规则处理，保留其实际信号而不伪称 SIGINT。停止后续调度，完成
同一清理流程后返回 130；清理失败返回 2，同时保存 `trigger=interrupt` 和
`interrupt_signal`。再次 SIGINT 不跳过有界清理流程。runner 的 SIGKILL、机器断电
或无法写 stdout 不可能保证最终报告送达；这些非可控终止不得伪装为已完成报告。

映射：T01–T13、R05–R09、J05–J07。

## C5 结果、原始状态与退出码

| 情况 | 单项规范化结果 / 调用结果 | CLI 退出码 | 后续调度 |
| --- | --- | --- | --- |
| 所有项通过 | `passed` / `passed` | 0 | 完成 |
| 测试源码编译/链接失败 | `compile_failed` / `failed` | 1 | 继续其他项 |
| 测试非零、信号退出或成功清理的残留 | `run_failed` / `failed` | 1 | 继续其他项 |
| 编译/运行超时且清理成功 | `timed_out` / `failed` | 1 | 继续其他项 |
| 工具不能启动、无触发事件的编译器信号崩溃、调度/监督/日志/清理失败 | `infrastructure_error` / `infrastructure_error` | 2 | 停止 |
| 非法参数/项目/lock、默认空集合 | 无已运行项 / `configuration_error` | 2 | 不启动 |
| 空集合并 `--allow-empty` | 无已运行项 / `empty` | 0 | 不启动 |
| 用户中断且清理成功 | 活跃项 `interrupted` / `interrupted` | 130 | 停止 |
| 用户中断但清理失败 | 活跃项 `infrastructure_error` / `infrastructure_error` | 2 | 停止 |

汇总优先级：基础设施/配置错误 2 > 用户中断 130 > 测试失败 1 > 成功/允许为空 0。
先前失败不会覆盖后来的中断或基础设施错误；未调度的已选入口保留 `not_run` 和
停止原因，不算通过/失败，也不能从 total 中消失。工具启动错误必须通过受控启动/结构化失败类别识别，
不能靠 stderr 文本猜测；原始文本仍保存。编译器无 timeout/interrupt 触发却被信号
终止是基础设施错误 2，而不是用户编译失败。无法启动 linker 是 2，已启动
linker 报告符号缺失是 compile_failed/1；测试程序文件无法执行是 2。

原始状态独立记录：有正常退出才填 `exit_code`，被信号终止填 `signal`，未观察到
终态为 null；平台原生错误号单列 `os_error`。不能把 POSIX wait 的编码值当退出码，
也不能用 128+signal 替代原始信号。timeout 清理导致的 SIGKILL 是终态，timeout
仍是触发原因。没有运行阶段的项不得编造 run exit 0。

映射：R01–R14、T01–T13。

## C6 机器报告与诊断来源

`--json` 的 stdout 必须恰好是一份 UTF-8 JSON 对象，可带末尾换行，不带 ANSI、
banner、进度或子进程输出。人类模式保持简洁汇总；两种模式都在自己的 run 目录
原子保存相同含义的 `report.json`，部分输出只存在于临时文件，不称为最终报告。
可控配置错误、工具失败、timeout、中断均生成报告；即使创建 run 目录失败，
`--json` 仍从内存输出报告并记录 `artifact_root=null`。最终写入失败将退出码改为 2；
stdout 关闭等无法交付情况在可用 stderr 明确说明，不能承诺物理上必然送达。

报告 schema 为 `toka.test-report`，`version=1`；同一版本可增加可选字段，不改变
已定字段语义。消费者应忽略未知可选字段。下面各字段必须出现，null 含义固定：

| 字段 | 类型 / 含义 |
| --- | --- |
| `schema`, `version`, `run_id` | 固定 schema、整数 1、调用的唯一 ID |
| `result`, `reason`, `exit_code` | C5 调用结果；没有额外原因时 reason=null；最终 CLI 码 |
| `project_root`, `artifact_root` | 规范绝对路径；尚未定位/不能创建时 null |
| `identity` | status 与下述固定身份字段；not_checked 时字段为 null；真正失败的必需身份检查伴随错误 |
| `selection` | mode、filters、candidate_count、selected_count、excluded；发现未完成的计数为 null |
| `supervision` | backend=posix_process_group/unsupported/not_checked；scope=direct_child_and_process_group/none，不填 all_descendants |
| `timeouts` | compile_ms、run_ms、terminate_grace_ms=2000、kill_wait_ms=5000，含 default/CLI 来源；配置尚未解析的预算/source 为 null |
| `summary` | total、passed、failed、infrastructure_error、interrupted、not_run |
| `tests` | 每个已选入口一个结果，按 ID 排序，顺序不受终止时间影响 |
| `errors`, `diagnostics` | 调用级基础设施/配置错误和诊断数组；无错误时为空数组 |
| `termination` | 停调度原因与触发信号，见下文；没有停止事件时 reason=null |
| `timings` | C7 中调用级阶段与总耗时 |

termination 固定字段为 reason、phase、trigger、signal、cleanup；reason 为
configuration_error|infrastructure_error|interrupt 或 null，trigger 为
none|timeout|interrupt|residual_process，signal 是 runner 收到的实际中断信号或 null。
有受控 child 的准备阶段中断也在此处保存清理结果；尚无 child 时 cleanup.status=not_needed；没有停止事件时
reason/phase/signal/cleanup=null、trigger=none。cleanup 非 null 时使用与单项相同
的对象结构。普通单项失败继续调度不设调用级 termination，触发原因仍在各项中。
选择或准备阶段中断可 total=null 或全 not_run，仍通过 termination 追溯真正原因。

identity 固定字段为 status、tokac_path、tokac_version、tokac_sha256、
runtime_object_path、runtime_object_sha256、sdk_root、sdk_version、sdk_revision、
lock_path、lock_sha256；sdk_revision 无可靠记录时可为 null，缺少 lock 的合法
无依赖项目其 lock 两字段为 null。不要将源码目录 HEAD 写入 sdk_revision。

`summary.total` 为已知的选中数；选择尚未完成时为 null，tests=[]，计数均为 0。
选择完成后 total 是非负整数，等于 tests 长度，且
passed + failed + infrastructure_error + interrupted + not_run = total。failed 只包含
compile_failed、run_failed、timed_out；可同时存在先前 failed 与后来的 infrastructure
或 interrupted。允许为空必须 total=0、passed=0、result=empty。

每个 test 对象必须包含 `id`、`entry`、`result`、`reason`、`trigger`、
`interrupt_signal`、`phases`、`cleanup`、`logs`、`diagnostics`。trigger 为
`none|timeout|interrupt|residual_process`；启动失败等原因由 reason/errors 记录。
phases 是含 compile_link、compile、link、run 四个固定键的对象，另记录
compile_mode=combined|separate|unknown。combined 时只记录 compile_link，
compile/link 为 not_started；separate 时反之；还未建立编译边界时为 unknown。
每个 phase 记录 name、state（`completed|not_started|aborted`）、duration_ms、原始
exit_code/signal/os_error，以及 process（leader_pid、pgid、target_pid、
target_role）。target_role 为 compiler|linker|test|helper，未知目标 PID 为 null；
原始状态须属于实际执行目标，不把监督 helper 的退出状态当成测试退出状态。
未开始阶段计时、process 和原始状态均 null。
cleanup 记录 `status=not_needed|confirmed|failed|unconfirmed`、作用范围、请求的
信号、leader_reaped、group_absent、输出完成状态和 duration_ms；确认失败同时把
最终结果提升为 infrastructure_error。未启动 child 时 cleanup=not_needed。
logs 中每个路径为规范绝对路径或 null，必须只指向该调用实际创建的文件。

诊断至少包含 code（没有可靠代码时 null）、message、severity、phase、source。
severity 为 error|warning|note|unknown，不把任意 stderr 猜成某种严重级别。
source 包含 `origin=user|dependency|sdk|unknown`、规范 path 或 null、
`package_node_id` 或 null、`classification_basis=workspace_node|locked_package_node|sdk_root|none`。
依据是项目归属、已锁定节点或
SDK 身份。优先使用图中的准确归属；项目根路径前缀不能把已明确归属的依赖/SDK
文件标成 user。真正冲突的节点归属、多义路径、外部系统路径、无源位置或不能可靠归属时必须 unknown，
不能按报错文本/路径片段猜测。依赖摘要/解析失败也保留节点身份或 unknown。
原始 stderr 仍保存，不能因无法解析诊断而丢失根因。此字段是 #41 的共用契约。

#38 的范围筛选约束**语义证据输出**，不是测试入口 filter，也不是缩小语义分析：
默认证据 scope 为目标文件，显式决定/完整图查询分别记录 decision/all；报告必须
记录 kind、目标和是否发生输出筛选，完整检查的通过/失败与未筛选运行一致。
确切的 evidence CLI 参数在 #38 的交付中沿用现有接口设计，不增加 v1 test 选项。

映射：J01–J10、S01–S04、P09。

## C7 计时与实现前的数值门禁

所有 duration_ms 使用单调时钟、非负毫秒数值，不用日志时间戳相减；测量到的
极短阶段可以为 0，未开始阶段仍是 null。调用级记录 argument_parse、project、
artifact_setup、selection、identity、dependencies、execution、report_preparation、total；单项记录真实可
观测的 compile/link/run/cleanup 阶段。未开始阶段保留固定 phase 对象，
state=not_started；其耗时、进程和原始状态字段为 null，不把整个 phase 写成 null。

如果编译器一次完成编译与链接，记录 `compile_link` 的真实耗时，compile 和 link 保留固定 phase 对象，state=not_started，
duration_ms/process/exit_code/signal/os_error 均为 null，注明 combined。只有实际边界可观测时才分别记录 compile 和 link。
调用级 execution 包含单项等待、日志捕获和清理；计时存在包含关系，不能把所有
字段相加冒充总耗时。total 截止到最终报告序列化快照，不声称包含该报告最后一次
写入自身所耗时间；report_preparation 也止于该快照。报告注明这个测量边界。

B0 默认值建议：compile_ms=30000，run_ms=5000。依据为同脚本 SHA 的三个核心
平台各任务 5 冷＋5 热样本；规则和完整原始数据位置见
[基线记录](toka_test_v1_baseline.md)。它们是待复审的契约建议，不是尚未实现 SDK
中的配置。复审关闭 B0 后，I1/I2 才把这两个值接入 SDK；本设计包仍不标为最终
实现冻结稿。合法长测可用已有 CLI 参数显式覆盖，不能用此基线承诺任意大项目
或套件都将在默认值内完成。

映射：M01–M05、T10。
