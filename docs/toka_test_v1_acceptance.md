# toka test v1 验收矩阵

状态：2026-10-02 设计用例，尚未执行实现验收。以
[主契约 C1–C7](toka_test_v1.md) 为唯一行为定义；版本与平台验收见
[0.12 开发线](0_12_development_line.md)。用例 ID 在实现批次中保持稳定。
每条用例须记录实际命令/输入、SDK/编译器身份、报告、原始日志、CLI 退出码和结果；
“有用例”不等于“用例通过”。不可运行的平台用例标为不适用并给出边界，不能记 pass。

## 小型可控夹具与公共断言

夹具使用普通 Toka 程序和已有语言机制，独立放在临时项目中，不依赖编译器源码树：

| 夹具 | 行为 |
| --- | --- |
| ok / print | 普通 main 成功；print 同时写 stdout/stderr，含 ANSI、无末尾换行及 Unicode |
| bad_source / bad_link | 确定的源码类型错误 / 已启动 linker 报告缺失符号 |
| exit_7 / exit_130 / signal | 正常退出 7 / 正常退出 130 / 由 POSIX 信号结束自身 |
| wait / child_wait | 有同步握手后等待；child_wait 创建同组子进程，双方等待外部终止 |
| residue | 主程序退出 0，留下已经完成握手的同组子进程 |
| escape | 隔离验收 harness 中创建逃离组的进程，由 harness 负责最终清理；不用产品保证替代 harness 责任 |
| dependency / resource | 使用一个固定发布依赖或本地依赖 / 按项目根相对路径读资源 |
| compiler_proxy | 固定身份的测试工具代理，按注入模式非零、信号退出、等待或拒绝启动；不伪称真实 SDK 资格 |

超时测试采用显式短 deadline 和 ready 通道，不靠易波动的 sleep 顺序制造状态。
清理/日志/等待失败由只在验收 harness 可用的故障注入 seam 制造；发行 CLI 不暴露
跳过清理或强制 pass 开关。harness 保存注入位置和预期触发，执行结束自查受控
进程与任何边界夹具都已回收，避免故障用例本身污染后续测试。

所有可控用例都断言：报告 schema/version、规范化结果与退出码一致，原始状态
没有被规范化覆盖，tests 顺序/ID 可复现，阶段 null/计数符合 C6。存在 run 目录时
检查 report.json 与 stdout JSON 的语义一致。只有 JSON 模式要求 stdout 单一报告。
下面“总计”指 summary.total；所有允许为空的用例同时断言 passed=0。

