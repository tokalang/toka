# B0 冷／热基线与默认值建议

状态：2026-10-02 测量与文档修订完成，**待复审，未进入 I1，未作最终实现冻结**。
本次是已发布 SDK 的组件测量，不是 0.12 功能验收，也不是完整发布资格矩阵。

## 固定身份与宿主

- 原始 SDK：v0.11.0，源码 `57b0f7dd7d52bdc24c6dde0457803240e5c62e8a`；
  所有包都与原发布 SHA-256 一致，二进制/runtime/helper 在每个样本前后未改变。
- 测量提交：`f8156f3b6c0c93c86fb2b199c3587ce18c3d59d9`。
  [固定测量器](https://github.com/tokalang/toka/blob/f8156f3b6c0c93c86fb2b199c3587ce18c3d59d9/tools/scripts/measure_test_baseline.py)。
- 测量器文件 SHA-256：`526df2f4d033081308b3e51806df99aaa88c921bcf5d86c48d1f7f2cf2ca5f05`。
- [正式 B0 运行 36899438902](https://github.com/tokalang/toka/actions/runs/36899438902)，
  attempt 1：三个核心目标均完成。同一提交、同一脚本文件、干净 checkout。
- 宿主 Python 固定 3.13.7，clang/LLD 20.1.8；实际可执行文件摘要、OS、kernel、
  libc、CPU 数、affinity、runner image 与命令参数均保存在原始报告。Linux x64
  使用 ubuntu-22.04（glibc 2.35，4 CPU），Linux ARM64 使用 ubuntu-24.04-arm
  （glibc 2.39，4 CPU），macOS ARM64 使用 macos-15（15.7.9，3 CPU）。
  这是固定观测组合，不能假称 GitHub 的滚动 image 永远不变。

| 目标 | 原始 SDK 包 SHA-256 |
| --- | --- |
| Linux x64 | `05faa6cf2128f9dc385aa832c33f722e34f5a4f4b98d0613887a3efe79a35635` |
| Linux ARM64 | `16e8deda50d7983bfa9cffc9f55e8587f4b9b3f4c72c7346a3e82f3c173466f1` |
| macOS ARM64 | `81e95d01f685f4fbb057fc42b03c376c2646b41bce08398f8f40a76d2cdd7b99` |

## 任务与阶段边界

协议 `toka.b0.sdk-components.v1`，固定七项任务：bare_ok、owned_vec_ok、json_ok、
local_dep_ok、registry_dep_ok、compile_reject、run_nonzero。源码正文及 SHA-256
直接留在每个平台报告的 tasks 中；每个任务 5 组 cold→hot 配对。

冷样本重新解包原始 SDK、新建项目，清空项目物化/语义/构建状态；SDK 原包已有
接口保留，未清理或假称清理了 OS 页缓存。本地依赖的绝对 locator 由 SDK helper
在采样外解析一次，锁文件随后固定；其原文也保存在 raw/setup 中。热样本复用
该对 SDK/项目/锁及已有状态，重复相同阶段顺序。

测量器只调用原 SDK 的 new/fetch、公开 package helper 映射命令、tokac、check
和 build。直接 compiler 调用固定 `-O0`，不依赖编译器源码树；参数使用安装 SDK
及其真实锁定节点。没有要求未实现的 0.12 test 命令，也不将 0.11 preview scanner
记作 v1 能力。直接 compile_link 在 check/build 探针之前，后两者是复合 CLI 流水线。

可观测 compile_link 与 run 单独计时；compile 和 link 的固定 phase 对象保持
state=not_started、duration/process/原始状态=null，不填造分离耗时。网络下载单列，
固定 unicode 0.1.1 包下载后由原 helper 离线验证/物化；该阶段不包含实时 catalog
查询。源码生成、SDK 解包与版本探测是采样前 setup，不塞进 compiler/run 时间。

## 汇总

共 210 个冷／热样本，每目标 70 个，每个任务每模式各 5 个。210 个 compiler
调用有完整观测；实际开始并结束的 run 为 150 个。compile_reject 与不兼容的
registry_dep_ok 都未启动 run，不能编造其运行时长。无保护上限触发、无截断样本。

以下是毫秒最大观测值，CLI build 是复合流水线，不用于 compile_ms 推导：

| 目标 | 样本 | 每任务冷/热 | compile_link max ms | run max ms | network max ms | build pipeline max ms |
| --- | --- | --- | --- | --- | --- | --- |
| linux-arm64 | 70 | 5 / 5 | 2169.966 | 4.041 | 214.355 | 31419.895 |
| linux-x64 | 70 | 5 / 5 | 3121.270 | 4.274 | 264.577 | 18308.176 |
| macos-arm64 | 70 | 5 / 5 | 3616.173 | 80.519 | 496.953 | 36137.637 |

逐任务、逐模式的计数、中位数、最大值如下；“functional failures”表示未满足该
任务功能期待的样本，负例的正常预期失败不算期待不符。not_started 不是 0 毫秒。

| Target | Task | Mode | n | functional failures | compile+link max ms | run max ms | network max ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| linux-x64 | bare_ok | cold | 5 | 0 | 966.396 | 3.853 | not_started |
| linux-x64 | bare_ok | hot | 5 | 0 | 966.451 | 3.958 | not_started |
| linux-x64 | owned_vec_ok | cold | 5 | 0 | 1417.301 | 3.955 | not_started |
| linux-x64 | owned_vec_ok | hot | 5 | 0 | 1417.352 | 3.890 | not_started |
| linux-x64 | json_ok | cold | 5 | 0 | 3121.270 | 3.819 | not_started |
| linux-x64 | json_ok | hot | 5 | 0 | 3121.254 | 3.833 | not_started |
| linux-x64 | local_dep_ok | cold | 5 | 0 | 966.328 | 4.274 | not_started |
| linux-x64 | local_dep_ok | hot | 5 | 0 | 966.392 | 3.932 | not_started |
| linux-x64 | registry_dep_ok | cold | 5 | 5 | 916.184 | not_started | 264.577 |
| linux-x64 | registry_dep_ok | hot | 5 | 5 | 916.304 | not_started | not_started |
| linux-x64 | compile_reject | cold | 5 | 0 | 64.304 | not_started | not_started |
| linux-x64 | compile_reject | hot | 5 | 0 | 64.528 | not_started | not_started |
| linux-x64 | run_nonzero | cold | 5 | 0 | 966.982 | 3.874 | not_started |
| linux-x64 | run_nonzero | hot | 5 | 0 | 967.007 | 3.876 | not_started |
| linux-arm64 | bare_ok | cold | 5 | 0 | 668.444 | 1.669 | not_started |
| linux-arm64 | bare_ok | hot | 5 | 0 | 666.173 | 1.678 | not_started |
| linux-arm64 | owned_vec_ok | cold | 5 | 0 | 1015.890 | 3.771 | not_started |
| linux-arm64 | owned_vec_ok | hot | 5 | 0 | 1016.745 | 1.680 | not_started |
| linux-arm64 | json_ok | cold | 5 | 0 | 2122.101 | 3.732 | not_started |
| linux-arm64 | json_ok | hot | 5 | 0 | 2169.966 | 3.735 | not_started |
| linux-arm64 | local_dep_ok | cold | 5 | 0 | 666.362 | 4.041 | not_started |
| linux-arm64 | local_dep_ok | hot | 5 | 0 | 666.538 | 1.676 | not_started |
| linux-arm64 | registry_dep_ok | cold | 5 | 5 | 517.402 | not_started | 214.355 |
| linux-arm64 | registry_dep_ok | hot | 5 | 5 | 518.072 | not_started | not_started |
| linux-arm64 | compile_reject | cold | 5 | 0 | 31.962 | not_started | not_started |
| linux-arm64 | compile_reject | hot | 5 | 0 | 31.982 | not_started | not_started |
| linux-arm64 | run_nonzero | cold | 5 | 0 | 667.645 | 1.689 | not_started |
| linux-arm64 | run_nonzero | hot | 5 | 0 | 666.452 | 1.710 | not_started |
| macos-arm64 | bare_ok | cold | 5 | 0 | 848.951 | 14.274 | not_started |
| macos-arm64 | bare_ok | hot | 5 | 0 | 902.567 | 16.273 | not_started |
| macos-arm64 | owned_vec_ok | cold | 5 | 0 | 1250.920 | 15.246 | not_started |
| macos-arm64 | owned_vec_ok | hot | 5 | 0 | 1187.993 | 21.893 | not_started |
| macos-arm64 | json_ok | cold | 5 | 0 | 3616.173 | 23.104 | not_started |
| macos-arm64 | json_ok | hot | 5 | 0 | 3075.454 | 13.880 | not_started |
| macos-arm64 | local_dep_ok | cold | 5 | 0 | 1006.352 | 57.941 | not_started |
| macos-arm64 | local_dep_ok | hot | 5 | 0 | 865.583 | 80.519 | not_started |
| macos-arm64 | registry_dep_ok | cold | 5 | 5 | 798.359 | not_started | 496.953 |
| macos-arm64 | registry_dep_ok | hot | 5 | 5 | 1007.011 | not_started | not_started |
| macos-arm64 | compile_reject | cold | 5 | 0 | 138.389 | not_started | not_started |
| macos-arm64 | compile_reject | hot | 5 | 0 | 77.348 | not_started | not_started |
| macos-arm64 | run_nonzero | cold | 5 | 0 | 692.241 | 18.814 | not_started |
| macos-arm64 | run_nonzero | hot | 5 | 0 | 646.847 | 16.714 | not_started |

## 默认值推导（规则先于正式采样固定）

| 项目 | compile_ms | run_ms |
| --- | --- | --- |
| 可用样本数 | 210 | 150 |
| 样本上界 ms | 3616.173167 | 80.519416 |
| 四倍上界＋加性余量 ms | 19464.692668（＋5000） | 1322.077664（＋1000） |
| 保守政策下限 ms | 30000 | 5000 |
| 向上舍入单位 ms | 1000 | 1000 |
| **默认值建议 ms** | **30000** | **5000** |

四倍是明确的跨宿主/调度波动裕量，不是从 5 个样本估计出的分位数。加性余量
覆盖启动/等待不稳定性；政策下限避免根据微型任务制定亚秒级脆弱默认值。两个
默认值在本次都由政策下限主导，不能宣称全由样本精确预测。合法大项目/长测试
可以显式覆盖；I1 若改变编译 profile 或执行边界，须重新核对本证据的适用性。

网络、依赖、check/build composite 与 setup 不混入上述推导；全部原始结果仍保留。
真正发生保护上限的阶段 duration_ms 必须为 null，只能作为 censored/incomplete，
不能当作“在上限时间内正常结束”的数据。测量器用 600000 ms compiler/build、
120000 ms run、180000 ms 网络/其他命令保护上限，并另有 60 分钟 job 上限；
这些数值与建议默认值严格分开。

## 保留的功能失败与未完成尝试

unicode 0.1.1 的获取、缓存摘要与锁节点验证成功，但其源码 grapheme_slice 等路径
在 v0.11.0 下触发 E0454/E0455。三个核心平台各 10 个样本均未能编译/check/build；
对应 run 没开始。30 个功能期待失败完整保留，没有修改包/SDK、换任务或删失败
样本。它是后续真实项目闭环需要处理的兼容性缺口；本次 B0 不声称它已经修复，
也不推断失败包在修复后的运行时间。其他预期通过任务及正常编译/运行反例记录
按实际结果保存。测量完整性通过不是“SDK 所有功能通过”。

首轮 [36898566339](https://github.com/tokalang/toka/actions/runs/36898566339) 的 Mac
准备阶段因 Python 3.12.12 无该 ARM64 构建失败；两个 Linux 完整结果和失败日志
单独保留。正式轮改为所有平台 Python 3.13.7，重做整组，不拼接轮次。更早本地
单对 pilot 为脏/未冻结测量器探索，也单独保存，不进入默认值推导。

## 交付与复审

原始产物为正式运行的 b0-linux-x64、b0-linux-arm64、b0-macos-arm64 artifacts。
包含未改写 baseline.json、所有原 stdout/stderr、原 lock/manifest/源程序。
汇总器拒绝不同提交/脚本、pilot、dirty、丢失或重复样本、SDK/lock 变化、截断、
伪造未开始阶段等输入。保护上限和推导拒绝用例已验证；这些控制不是 v1 实现测试。

本地完整证据在工作区 evidence/0.12-b0，包括 summary.json、summary-table.md、
所有轮次、原始路径到下载文件的索引以及摘要清单。仓库文档保留可移植的运行链接，
下载镜像不改写原始运行的绝对路径；索引用于本地阅读和校验。

两处已批准契约修订同步完成：C6/J03 五类汇总；C6/C7/M02 未开始 phase 固定对象。
B0 数值建议已写回契约，仍须复审再进入 I1，不触发平台策略部署或完整发布资格。
