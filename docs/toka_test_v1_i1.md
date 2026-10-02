# I1 项目测试基础：Preview

状态：2026-10-02 I1 实现与定向验证；B0 已独立复审 Accepted。尚未进入 I2，
也未冻结 0.12 SDK 或修改平台政策。规范仍以 [v1 契约](toka_test_v1.md)为准。

## 本批实现

- `toka test` 将参数按 argv 转交随 SDK 安装的 `lib/toolchain/toka_test.py`，
  不生成 Bash 扫描脚本，不要求用户补源码目录或隐藏包含路径。
- 从当前目录上溯项目根；默认只发现 tests 下的 `_test.tk`，排除辅助模块、
  链接和嵌套项目。显式入口替换默认集合；按规范 ID 排序/去重，字面 filter
  支持 OR，空集合默认 2，allow-empty 为 0 且明确无测试。
- 复用权威 package resolver、compiler-mappings、compiler-node-mappings 与
  workspace-node；生成器的 lib/<name>/mod.tk 自身库布局复用权威 helper 的
  workspace library 映射，并绑定 workspace node。新的可选 locked 模式只用于此预览，不改变默认 fetch 行为。
  测试准备不生成或升级 lock，拒绝 manifest/节点/本地内容失配；按原 lock
  获取并验证缓存。并发预览调用的共享依赖物化用项目写入锁串行化。
- 必需 native 输入复用 SDK 原有 build helper 的 native plan、C 编译与链接
  标志规则，输出对象写入该调用的独立目录。不调用整个 toka build 流程。
- 每次调用与每项独立产物目录；cwd 为项目根，stdin 为 EOF；失败日志保留。
  串行编译/执行，正常编译/运行失败继续，工具启动或准备失败停止。

## 有意保留的阶段边界

CLI 仍显式打印 Preview。`--json`、compiler/run timeout 选项及正式进程监督
没有进入 I1；不能将 B0 测量器的保护上限当成这里的监督实现。B0 接受的
30000/5000 ms 写入配置常量供 I2 接入，**当前预览不实施编译/运行 deadline**。
依赖写入锁等待采用独立 30000 ms 上限，不应用到整个 build 流水线。

保留的 `preview.json` 是 I1 内部回执（schema=toka.test-preview-i1），不是
C6 的稳定 machine report，也不是 stdout JSON 接口。它明确记录 supervision
尚未实现及 deadline 未启用。用户中断、后代退出确认、结构化来源和完整失败
报告仍由 I2 验收。Windows 源构建路径对非空执行明确报告 POSIX 预览边界；
不通过 Bash 或单 PID 取消伪称具备 C4 保证。

## 验证范围

源码定向控制覆盖发现、显式/筛选/空集、排序去重、Unicode/空格/符号链接、
相对入口、非法参数、失败继续、工具启动失败停调度、锁不改写、manifest/本地
内容变化拒绝、产物路径和两个并发调用的隔离。原包供应链回归仍通过。

安装集成在独立目录重定位原 0.11 SDK 的**私有副本**，用原编译器只构建新 manager，
覆盖副本中的 manager 与两个 Python helper；编译器及 runtime 原字节保持不变。
它不是公开 0.11 包，也不是完成资格的 0.12 SDK。SDK 自定位从 PATH 调用验证，
不设置用户 TOKA_LIB。新 manager 构建在独立目录中完成，避免开发源码缓存影响
标准库定位；源码不成为消费者项目的运行依赖。

真实安装检查包含：辅助文件不执行、子目录显式入口、锁定本地依赖、C 原生
输入、离线 registry 夹具、运行非零及空集合。registry 使用可控的不可变小包，
只验 SDK 缓存/节点基础；**不能替代 Unicode 的实际失败路径或宣称 P01/E0 完成**。

B0 的 Unicode 0.1.1 三平台 30 个失败及原发布包继续保留。此提交不改编译器
权限检查，不迁移该包，不裁决其根因。后续诊断/修复必须独立提交，先区分
包契约迁移与编译器误拒，并保留不可变历史包。

对应验收 ID 与实际控制的映射见 tools/scripts/test_toka_test_i1.py；全 120 条
矩阵仍有 I2/I3/PL/E0 项，不能把本批定向检查写成整个 v1 Accepted。