## 发现与选择（C1）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| D01 | tests/a_test.tk、tests/nested/b_test.tk 均 ok；无显式路径 | 发现并执行两项 | 0 | mode=discovery；候选/选中/total=2；passed=2 |
| D02 | 上述项目另加 helper.tk、main.tk、A_TEST.tk | 辅助模块/大小写不匹配文件不执行 | 0 | tests 仅含 D01 两项，不把辅助文件算 failed |
| D03 | 显式传 tests/helper.tk（有 main），默认集合另有 bad_source | 只执行显式 helper | 0 | mode=explicit；total=1；不混入默认项 |
| D04 | 显式两个入口，其中一个不在 tests/，另有默认入口 | 执行两个指定程序 | 0 | 只包含指定 ID |
| D05 | 三个入口，--filter nested/ | 字面子串筛选 | 0 | filters 原值保留；candidate_count=3；selected_count=1 |
| D06 | --filter a_ --filter b_，另有 c_test.tk | OR；不把重复匹配重复执行 | 0 | 选中 a、b，各一次 |
| D07 | --filter '*'，入口名无星号 | 星号不作为 glob；无匹配 | 2 | result=configuration_error；reason=no_matches；total=0 |
| D08 | 有候选，--filter 不存在的名称 | 不运行任何程序 | 2 | reason=no_matches；候选数非零；passed=0 |
| D09 | D08 加 --allow-empty | 显式允许空 | 0 | result=empty；total=0；passed=0 |
| D10 | 没有 tests/，或 tests/ 中只有 helper.tk；分别运行 | 无测试 | 2 | reason=no_tests；total=0 |
| D11 | D10 加 --allow-empty | 不伪造通过 | 0 | result=empty；total=0；passed=0 |
| D12 | 打乱文件创建和显式参数顺序；混用相对/绝对重复路径 | 顺序确定，规范路径去重 | 0 | ID 按 UTF-8 字节序；相对/绝对及大小写不敏感宿主的 case 别名只一项 |
| D13 | tests/space name_test.tk 和中文名；从有空格路径的项目启动 | 参数与路径不被 shell/空白拆分 | 0 | 完整 ID、日志路径有效，路径可无损往返 |
| D14 | 默认发现有符号链接文件/目录；同时有普通 ok 入口 | 不跟随链接，正常入口执行 | 0 | selection.excluded 含 symlink；链接不进入 tests |
| D15 | 显式符号链接或根内路径经过链接；再加 --allow-empty | 拒绝整个选择，不掩盖配置错误 | 2 | reason=invalid_entry；不启动任何入口 |
| D16 | 显式项目外路径、根外 ..、目录、缺失文件、非 .tk；分别运行 | 各为配置错误 | 2 | total=null；tests=[]；错误包含输入及规范路径（若可取得） |
| D17 | 从项目子目录调用；../tests/a_test.tk 仍在根内 | 正常执行；cwd 为根 | 0 | project_root 稳定，ID=tests/a_test.tk |
| D18 | tests/sub/package.tk 下有测试；外层也有 ok 测试 | 默认发现跳过嵌套项目 | 0 | 只执行外层入口；excluded 标明 nested_project |
| D19 | 显式选择根内另一个 package.tk 所属入口 | 拒绝使用当前项目图执行别的项目 | 2 | invalid_entry；无子进程 |
| D20 | tests/.hidden/x_test.tk，目录无 package.tk/链接 | 隐藏路径仍按同一规则发现 | 0 | 选中对应完整 ID |
| D21 | 一个合法入口和一个缺失显式入口，filter 只匹配合法项 | 先校验显式输入，不掩盖非法项 | 2 | 配置错误；所有入口未启动 |
| D22 | 命名匹配或显式指定的普通 .tk 文件缺 main/入口签名非法 | 交给编译器，不做函数扫描 | 1 | compile_failed；run 未开始；原始入口诊断保留 |
| D23 | toka test --help 单独使用 | 打印帮助，无执行报告 | 0 | 无执行/产物/子进程；不是 empty/pass 记录 |
| D24 | toka test --help --json；--json --json；--json --allow-empty --allow-empty，分别输入 | 配置错误仍按已知 JSON 模式输出 | 2 | 无测试执行；结构化 errors |
| D25a | -- 后传存在且有效的 -named_test.tk | 分隔符后只解释路径 | 0 | 选中正确文件，不当作选项 |
| D25b | toka test -- --json，--json 是无 .tk 后缀的入口名 | 拒绝非法入口，不切换输出模式 | 2 | 人类模式错误，report.json 仍存在 |
| D26 | 默认发现时注入普通目录/匹配文件不可读 | 不静默遗漏测试 | 2 | selection 未完成，total=null；错误带路径 |
| D27 | POSIX 显式输入不可无损 UTF-8 编码的路径；文件系统允许建立该名称时另测默认发现 | 明确拒绝，不替换名称；底层拒绝创建时只将发现变体标为前提不适用 | 显式/可运行发现 2；创建拒绝保留原 errno | 合法 UTF-8 单一 JSON、raw_input_base64 可还原；记录正常名称可创建及 FS/宿主；不得将未执行发现写成 passed；处置见 [收口说明](toka_test_v1_i2c_closure.md) |

