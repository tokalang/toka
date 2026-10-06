# A1：锁配置错误码与消费者迁移

本变更只增加 `toka test --json` 的 P05 归因。C6 仍为 `toka.test-report` v1，
复用必需字段 `errors[].code`；执行、退出码、优先级与调度不变，不自动修复。
完整错误码表和处理建议见 [C6 契约](toka_test_v1.md)。

发现点为 `toka_package.read_lock` 的既有格式拒绝、locked resolver 的缺锁/记录
不匹配判定。锁不存在但项目没有依赖仍合法。symlink 安全拒绝、manifest 配置错误、
纯字节规范化差异及其他配置问题不自动成为三类错误；读取权限和其他基础设施故障
也不归入此错误族。锁格式未变，所有原始锁字节不由 test 改写。

## 兼容处理

- 旧报告 `code=null`：保留失败结果与退出码，具体归因不可用；不得凭 `fetch` 等文案猜测。
- 未知字符串代码：保留原值，显示未知代码，继续按 result/exit_code 处理，不能视为通过。
- 新候选的 P05 受控场景：必须准确匹配三类已知代码，同时核对 context 准备、
  `configuration_error/2`、全部已选测试 `not_run` 及未启动编译/运行。
- 必需字段缺失、协议版本不支持或同次 CLI 状态矛盾：消费者拒绝报告，保存原回执。
  消费者错误不能被写成生产者的 `compile_failed`、锁错误或新的 CLI 退出码。
- 更高优先级故障接管报告时，仍保持原基础设施结果；worker 原始失败另行保留。

消费者示例（`code` 已确认是必需字段，值为 string/null）：

```python
known = {"test.lock_missing", "test.lock_invalid", "test.lock_mismatch"}
code = report["errors"][0]["code"]
if code is None:
    label = "具体归因不可用"
elif code not in known:
    label = "未知错误码: " + code  # 原值保留；仍处理 report.result/exit_code
else:
    label = code  # 还必须核对 context、configuration_error/2 和未运行事实
```

不要从错误消息判断类别。修复建议来自固定契约表，不自动调用 fetch。

## P2 修订：独立脚本交付与显式回放契约

独立依赖、发现工作流及 `bundle_toka_i2c_batch.py` 必须携带
`toka_test_lock_contract.py`。工作流 identity.files 和完整包
support_files_sha256/bundled_inputs 均记录该模块，不依赖 checkout 或 PYTHONPATH。
对实际生成的三个包，在 checkout 外、移除 PYTHONPATH 的目录执行所有脚本 --help；
不能仅在源码目录 import 成功就认为交付完整。
完整包接收端在运行任何脚本前还必须读取 support_files_sha256，检查必需模块、
必需摘要项和 bundled_inputs，并核对实际字节。实际工作流接收代码的有效包正例、
字节篡改、错摘要、模块缺失、摘要项缺失、支持摘要表缺失五类拒绝控制均通过。

依赖 runner 和 ledger 的 CLI 必须显式指定 `--contract legacy|a1`，
不根据 code=null、退出码或观测结果自动切换。已冻结的旧 SDK 工作流明确绑定 legacy。
新候选本地/后续授权验证使用 a1。CLI 直接调用和台账绑定的契约不得冲突。
契约还须沿工作流 → test_toka_test_i2c.py → test_toka_test_i2b.py 安装子入口传递。
I2-B 的 --sdk 模式缺少显式 --contract 必须拒绝；源码控制入口保持不变。
冻结覆盖工作流使用 legacy，当前源码覆盖/安装流程使用 a1，不依赖报告返回值猜测。

- legacy：检查既有 context/configuration_error/2 与未启动事实，
  不要求区分三类错误码，不作 A1 代码验收结论；旧 null 仍为具体归因不可用。
- a1：三类准确代码及既有阶段事实均为必需，缺失、null、空或错误代码仍不通过。
- --observe：记录未满足项、原始报告、argv/cwd/退出码及原流，然后继续观察。
  进程返回 0 只表示观察完成；result.json 的 contract_pass=false 不得当作验收通过。
- ledger：独立核对所选契约，P05 缺口保留 partial 与具体 contract_gap；
  不能因为历史 checks=true 或观察命令返回 0 自动放宽 A1。
  保存 prior_evidence/prior_coverage，不覆盖旧台账；full_matrix_pass 保持 false。

定向结果：旧 SDK 的 A1 观察完成 33 场景且准确保留三个 P05 缺口；
同 SDK 显式 legacy 完成 33 场景通过；A1 覆盖版的严格 a1 完成 33 场景通过。
同一历史记录，legacy 台账保留旧契约覆盖，a1 台账保留 P05 partial，不中止生成。
新增接线控制覆盖实际接收端摘要拒绝与完整契约传递；现有四组控制仍保留。
使用 I2-C 实际生成的安装子命令，旧 SDK 的 legacy 18 场景通过，旧 SDK 的 a1
在 lock-missing 因 null 明确拒绝且保存原 CLI 退出 2，覆盖版的 a1 18 场景通过。
不重建 SDK、不执行完整 I2-C 矩阵或 Q0。

