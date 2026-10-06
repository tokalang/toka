# B1：本地包入口与准备诊断（0.13 开发契约）

范围：同名库消费；不承诺任意依赖别名重命名，不改变平台资格。公开 SDK 0.12 标签与原包冻结。

1. `toka new NAME --lib`（现有 CLI 名称在前；方案中的 new --lib NAME 表示创建库功能） 生成 `lib/NAME/mod.tk`，依赖别名为 NAME，消费方写 `import NAME::{…}`。既有 official/name 入口保留。
2. add/fetch 成功仅代表解析和锁定，不代表消费构建成功。已有锁的 build/check/test 准备使用锁验证，不隐式升级或改锁；按既有 TOKA_OFFLINE 设置允许获取经锁摘要验证的内容来补齐缓存。无锁时依赖配置失败，提示显式 fetch。
3. 三命令入口准备提供相同根因事实。代码 `package.entry_alias_mismatch`、`package.entry_missing`、`package.import_invalid` 在发现处产生。事实沿现有 PackageError.details 和 C6 errors[].package_entry 传递：`package_entry` 含 alias、requested_entry（未遇到导入则 null）、expected_entry、suggestion、source（未知则 null）。build/check 保留失败退出1；test configuration_error/2，context阶段、选中项not_run。不会将准备错误记为语义拒绝。
4. 无导入时也验证锁节点约定入口；错误导入诊断仅覆盖已锁包的已知根入口误写（仅 NAME/mod 或 NAME/NAME/mod；其他子模块仍交给编译器），不预解析全部语言。编译器仍负责其他语义与未知模块。不冒充一个完整语言解析器。
5. 人类消息允许变动；消费者依代码和同次阶段/状态/退出码判定。旧合法报告code=null与未知code保留结果，归因不可用或未知，不能推断通过；新受控场景缺码/缺事实失败。schema不变，既有消费者不要求新增顶层字段。权限、工具、缓存、下载、完整性错误保持原分类。
6. prepare-entry 是内部包helper命令：禁止改写现有锁，遵守既有离线设置，返回0/1；JSON错误复用toka.resolve-report v1（失败/1）。不承诺它是新公开用户命令。manager调用受控准备后才取映射，不能吞映射错误并让编译器报空位置E0901。
7. 锁字节与节点身份在重复运行中保持。用户修正manifest后需显式fetch生成匹配锁；不自动重命名、补入口或改锁。

契约先于实现写入。本地验证不等于正式SDK资格；仅编译本地manager验证消费者，不重建SDK。文档路径与验收见 package_entry_example.md 和 tools/scripts/test_package_entry.py。

## 兼容与迁移

check FILE --json 正常分析仍输出编译器 diagnostics v2；新增入口准备失败输出现有 toka.resolve-report v1（failed/1），errors[].code/category/details.package_entry。准备失败不伪造编译器语义报告。test --json 仍为 C6 v1，errors[].package_entry；build文本反馈保留退出1。消费者按schema分别读取并核对同次退出码，不能把resolve失败认作编译语义失败。旧报告code=null保留失败并标归因不可用，未知code原样保留；本批三类新候选受控输入必须具备对应码和事实。

已有锁的build/check不再静默调用fetch刷新锁；不匹配时需用户明确修正后fetch。这是准备行为收紧，原合法锁节点和成功结果保持；无锁无依赖合法，缺锁有依赖失败。其他manager命令复用append_locked_project_context的已锁准备同样受到此收紧，未新增它们的入口支持承诺。

0.12 的入口缺失/别名不对应曾在 test 中表现为 infrastructure_error/2；本批将这些明确的入口配置问题归为 configuration_error/2，退出码2和停止调度/not_run保持。旧报告必须按其原状态保留，不回写类别，也不得强制它带新码。读取/完整性故障不随之改类。

B1-R 修订：导入写法仅检查实际选中入口和沿现有源码import可达的文件，不读取无关lib/src文件；无入口参数只验证锁节点。build在选中build.tk/Project.tk准备后，由实际build driver针对entry_files检查目标。未知import解析继续由编译器负责。缓存缺失与锁升级不同；前者在非离线下可获取并校验锁定字节，后者禁止隐式发生。

R1 相对导入补充：现有 ./ 与 ../ 路径以导入文件所在目录解析，再按规范化文件身份去重；保留选中范围和循环保护。同一可达文件中错误包入口的诊断不得因调用方采用相对或根路径写法而改变。未知导入仍交由编译器，不新增语法。
