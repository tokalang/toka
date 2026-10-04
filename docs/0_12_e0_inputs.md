# E0 真实项目与操作输入冻结

本提交只固定输入与验收任务；E0 尚未执行，也未 Accepted。
源仓库 toka-examples 固定为 `cdece08ca6ddc4ec403c160b198bbba8841e14dc`；
项目文件摘要、任务资源、期望 Unicode 锁与介入记录字段见
`spec/e0_input_plan.v1.json`、`tests/fixtures/e0/`。

## 两项真实任务

1. **registry_unicode_consumer**：从空目录创建项目，恢复固定源码/build，显式
   add `unicode:0.1.2`，生成独立新锁。原 0.1.1 manifest/lock 保留；不隐式升级或
   覆盖原证据。新锁须与固定 archive/content SHA 相符。依次 check、build、将
   原入口复制为明确的 test 入口、test、run；交付源码/lock/运行回执与产物。
2. **csv-transform**：创建并恢复固定真实应用源码/build，复制资源 input.csv 与
   固定本地 e0_paths 包，再 add 本地路径依赖。check、build、运行固定
   csv-e0_test.tk（实际使用本地路径依赖），toka run 输入/输出参数；用固定
   verify_csv.py 核验两字段记录及顺序。交付包含本地依赖、资源、源码、锁、输出
   与回执。测试脚手架、资源/预期数据在执行前已固定，不临时改任务提高通过率。

SDK 输入为原始 v0.11.0 与后续固定候选各自完整归档、SHA 及组件身份，不能借旧
SDK 回执充当新包结果。SDK 位于独立目录，源仓库及编译器源码不可见。复制固定
任务输入属于任务明确步骤；维护者修改命令、补环境路径、修缓存另算介入。

## 对照与记录

两种执行流程（用户按文档、AI 按同一任务）从相同空目录/固定输入开始。各自保存
第一次冷执行和同任务热执行，保留失败。每步保存 argv、退出码、单调耗时、stdout/
stderr、SDK/锁身份、修复轮次、人工介入与原因分类；未完成步骤列明，不能改写成
通过。发布依赖获取单列网络时间，离线复跑保持锁不变。

create/add/check/test/run/交付的任务语义相同；0.11 不支持的正式 test 参数须记录
实际不支持结果，不能换成维护者编译器命令伪装通过。本文件不承诺耗时或完成率。
执行脚本与结果在 E0 批次交付，输入变更须单独保留修订与旧失败记录。