## 项目依赖与安装 SDK（C2）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| P01 | 干净发布 SDK 经 PATH；项目使用一个锁定发布包 | 同 build/check 解析、编译并运行 | 0 | 节点/版本/摘要一致；lock 前后字节相同，无手工 -I |
| P02 | 锁定本地依赖；移动测试入口但不改项目根 | 本地映射正确 | 0 | lock 节点身份保留、cwd=root；无隐式升级 |
| P03 | 已校验缓存；开启项目既有离线模式 | 不访问网络 | 0 | 缓存身份一致；lock 摘要不变 |
| P04 | 离线且缺缓存 | 获取失败；任何入口不启动 | 2 | result=infrastructure_error；dependencies 原因和节点可追溯 |
| P05 | 有依赖但 lock 缺失/损坏/与 manifest 不符；分别运行 | 提示显式修复/fetch，不改 lock | 2 | 分别为 test.lock_missing / test.lock_invalid / test.lock_mismatch；configuration_error；context 准备退出 2；已选入口 not_run，编译/运行未启动；不得凭 fetch 文案判断身份 |
| P06 | 无依赖、无 lock 项目 | 允许运行 | 0 | identity.lock_sha256=null；不是锁定校验失败 |
| P07 | 原锁定包缓存缺失，在线可获取；registry 同时发布更新版本 | 只取原锁定版本 | 0 | 不选择更新版；lock 字节和摘要不变 |
| P08 | 下载/缓存包摘要与 lock 不符 | 校验失败，不运行入口 | 2 | dependency 身份/期望与实际摘要有记录；不重写 lock |
| P09 | compiler_proxy 分别输出来自已知用户/依赖/SDK/外部无归属位置的编译失败诊断 | 来源有依据或 unknown | 1 | source.origin、path、node、classification_basis；不靠文本猜测 |
| P10 | helper 或必需宿主工具缺失；无编译器源码目录可见 | 明确工具启动错误 | 2 | 不回退源码树；工具名称、原生错误及日志可追溯 |
| P11 | 注入必需编译器/runtime 摘要或版本读取失败 | 身份准备失败，不开始测试编译 | 2 | identity.status=failed；选中项全 not_run；无伪造 SHA |
| P12 | identity 工具等待超过 compile_ms，然后成功清理 | 准备超时归基础设施错误 | 2 | identity 阶段 timeout；不是测试 timed_out；选中项全 not_run |
| P13 | 两个 CLI 冷缓存并发获取同一锁定包；首个写入握手尚未发布 | 共享写入有序/原子发布；两者不读半写目录 | 0 | 双方节点/摘要正确；lock 未改；原始缓存状态可检查 |
| P14a | 注入共享缓存写入权占用超过 compile_ms | 等待调用明确错误，不调度入口 | 2 | dependencies.lock_wait_ms；全部选中项 not_run |
| P14b | 缓存写入持有者收到 SIGINT，后续调用在其确认退出后获取写入权 | 持有者清理退出，不留永久锁 | 130（持有者）；0（后续调用） | 中断与释放证据分别保留；后续节点/摘要正确 |

## 执行结果与退出码（C2、C5）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| R01 | 两个 ok 入口 | 全通过 | 0 | result=passed；passed=total=2；原始退出 0 |
| R02 | bad_source，然后 ok | 编译失败后继续；失败项不运行 | 1 | compile_failed、run=not_started、原始 compiler 非零；后项 passed |
| R03 | bad_link，然后 ok | 链接诊断属于测试编译失败 | 1 | compile_failed；phase=link 或 compile_link，按实际可观测阶段 |
| R04 | exit_7，然后 ok | 运行失败后继续 | 1 | run_failed；raw exit_code=7、signal=null；后项 passed |
| R05 | signal，然后 ok | 测试信号失败后继续 | 1 | run_failed；保留实际 signal，exit_code=null；不是 interrupted |
| R06 | compiler_proxy 等待，显式编译 timeout；后项 ok | 编译超时，清理成功后继续 | 1 | timed_out、trigger=timeout、phase=compile/compile_link；run 未开始 |
| R07 | wait，显式运行 timeout；后项 ok | 运行超时，清理成功后继续 | 1 | timed_out；原始终止信号单列；cleanup=confirmed |
| R08 | 编译器/已知 linker 无法启动；分别注入 | 基础设施失败、停止 | 2 | infrastructure_error；工具名、os_error；后项 not_run |
| R09 | 已编译测试程序无法启动/变为不可执行 | 基础设施失败、停止 | 2 | run 未成功启动；不能记测试非零退出 |
| R10 | 选定文件在发现后、编译前消失 | 调度失败，不降为源码错误 | 2 | 当前项 infrastructure_error；后项 not_run |
| R11 | 非项目目录、未知选项、缺值、重复标量、空 filter；分别运行 | 配置错误，保留报告 | 2 | total=null；errors 非空；无子进程 |
| R12 | exit_130 程序正常退出 | 普通运行失败，不是用户中断 | 1 | raw exit_code=130；trigger=none；无 interrupt_signal |
| R13 | 一项 compile_failed 后发生基础设施失败 | 错误优先级唯一 | 2 | failed=1、infrastructure_error=1、后项 not_run；total 不缩水 |
| R14 | compiler_proxy 被信号结束，未触发 timeout/interrupt | 工具异常终止，停止 | 2 | infrastructure_error；原始 signal 保留；不得归咎用户源代码 |