## 接口变化 → 消费者 → 控制 → 定向结果

下表的结果范围为本地源码控制与 macOS ARM64 原 SDK 副本上的 A1 Python 覆盖层，
不构成新 SDK 资格或跨平台结论。实际 argv、cwd、原始流和结果见交付 evidence。

| 消费者 | 原判断 → 新判断 | 控制与结果 |
| --- | --- | --- |
| `test_toka_test_dependencies.py` | P05 根据消息含 fetch → 显式 legacy/a1；a1 固定代码与同次 context/configuration_error/2/not_run；observe 保留缺口继续 | 33 依赖场景通过；P04/P08/P10/P14a 保持基础设施分类 |
| `ledger_toka_test_dependencies.py` | 仅布尔 checks → 对 P05 原报告再次核对代码和阶段 | legacy/a1 显式绑定；缺码 a1 保留 partial，legacy 只作原契约结论；旧台账字节不变 |
| `.github/workflows/test_0_12_i2c_dependencies.yml` | 原脚本清单缺支持模块 → 携带并摘要；冻结 SDK 显式 legacy | 实际生成的独立包脱离 checkout 启动通过；未执行远端工作流 |
| `.github/workflows/test_0_12_i2c_discovery.yml` | 同样依赖 Batch → 补支持模块及摘要 | 实际生成的独立包脱离 checkout 启动通过；未执行远端工作流 |
| `bundle_toka_i2c_batch.py` / `.github/workflows/test_0_12_i2c_coverage.yml` | 完整包及接收端核验支持模块/hash/bundled_inputs；旧 SDK 子入口明确传递 legacy | 全部实际生成脚本脱离 checkout 启动；固定 SDK/门禁范围不变 |
| `test_toka_test_i2b.py`（源码与 installed 入口） | 一般配置/退出结果 → 增加三类代码与同次阶段断言；独立必需字段清单 | C6 控制保留；安装子入口 old/legacy 通过、old/a1 拒绝、new/a1 通过；持久化优先级不变 |
| `test_toka_test_i2c.py` | 复用 I2-B validator → 共享 C6 校验；显式契约传入 I2-B 安装子入口，先存原始回执再校验 | 共享校验器控制通过；本轮未重跑完整 I2-C |
| `test_toka_test_i2c_batch.py` | schema/counts → 增加 A1 代码一致性；消费失败保留原结果与流 | 33 场景共享此入口；协议/必需字段/解码负控制通过 |
| `test_toka_test_discovery.py` | 继承 Batch 的 schema/result 判断 → 同步获得 A1 校验 | 共享校验器兼容控制通过；完整发现批次不重跑 |
| `test_toka_test_r08.py` | 继承 Batch；驱动启动 vs 真实链接拒绝 | A1 不改变 R08 的阶段事实；共享校验器兼容，非锁基础设施不冒充锁错误 |
| `test_toka_test_lifecycle.py` | 继承 Batch；超时/中断/阶段/清理 | 共享校验器兼容；A1 实际锁等待仍 infrastructure_error/2，不重跑生命周期矩阵 |
| `test_toka_test_cleanup.py` | 继承 Batch；清理与停止调度 | 共享校验器兼容；码不改变监督/清理实现，不重跑该批次 |
| `test_toka_test_observation.py` | 继承 Batch；状态和优先级 | 共享校验器兼容，unknown/null 一般失败仍保留，不重跑该批次 |
| `test_toka_test_report_boundaries.py` | 继承 Batch；J/A/M 报告边界 | 共享校验器兼容与消费者原流保留控制通过，不重跑该批次 |
| `test_toka_test_timing.py` | 继承 Batch；单调计时和冷/热 | 无计时接口变化；兼容检查，无新测量轮 |
| `test_toka_test_closure.py` | 继承 Batch；P01/D27 | 无 Unicode/FS 接口变化；共享校验器兼容，旧结论冻结 |
| `test_toka_test_i1.py` | preview receipt 的选择/结果字段 | 不读取 C6 错误码；执行接口未变，无需修改 |
| `test_toka_test_i2a.py` | 原监督 receipt 与阶段 | 不按 C6 code 判断，无需修改监督实现 |
| `test_toka_test_eperm_policy.py` | 原监督清理策略 | 不消费新码；原契约不变，无需修改 |
| `ledger_toka_test_reports.py` | 历史报告 schema、退出码与 counts | 无码身份判断；null/unknown 保留一般结果，旧回执不改写 |
| `ledger_toka_i2c_batch.py` | R/J/A/T 行事实、原结果 | 不含 P05 归因；新码不改变所读字段，无需修改 |
| `ledger_toka_test_discovery.py` | 发现批次 contract_pass 与入口事实 | 不读取锁码，兼容，原台账不改写 |
| `ledger_toka_test_cleanup.py` | 清理回执与 contract_pass | 不读取锁码，兼容，原台账不改写 |
| `ledger_toka_test_lifecycle.py` | 生命周期原状态与 contract_pass | 不读取锁码，兼容，原台账不改写 |
| `ledger_toka_test_observation.py` | 监督范围/参数/事件验收 | 不读取锁码，兼容，原台账不改写 |
| `ledger_toka_test_timing.py` | 计时观察 | 不读取锁码，兼容，不启动测量 |
| `ledger_toka_test_closure.py` | P01/D27 事实 | 不读取锁码，兼容，原台账不改写 |
| `validate_sdk_basic.py` / `release_platform_policy.py` / `test_release_platform_policy.py` | C6 投影、完整阶段和固定 run_fail/timeout | 正常锁行为和语义阶段不变；既有严格阶段拒绝继续保留，不扩大 basic 范围 |
| `run_e0_tasks.py` / `record_e0_command.py` / `run_e0_delivery_installed.py` | 测试 CLI 退出结果或通用 argv/原流 | 不按 code 归因；本变更保持退出语义，不重开 E0 |
| `test_developer_experience.py` / `test_developer_experience_contract.py` | C6 Preview、结果、编译失败和 Windows 能力边界 | 现有正反控制通过，包括同次 Preview 缺失拒绝；独立定位不改 |
| `test_json_cli_contract.py` / `evaluate_ai_coding.py` | manager evidence 参数/schema、PAL 语义拒绝 | 非 C6 消费者；既有回归保留；本轮源码专用 fixture E04648 拒绝，未扩大通过结论 |
| `test_toka_evidence_installed.py` | SDK 中 evidence-view v1、完整分析及 PAL Reject | 27 场景通过，另核验旧 --check-only 参数拒绝为 configuration_error/2、分析未启动 |
| `test_toka_add_installed.py` | add 的解析/回滚错误与另一 JSON 协议 | 17 安装场景通过；code 仅在 test C6 暴露，add 协议和回滚不改 |
| `tools/toka/src/test_report.tk` | 生成 native bootstrap C6 错误 | 不是读取方；Python/helper 缺失继续 code=null，不冒充锁错误 |
| `lib/toolchain/toka_evidence.py` / `test_toka_evidence.py` | evidence-view 的错误/完整分析 | 检索命中但非 C6 报告消费者；本次不改其错误族 |
| `summarize_test_baseline.py` | B0 测量协议 | 不是本批 C6 消费者；不提前启动测量或迁移 |
| `test_toka_test_filesystem.py` / `test_toka_test_filesystem_linux.py` | 文件系统查询与名字校验 | ext2/ext3 正例和空值、单独 / 等负例通过；不改 FS 实现 |

