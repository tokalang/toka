# toka test I2-B：C6 报告、来源与实时日志

状态：Preview，实现候选等待定向验收与独立复审。I2-A 修订 `54ca45a8` 已 Accepted；
I2-C 通过前不移除 Preview。正式报告协议与命令稳定发布是两个不同门禁。

## 固定报告与完成边界

所有调用使用 `toka.test-report`、version=1，固定 C6 顶层、identity、selection、
summary、termination、timings、tests/phases 字段。每项 compile_link/compile/link/run
均保留 phase 对象，未开始字段为 null；真实编译器一次编译/链接记录 combined。
compile_mode 位于 test 对象。五类计数之和等于已知 total；选择失败时 total=null。
`--json` stdout 只写一次 JSON，进度和子进程输出走 stderr；人类与 JSON 模式
均原子保存同义 report.json。Preview 内部 preview.json 仅作为 I2-A 回归/原始状态
回执继续保留，不是 C6 稳定报告，消费者应读 report.json。

汇总和暂存 preview 写入仍在提交边界前。屏蔽 SIGINT/SIGTERM、收集 pending 信号后
固定结果快照，最终 finalized=true；基础设施/配置/持久化错误 2 优先于中断 130。
边界后的信号不回写快照，实际持久化失败仍提升为 2。所有可控配置、准备、启动、
运行失败与中断都有报告；产物不可建时 JSON 从内存交付，artifact_root=null。
stdout 无法物理交付时尽力 stderr/非零，不把不可交付情况记为已交付。

## 可信来源（#41 共用接口）

编译调用使用已有 `--diagnostics-json` 协议，其文件、代码、严重级别来自编译器。
来源判断集中于 toka_test_report.source_origin：context worker 在 Resolver locked
验证与锁字节检查成功后输出 workspace node、锁定 package nodes 及实际根。
同一路径的锁定节点冲突、多义、SDK/依赖冲突、嵌套非当前项目、外部/虚拟/缺失
文件为 unknown。锁定依赖先于宽泛 workspace 路径前缀。SDK 分类要求本次
编译器与 runtime 身份检查成功；SDK revision 没有可靠发行记录时保持 null，
绝不使用源码 HEAD。普通 stderr 无论包含什么代码、路径或关键词均为 unknown，
不猜严重级别。原始二进制日志保留；JSON 文本不可解码字节只作显示替换。
非法 UTF-8 输入通过 raw_inputs_base64 保留。当前接入 test，不宣称其他 CLI 的
全部 #41 交付或整个 issue 已关闭。

## 真实 CLI 外层

原生 manager 在 exec Python 前检查必需 SDK helpers；Python exec 失败与 helper
缺失由原生 test_report 模块产生同一 C6 结构，完全不依赖 Python 或脚本 fallback。
身份/选择尚未检查时明确 not_checked/null、total=null，不能伪造测试通过。
外层通过 OS 的 physical cwd 验证 .toka/test-runs 与预期规范路径完全一致，
拒绝符号链接和不能确认的目录；用安全临时文件预留名称后独占创建 run 目录。
项目不存在或目录不能安全建立时交付内存 JSON、artifact_root=null。这是 bootstrap 失败路径，不扩大发现或监管范围。

## 日志与计时

监督器继续把子进程原始 stdout/stderr 写入独立普通文件，并在轮询期间分块转发。
转发每轮有界，stderr 写入使用非阻塞检查；转发失败作为基础设施错误，先有界
清理，不能让被阻塞的消费者停止 deadline 检查。原始捕获文件仍完整保留。
native worker 的内部工具管道在该 worker 的受控组内实时排空、保留并转发；
context/native 的独立保护上限和 compile_ms 锁等待边界不变。

compile/run 报告实际启动至退出/触发事件的耗时；cleanup 单独计时。调用级 execution
包含两者，不把分层时间相加冒充 total。9 个调用阶段按单调时钟实际边界记录；
未开始保持 null。total/report_preparation 止于最终序列化快照，最后一次自身文件
写入不计入它们。原生命令 bootstrap 可观测的 project/artifact/report/total 阶段
据实记录，普通执行阶段未开始；源码/SDK 身份不推断。

## 验收范围

新的 C6 控制与真实已安装 SDK 用例覆盖 stdout 单一 JSON、固定 phases/计数、
原始退出/信号、超时及 cleanup 分开计时、报告写失败、来源正负例、ANSI/二进制
日志、背压、配置/空集合和 Python/runner/report helper 缺失。保留 I1 与全部
21 组 I2-A 监督控制以及 17 个安装生命周期场景。

三个核心平台按同一候选执行定向任务；该证据不替代 I2-C，也不启动完整 Q0
发布资格。Unicode 原始失败与平台政策继续独立处理，不更改 0.11 发行资产。

## I2-B P2 修订

`4f97b231` 的归档核验通过，但尚未 I2-B Accepted。本次独立修订收闭三项：

- identity.status 固定为 not_checked/complete/failed；编译器/runtime 记录与
  context worker 的锁身份记录齐全后才 complete。消费者使用同一冻结枚举，
  验收独立拒绝 checked 等非契约值。
- PackageConfigurationError 明确标识缺失必需锁、锁格式/编码无效、锁与请求
  不匹配或过期等配置问题；worker 序列化 category，父调度器按类别传播，不查
  stderr 文本。下载/离线缓存、启动、监督、锁等待保护与清理故障保持基础设施
  错误。两者仍返回 2，全部未调度入口保持 not_run。
- 编译器结构化诊断仅从指定 compiler producer 的 compile/compile_link stdout
  解释。runtime/helper/probe 等其他发出阶段即使含相同 envelope，也只保存
  原始日志或 unknown 输出，不产生编译器代码、严重级别或来源事实。信任参数
  由调度路径提供，不从输出数据读取。producer 边界控制使用静态日志夹具，
  不运行冒充诊断的程序。

保持锁预算、结果提交边界、原始日志和 Preview 的回归。修订复审通过前不进入
I2-C；Unicode、平台承诺和完整发布矩阵仍为独立事项。