## 进程生命周期与故障注入（C4、C5）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| T01 | child_wait 在同组建好双端握手后超时 | 父/受控子退出，直接 child 被回收，组消失 | 1 | scope=direct_child_and_process_group；cleanup=confirmed；harness 无存活成员 |
| T02 | 同组子忽略 SIGTERM，随后 timeout | 2000 ms grace 后升级 SIGKILL，限额内确认 | 1 | 保存请求的两个信号与真实原始状态；不只检查 signal sent |
| T03 | 在活动运行阶段向 runner 发 SIGINT | 完成清理，停止后项 | 130 | interrupted、trigger=interrupt、interrupt_signal；后项 not_run |
| T04 | 编译/依赖准备/选择阶段 SIGINT；分别注入 | 已启动的受控进程按相同规则清理；其他阶段安全终止 | 130 | 活跃阶段及部分计时/termination 原因保留；不能伪造运行开始 |
| T05 | SIGINT 后注入持续组探测权限/wait/输出结束确认失败 | 保留中断原因，提升基础设施错误 | 2 | trigger=interrupt；result=infrastructure_error；cleanup failed/unconfirmed |
| T06 | timeout 后注入持续确认失败；后项有明显启动标记 | 停止调度，不启动后项 | 2 | timeout 原因仍在；后项 not_run；无其启动标记 |
| T07 | 正常 exit_0 后留下同组 residue | 成功收尾也不是测试通过 | 1 | run_failed、reason=residual_process、trigger=residual_process、raw exit=0 |
| T08 | 分别注入：回收后短暂 EPERM→真实 ESRCH；持续 EPERM；无 ESRCH 至截止；身份失效；回收前权限失败 | 仅首种在原确认预算内可恢复，回收后不发信号；其他情况清理失败 | 恢复保持原结果 0/1/130；失败 2 | errno 观察保留；正常恢复继续、timeout 保持失败并继续、interrupt 停止；失败后项 not_run；全部变体见 [C4/T08 修订](toka_test_v1_c4_t08_revision.md) |
| T09 | escape 离组并把 stdio 重定向到空设备，受控 main 退出 0；harness 持有逃离者回收责任 | 受控组正常结束；不声称监管组外程序 | 0 | scope 不声称 all_descendants；此例不作全后代清理证据，harness 另确认回收 |
| T10a | compile/run timeout 为 0、负数、小数、NaN、2147483648；各参数分别输入 | 非法值拒绝 | 2 | 配置错误；无子进程；不采用默认值 |
| T10b | wait 夹具，--run-timeout-ms 1000 | 有效预算在运行阶段触发 | 1 | timed_out；timeouts.run_ms=1000；来源为 CLI，cleanup=confirmed |
| T11a | 同一观察周期同时有已确认正常 exit 0 和 deadline | 已确认终态优先 | 0 | passed；trigger=none |
| T11b | deadline 到达，尚未确认终态，随后进程退出 | timeout 已触发，不能事后改成 pass | 1 | timed_out；cleanup=confirmed |
| T11c | 同一周期观察 SIGINT 和 deadline，尚无终态 | 中断优先 | 130 | trigger=interrupt；cleanup=confirmed |
| T11d | SIGINT 清理期间再发一次 SIGINT | 不跳过有界确认 | 130 | interrupted；保留中断记录，后项 not_run |
| T11e | 一项失败，后项 SIGINT，再注入清理失败 | 基础设施错误优先 | 2 | 保留先前 failed、interrupt 和清理失败 |
| T12 | Windows dogfood 或 POSIX 上无法建立受控组 | 不启动无监管测试 | 2 | unsupported_supervision 或 supervisor_start_failed；backend 如实记录 |
| T13 | 向活动 runner 发 SIGTERM；受控子不逃离组 | 同一有界中断清理流程 | 130 | trigger=interrupt；interrupt_signal=实际 SIGTERM；清理 confirmed；后项 not_run |

