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