检索按 C6 schema、`test --json` 入口、errors/code、阶段及 report 读取字段进行，
完整命中文件清单保存在 evidence。`test_encap_slice0_*`、
`test_generic_body_call_qualification_stage0.py`、`test_non_call_transfer_shadow_stage0.py`、
`test_stage0_codegen_authority.py` 是编译器语义控制，并非 C6 读取方；本次不改它们。
其他文档（AI tooling、E0 用户指南、CSV pilot、I2-B 说明、tooling phase2）
没有基于锁消息归因的示例，保留原退出/报告描述；规范与 P05 表已同步。

## 失败与未覆盖项

负控制从独立固定 C6 夹具和三类固定代码构造，不用生产者工厂生成期望。
解码、必需字段、错阶段、状态/退出矛盾与未知协议拒绝保存消费者错误，并保留
原始生产者报告。真实 CLI 场景保存 argv、cwd、原退出/信号状态和 stdout/stderr；
无法取得的信息标注不可得，首次失败与修订分开。

本轮未执行 Linux 两宿主、Windows 实际 CLI、完整 I2-C/PL/E0 或 Q0。
Windows 当前监督能力不支持的行为不变，不宣称增加锁监督能力。
测量随老大的小验证轮派发，本轮未提前启动测量轮。无推送、合并、发布或 issue 操作。

范围外观察：将源码专用 `test_json_cli_contract.py` 直接配合已安装发布 SDK，
`check_success` 在源码树 `lib/core/traits.tk` / `lib/std/vec.tk` 出现 E04648、退出 1。
原 SDK 副本同样复现，未据此判为 A1 回归或推翻 Q0，也未放松检查器。
`compiler_version=0.9.9-38` 是编译器接口版本字段，不能据此断言实际运行了旧发行版。
安装夹具的 evidence-view/PAL 拒绝和非法参数分支已独立通过；源码 fixture 的
库信任/配置行为另列后续调查，原始失败、PATH/库绑定尝试与结果分别保留。