## 产物、资源和并发调用（C3）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| A01 | 同一项目同时启动两次 CLI，各有握手待运行入口 | run/case 目录及文件完全独立 | 0 | run_id 不同，产物路径不交叠；入口执行各一次 |
| A02 | 两个同名 basename、不同子目录的入口 | 产物不覆盖 | 0 | 各 ID 对应不同 case 目录 |
| A03 | bad_source、exit_7、timeout；执行结束后检查日志 | 失败原始日志保留 | 1 | 每个已开始阶段路径存在、内容匹配；未运行阶段为 null |
| A04 | 主项目有相对资源，从子目录调用 | cwd/root 规则稳定 | 0 | 读到同一资源；env 两个产物目录为各自实际位置 |
| A05 | 测试读 stdin，另有 env 读取夹具 | stdin EOF；只加入约定 env | 0 | 不挂起；报告不泄漏整个环境/秘密值 |
| A06 | 注入 mkdir、日志写入/关闭、report 持久化失败；分别运行 | 基础设施错误；保留可用产物和内存报告 | 2 | artifact_root 可为 null；日志标记不完整；不记 passed |

## JSON、诊断与报告完整性（C6）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| J01 | --json 配合 print 和编译器大量输出 | stdout 恰好单一 JSON；输出实时走 stderr 并存日志 | 0 | UTF-8 可解析、无 ANSI/banner/拼接 JSON；日志保留原始字节 |
| J02 | JSON 模式下逐一执行 R02、R04、R06、R08、R11、T03 | 可控失败和中断仍报告 | 1/2/130，按所选用例 | schema/version、原始/规范状态与真正退出码一致 |
| J03 | 选择后在第 2 项失败停调度，余 2 项 | 未运行项保留 | 2 | total=4；tests 长度=4；五类计数之和=4；not_run=2 |
| J04 | 编译失败诊断分别缺源位置、指向虚拟文件/未知系统路径/多义节点 | 来源为 unknown | 1 | unknown 不被用户/依赖/SDK 猜测替换；原 stderr 可追溯 |
| J05 | 超时清理导致 SIGKILL | 双重状态不丢失 | 1 | trigger=timeout；raw signal=实际信号；不是普通 run_failed |
| J06 | SIGINT 加清理故障 | 双重原因不丢失 | 2 | interrupt_signal 与基础设施错误同时存在 |
| J07 | 无法创建产物目录/最终 report 文件；stdout 仍可写 | 输出完整内存报告 | 2 | artifact_root/null 或 persistence_error；不宣称已保存报告 |
| J08 | 未知参数在 --json 前；选择尚未完成 | 仍按 JSON 错误协议输出 | 2 | 单一报告；total=null；tests=[]；计数均 0 |
| J09 | runner SIGKILL/断电模拟，或 stdout 提前关闭 | 不保证不可交付的最终报告 | 无完成码 / 尽力非零 | harness 标为 incomplete；不能读取部分文件当 completed/pass |
| J10 | ok 入口编译成功，同时产生来自已知用户/依赖/SDK 的 note/warning | 保留可靠来源、严重级别、位置与节点 | 0 | #41 字段完整；警告不改变测试成功退出规则 |

