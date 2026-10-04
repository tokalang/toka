# I3-B：语义证据输出范围

此批落实 #38 / S01–S04，保持 Preview，等待独立复审。完整分析与证据输出
分离：使用已锁定项目上下文，以相同 argv 执行完整 `tokac --semantic-evidence=json
--check-only`；编译器退出后才选择输出记录。不改编译器语义或原始证据 v1 ABI。

## 命令

```sh
toka evidence main.tk --json                        # 默认 file，目标为入口
toka evidence main.tk --scope file --target lib/a.tk
toka evidence main.tk --scope all
toka evidence main.tk --scope decision --decision decision-v1-<sha256>
```

`--json` 保留为兼容标志；evidence 始终输出一个 JSON 文档。`-I` 可重复使用，
`-o` 保留透传兼容（仍为 check-only）。`--raw-project` 明确禁用自动项目上下文。
不再透传任意未知选项，未知/缺值选项返回配置错误 2，不回退到 all。
项目上下文沿用已验收的锁定解析及内容校验，不升级 lock；独立文件没有可确认的
项目归属时来源为 unknown。源码及运行环境仍可影响检查，但 scope 不进入编译 argv。

file 的路径按当前目录解析并规范化（包含符号链接/系统路径别名）。入口无记录时
仍可被选择；其他目标必须出现在实际完整证据的主位置或来源位置，存在但不在
此证据图的文件返回 2。此版本不承诺枚举所有零记录导入模块。decision 标识必须
存在于该完整结果；缺失标识、冲突选项及未知 scope 均返回 2，保留原始 scope 输入。

## 报告及兼容

编译器原始 `toka.semantic-evidence` v1 保持不变。manager 改为
`toka.semantic-evidence-view` v1；消费者应迁移到新 schema，不能把旧顶层固定
结构套到此报告。records 保留所有原始字段，增加 decision_id、reason_id、source、
origin_source。schema 位于 `schemas/toka.semantic-evidence-view.v1.schema.json`。

固定顶层字段为 schema、version、result、exit_code、scope、analysis、records、
reasons、compiler、errors。scope 记录 kind=file|decision|all、规范目标（all 为 null）、
output_filtered（确实省略了记录才为 true）及原始参数 input。配置解析未完成时
kind/target 可为 null，input 始终保留。analysis.scope 永远为 full，保留原始编译器
exit_code/result、总记录数、输出记录数及范围外仍保留的拒绝 ID。未启动编译器时
result=not_started、exit_code/records_total=null、records_emitted=0。
非 UTF-8 参数在启动分析前返回 configuration_error/2；input 使用替换字符展示，
input_base64 同时保留每项参数原始 OS 字节，输出仍是可移植的 UTF-8 JSON。

decision_id 为 `decision-v1-` 加原始记录的规范 JSON SHA-256；reason_id 为
`reason-v1-` 加 reason/subject/origin/两个位置的规范 JSON SHA-256。规范 JSON 使用
键排序、UTF-8、无多余空白，保留编译器实际路径。因此 ID 不受记录排序或范围影响，
不承诺源码重定位或不同宿主后的稳定性。reasons 以 reason_id 为键，保留实际理由和
两个位置及其来源分类。跨文件必要理由限定于编译器实际给出的 origin_location，
不推断未提供的传递因果图。来源复用 #41 的可信项目/锁节点/SDK 归属，未知归属不猜测。

file 保留任一位置命中目标的记录；decision 保留精确 ID 的记录。所有 Reject 和
ConservativeReject 无条件保留，即使发生于范围外。完整 stderr 原字节转发到 stderr，
并保存在 compiler.stderr_base64；不把文本猜成诊断来源。compiler 记录实际 argv、
原始退出码及完整证据 stdout_sha256，供与 all/原始编译器对照。成功保留 0，正常
语义失败保留编译器非零码；信号退出保留原负码并将 CLI 码归为 128+signal。
配置错误为 2，启动失败或无效编译器证据为 infrastructure_error/2，原输出保留。

本批不新增生产进程监督承诺；Python/SDK helper 的最外层启动失败沿用系统 stderr，
不属于 S04 的范围参数错误。toka test 的 C6 外层报告承诺保持原样。

## 验收映射

| 编号 | 实际控制 |
| --- | --- |
| S01 | 真实锁定本地依赖与 SDK 图，all/default/file/依赖目标输出对比；argv、完整 stdout 摘要一致；直接 tokac v1 原记录一致 |
| S02 | 真实依赖函数体拒绝及入口跨文件调用拒绝；all/file 非零相同，范围外拒绝仍保留，来源有锁节点；失败 decision 视图保留其他拒绝 |
| S03 | 成功程序选择实际跨文件来源决策及入口决策，记录及理由 ID 一致，无关 Allow 省略 |
| S04 | 无效 scope、缺失及图外目标、未知及缺失 decision、冲突参数、缺值及未知选项，单一配置错误 JSON/2 |

源码控制另覆盖输入不变、排序不改变 ID、空入口及不合法编译器文档。三平台安装
作业不 checkout 源码；同时保留 I3-A 的全部 17 场景回归。完整发布矩阵留到 Q0。
