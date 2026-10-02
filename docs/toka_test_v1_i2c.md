# I2-C 完整 Preview SDK 归档与干净安装

状态：Preview，等待固定候选的三平台定向验收和独立复审。不移除 Preview，
不启动 Q0。I2-B 修订 d6fe34ed 已 Accepted；首次并发失败仍作为原始观察保留。

## 夹具跟进

ignore-TERM 程序记录 entered、term_ignored、ready 的单调时间与 PID；就绪必须
发生在安装忽略 handler 之后。监督观察器记录 TERM/KILL 请求的实际时间，保存
每次原始 stdout/stderr、event 文件和 phase 状态。显式延迟用例覆盖 deadline
在就绪前到达（TERM）与就绪后到达（KILL）。timeout 仍从 launch 开始，不从
READY 重新计时，也不修改生产默认值。未达到夹具前提是夹具失败，不能重试到通过。

C6/监督中需要证明升级到 KILL 的用例将测试保护值设为 5000 ms，并核验就绪记录；
native 子工具夹具也使用独立 5000 ms 测试保护值以覆盖其真实子阶段。生产 native
保护仍为 120000 ms、context 为 180000 ms。每个 source control 结束时按指定
证据目录复制所有临时原始文件，失败也留存。三组控制并发运行，不能只保留顺序绿灯。

独立复审的首次 signal=15 记录、隔离 signal=9 观察和顺序通过记录原样保存；
首次 raw phase 日志不可恢复，因此启动时序只是历史原因假设，不标成已证实。
本轮另一次 native-ready 前提失败拥有原始日志，单独保留，不混成历史信号失败。

## 固定完整归档

package_test_i2c_sdk 构造完整组合 Preview SDK：冻结 0.11.0 原包（source 57b0f7dd）
提供 tokac、tokafmt、tokalsp、runtime、标准库和旧文档；本次固定源码提供 manager
和四个 project helpers。所有文件标明来源 SHA、SHA-256 和模式，preview-sdk.json
绑定整体候选、原包摘要及组件清单。不能宣称所有二进制都从新 SHA 重建。

归档含完整工具/库，没有隐藏的源码树取件步骤，明确标为 composite Preview，
工具基础版本仍为 0.11.0，不冒充 0.12 公开发行。编译 manager 产生的额外缓存
移除；非替换的基线文件恢复到冻结原字节。归档头部时间/UID 固定，字节摘要留存。
原公开 SDK、标签和版本资产不变。

构建与安装是独立 runner/job。安装只下载同运行提供的实际 SDK 归档与身份文件，
先核验归档和每个组件，再在全新目录解包，清除 TOKA 源码覆盖环境，用真实已安装
CLI 执行 I1/I2 回归和额外发现/选择/失败场景。最后核验归档中已有组件字节未变。
测试 harness 在 consumer 目录运行；用户程序不借编译器 checkout、补 -I 或补库路径。

## 验收映射与限制

map_toka_test_i2c_acceptance 对原矩阵的 120 个编号逐条保留输入/断言，映射实际
case JSON、CLI 码、控制原始目录；coverage 使用 covered、partial、uncovered、
not_applicable、outside_i2c。完整控制数量不能代替逐项契约完成。

partial 包括没有覆盖所有输入变体、只有 source fixture 或没有单独安装断言的项；
uncovered 保留具体缺口。组合编译器没有 separate compile/link 边界，M01 不适用。
#38/#39、平台迁移、B0 和外部项目任务不借安装结果追认。P01 原 Unicode 0.1.1
失败路径仍未修复，受控 registry 正例不替代该验收。所有这些状态会随 SDK回执交付。

本批可报告归档身份/干净安装/指定回归通过；只要仍有 partial/uncovered，不能称
完整 I1/I2 矩阵通过或授权移除 Preview。I2-C 是否 Accepted、后续补齐和 Preview
移除由独立复审决定，Q0 仍为后续独立门禁。

## 后续覆盖批次与实际实现缺陷

15267dcb 的归档/安装/就绪取证已独立复核为有效中间交付，I2-C 整体未完成。
本批先按错误/报告与共享状态边界补逐项断言，再组合已有同候选有效证据。
复用其冻结 SDK 时，大量正常输出复现了转发 EAGAIN 误判为基础设施错误；
原始 SDK SHA、失败报告与原始日志保留。只有发现这个实现缺陷后才生成新 SDK。

修订：实时转发只按实际写出的字节推进文件位置。短暂背压在下一轮有界重试，
不停止进程 deadline 检查；结束时在原有确认上限内排空。真实关闭/永久无法转发
仍为基础设施错误 2，记录未转发字节，原始日志不丢失。正常大输出不能因临时
EAGAIN 被杀掉或改记失败。SDK身份依旧按完整归档及实际组件来源绑定。

新增独立 SDK-only runner 不执行 checkout，只下载完整 SDK、单文件标准库 harness、
原始验收行和 ledger 脚本；harness 只导入标准库及安装 SDK 内 helpers。
无维护者 compiler/std-library checkout，SDK 自带标准库源码属于发行内容，允许使用。
原有安装/JSON/中断/P2 回归在另一个任务重放；不能把旧 SHA 结果直接重标。

R03/R04/R08–R11/R13/J01/J03/J09 使用真实 CLI 或明确标识的已安装 SDK helper
故障注入层；大编译输出代理委托真实 tokac 并追加合成 note 流量，不声称 note
来自原编译器。J09 stdout 关闭明确标 incomplete，不能从磁盘的早期 pass 快照
推断已成功交付。共享冷缓存/写锁释放、并发产物、同名 basename、stdin/env、
错误优先级与 POSIX managed start 拒绝均保存具体见证和原始输出。

R08 的外部 linker 启动变体对本核心 SDK 不适用（bundled LLD，无独立 linker PID）；
T12 的 Windows managed 后端仍不支持、位于核心 SDK 范围外，POSIX 拒绝启动
通过故障注入验证。覆盖描述必须保留这些 profile 边界。

逐行 ledger 只在完整输入/结果断言具备证据时收闭；其余行仍 partial/uncovered。
Preview 保留，Unicode/P01、平台策略、完整 Q0 继续独立。这里不授权移除 Preview。

报告输出末端的补充修订：第一次 SDK-only Linux J09 中，关闭 stdout 后没有 JSON
交付但返回了 0，失败 artifact 原样保留。输出末端现检查 stdout 是否可用，校验
完整写入并显式 flush；无 stdout、断管或刷写失败返回 2，并在可用 stderr 说明。
不可交付结果仍标 incomplete，不读取磁盘上的执行快照当成成功交付。测试覆盖
无 stdout/flush 断管，首次 Linux 的具体时序不由这些控制反推。

P14b 的 macOS SDK-only 首次失败也保留：观察到 interrupt 时 context worker 同时
正常退出，直接 child 与组最终都已确认消失，但无必要的组信号被 Darwin 拒绝。
监督修订仅在 leader wait 权仍保留、已确认退出且没有其他组成员时省去发送信号，
随后正常 wait/reap 和 ESRCH 确认。真正存活成员、权限或身份/组确认错误仍返回 2，
不忽略 EPERM、不取消权限检查、不向已回收 PID 发信号。独立控制覆盖此完成边界。