## 语义证据范围（#38，与 C6 共用边界）

实际命令为 `toka evidence <入口> --scope file|decision|all`，目标参数为
`--target <文件>` / `--decision <ID>`；详见 [I3-B 接口](toka_evidence_scope_v1.md)。这不能改变以下行为断言。这是证据输出范围，
不是 `toka test --filter` 的入口选择。

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| S01 | 合法小入口导入大 SDK/依赖图，分别请求目标文件和 all 证据 | 输出缩小，完整检查结果一致 | 两次均 0 | scope.kind/target/output_filtered；相关节点身份保留 |
| S02 | 错误位于依赖或跨文件决策链，筛选目标文件 | 不隐藏检查失败；保留直接相关错误证据 | 两次均非零 | 检查失败不因筛选变通过；source/node 可追溯 |
| S03 | 合法程序请求一项决策与 all，包含跨文件理由 | 决策的必要理由可追踪，其他无关记录不输出 | 两次均 0 | scope.kind=decision；目标/理由 ID 明确 |
| S04 | 无效 scope/目标/决策标识 | 参数错误，不回退成 all | 2 | 结构化配置错误，报告范围原输入 |

## 阶段计时（C7）

| ID | 输入/操作 | 预期行为 | 码 | 报告断言 |
| --- | --- | --- | --- | --- |
| M01 | ok 入口，能分别观测编译与链接的代理/实际管线 | 分别记录边界 | 0 | compile/link 均为真实非负耗时，标记 separate |
| M02 | ok 入口，tokac 单调用完成编译+链接 | 如实合并 | 0 | compile_link completed；compile/link 固定对象 state=not_started，耗时/进程/原始状态=null；combined |
| M03 | 分别复用 R02 编译失败、R08 启动失败、R13 停调度输入 | 未发生阶段无伪造计时 | R02=1；R08/R13=2 | run/not_run 的时长为 null，不填 0 |
| M04 | 等待夹具配显式短 timeout；注入墙钟回拨，单调时钟继续 | deadline 与时长不受墙钟影响 | 1 | 非负 monotonic 值，无负时长 |
| M05 | ok 入口与固定依赖的冷/热同任务多次执行 | 保存可复现样本与真实包含关系 | 各次 0 | 单项和调用级原始计时、宿主/身份/冷热定义、total 快照边界 |

## 依赖添加反馈（#39）

以下用例属于 I3，不把 `toka test` 的退出码规则施加到其他命令。add 沿用当前
成功 0、失败 1 的结果约定；机器报告接口在该项交付时固定，并记录实际命令。
人类摘要前缀固定为完整 SHA-256 的前 12 个小写十六进制字符，仅供展示；锁定
身份与机器字段始终使用完整摘要，不拿前缀做校验。

| ID | 输入/操作 | 预期行为 | 码 | 报告/输出断言 |
| --- | --- | --- | --- | --- |
| F01 | 固定 registry 夹具中 latest 解析为版本 0.1.2，执行 add | 显示真实锁定版本与已校验摘要前缀 | 0 | 输出 0.1.2 与 lock 的前 12 hex；不以 latest 冒充 resolved |
| F02 | add 指定版本，夹具中此版本不存在/摘要校验失败，分别运行 | 不打印成功解析摘要 | 1 | 原始根因保留；失败定位到包/版本或 unknown；manifest 回滚行为不回退 |
| F03 | F01 的机器输出模式 | 输出真实解析字段 | 0 | requested/resolved 明确区分；完整 digest、node 与 lock 一致 |
| F04 | 已解析本地依赖或带固定 revision 的依赖 | 显示其实际节点与适用身份，不伪造 registry 版本 | 0 | 本地/revision 身份与 lock 一致；缺少可用版本字段时明确 null/类型 |
| F05 | helper 成功退出但解析结果缺失/与 lock 不符的注入 | 拒绝伪造成功展示 | 1 | 结构化解析结果错误，原始 helper 输出可追溯 |

