# 0.13 D1/D2 诊断修订（本地候选）

## D1：直接应用完整 fix

E04509/E04570 的代码、原消息、diagnostics v2 与 fix 结构保留。一个 machine-applicable fix 是一组原始坐标上的 edits，不是只取 edits[0]。普通名称仍是一条插入 `cede `；带 payload 写意图 `#` 的名称在同一个 fix 中还删除该后缀。相邻、空格、换行、注释下的后缀位置来自已解析 AST，不通过消息猜测。声明中的 `#` 保留；`cede buffer#` 仍被语言拒绝。

消费者应先验证所有原文件及坐标，再按原坐标倒序整体应用。不能应用插入后再按旧坐标正序删除。不能把重新手写的源码算作生产者建议应用通过。未能确认简单命名来源及原始拼写时，不广告机器可应用建议；复杂源表达式的自动修复不在本批扩项。

D1 错误捕获临时将 active diagnostic node 绑定到真正实参，opaque AST anchor/semantic node identity 可能从调用节点变为实参节点，schema不变。消费者不能把这些不透明身份写死为旧调用节点；源码诊断位置仍按实际错误节点保留。

## D2：方法实参 signed→unsigned

本批已知模式是 Document.find 的 usize 索引接收已定型 i32 值。方法检查不再由 PrimitiveType 的 loose integer fallback 放行有符号到无符号实参；具体和动态 trait 方法都使用此检查，其他 isTypeCompatible/CodeGen 未改。

现在 check-only 与代码生成均在前端返回失败并给 E04510、实际实参的源码位置、expected/actual类型。原 latent-invalid 程序 check曾返回0；现在返回1。有效 usize 实参与显式转换路径保留，编译器不自行猜测修复或插入无符号转换。平台别名经规范化可能显示usize的底层u64；此时与i32的宽度/符号差异仍是实际类型事实。源码中声明usize时优先保留该名称。

不声称所有隐式数值转换、所有函数/FFI/CodeGen路径或所有IR verifier问题均修复。LLVM验证器继续保留。本地编译器使用冻结0.12 SDK库/运行时做定向控制，不构成新SDK或跨平台资格。

## 已知消费者对应表

| 变化 | 已知消费者 | 处理／验证 |
|---|---|---|
| D1一个fix可含两条edit | test_d1_d2_diagnostics.py（新增独立应用器） | 全量读取并倒序应用所有edits，直接编译+运行，另保留只插入不删#的拒绝控制 |
| bare cede fix仍一条插入；hatted名称保留 | stage1 explicit/indirect/method/dynamic_trait 参数控制 | 复用既有脚本与夹具，本地SDK库环境复跑；不修改其断言 |
| fix数组读取 | test_ai_tooling.py | 现有循环逐条收集并倒序应用所有edits，单行跨度仍满足；本批新应用器覆盖同样消费规则，未跑完整tooling |
| 单edit评价 | evaluate_ai_coding.py | 现有repair_cases仅E01244/E01246，未消费E04509/70，逻辑不变；不把它扩成新全量评价框架 |
| editor/LSP | tools/tokalsp/main.cpp | 检索未发现消费diagnostic fixes的应用逻辑，现有edits为rename，与本批无关；未构建LSP |
| D2既有E04510判断 | stage1 method参数控制、call-transfer shadow用例 | code/消息模板不变；方法定向脚本回归，shadow完整批次未跑，不能扩大其通过结论 |
| manager/C6 | toka test报告读取、CLI diagnostics消费者 | 本批不改C6/manager/诊断schema/退出码映射；已知无效模式由CodeGen失败变成前端失败，正式安装SDK验收不在本批 |

脚本包必须包含应用器、已用历史脚本、夹具与依赖身份；本地回执与源字节单独绑定。旧合法报告照原事实保留，不回写为新诊断；未知码保持原值，不猜测根因。缺协议字段、不支持版本、错误状态或必需源码事实时，新控制拒绝并保留原CLI回执。