## 平台与发布政策验收（独立提交）

| ID | 输入/操作 | 预期行为 | 码/门禁 | 报告断言 |
| --- | --- | --- | --- | --- |
| V01 | 0.12 三个核心平台同 SHA 全部通过，Intel 未运行 | 核心资格可通过 | 资格 pass | Intel=not_run；不能写 Intel pass 或完整四平台 pass |
| V02 | 任一核心平台失败、缺失、dirty 或 SHA 不符 | 阻断发布 | 资格 fail | 错误定位目标；不能用 Intel pass 替代 |
| V03 | Intel 构建或基本重放失败/未运行 | 不阻断三个核心平台；不提供 Intel 二进制 | 核心可 pass，Intel failed/not_run | 资产集仅三包；可选平台失败/未运行原因保留 |
| V04 | Intel 基本验证通过，归档与同候选绑定 | 可纳入第四个尽力支持包 | 资产验证 pass | Intel 验证等级=basic；不是 full qualification |
| V05 | draft 有 Intel 包但无合格基本验证，或包摘要不符 | 拒绝创建/推广该资产集 | 验证 fail | 不默默丢包后仍称原资产集 verified |
| V06 | 三/四包有效集、缺任一核心包、重复或额外未知包；分别输入 | 只接受完整核心集和经过验证的可选包 | pass / fail | exact asset set、checksum、source run/attempt/policy 均绑定 |
| V07 | 安装器在 Linux x64/ARM64、macOS ARM64 选择对应发布包 | 安装与基本运行正常 | 0 | 下载固定版本/平台包并校验摘要 |
| V08 | Intel 用户请求缺少 Intel 包的明确版本 | 明确本版本无 Intel 二进制；不装 ARM 包/不静默退版本 | 非零 | 版本、平台、缺包原因及可用源构建指引 |
| V09 | 已发布 0.11 资格/推广回放验证 | 仍要求旧四平台和原 Intel replay | legacy pass / 缺失 fail | 旧版本不被新三平台政策追认或放宽 |
| V10 | 同 SHA 的已验证原包创建 draft，再推广 | 复用原字节，不重建；来源/重放/摘要不符则停止 | pass / fail | 固定 SHA、版本、run attempt、包含资产、policy、所有摘要一致 |
| V11 | Intel 基本包验证：安装、工具版本、创建、编译链接运行与小型 test 成功/失败/超时 | 验证实际包可用且监督结果正确 | basic pass / fail | 干净 SDK，无源码路径；退出码和原始状态准确 |
| V12 | 任意 optional 状态字段被写成 unknown 或漏报 | 不允许混淆未运行和通过 | policy fail | optional target 状态显式且合法；未运行/失败无资产 |

## 外部开发任务与实现前门禁

| ID | 输入/操作 | 预期行为 | 码/门禁 | 记录 |
| --- | --- | --- | --- | --- |
| B01 | 按 B0 对照 0.11 发布 SDK 与受控基线工具，冷/热固定任务 | 获得真实分阶段样本；不得把 preview test 当 v1 pass | 测量完整 / 不完整 | 固定 SHA/SDK 摘要、宿主、样本、原始命令、阶段边界 |
| B02 | 用 B01 数据制定 compile/run 默认值及依据 | 写回契约、固定 SDK 配置接入要求；复审通过再实现 | 前置 gate pass / blocked | 默认值、适用平台、裕量依据；覆盖方式不变 |
| B03 | 两个人或 AI 从相同干净快照完成 create/add/check/test/run/package | 独立 SDK 流程；一次失误不能被维护者手工补路径隐藏 | 项目验收 pass / fail | 完成率、修复轮次、冷热耗时、人工介入、原因分类 |

“package”指按真实项目既有交付方式形成可运行交付物，不在本版本自动承诺新增
通用 `toka package` 命令。真实项目集合、依赖/输入/期望输出与任务步骤须在测量
开始前固定；遇到失败如实记录，不临时换项目、改任务或删除失败样本。
