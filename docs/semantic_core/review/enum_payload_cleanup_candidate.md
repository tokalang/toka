# Enum payload 清理：独立修复候选

Date: 2026-09-08
Status: Accepted — typed enum payload cleanup only

用户独立审查接受：仅限完整元数据的清理派发及不完整元数据的拒绝；不包括
上游声明解析或 ABI 修复。独立验证 enum/Vec CTest 2/2、Vec 17/17、diff 检查，
并补测 enum 的 `[^Resource; 2]` payload，两个元素均正确析构。

## 范围

本轮编译器变更只在 `CodeGen_Memory.cpp::emitDropCascade` 的 enum payload 分支。
不改 ABI、raw_take、Vec remainder、引用计数算法或其他结构体清理路径。
工作树仍未提交，既有冻结 refs 未移动。

原来的字符串首字符判断把 unique/shared 与 raw/reference 一起跳过。本候选
只用 Sema 提供的完整 `ResolvedType` 计算当前 variant 的 payload 字段布局，
逐字段交给现有 `emitDropForType`：

- unique：递归析构后释放 allocation；shared：释放当前引用，最后一个引用才析构。
- raw/reference：不析构或释放 pointee。
- nested enum、普通值和多字段 payload：继续按现有类型清理协议递归。

原有活跃 tag 分派、外层 moved/uninitialized gate、custom-drop 调用位置、
清理责任和剩余字段协议未改变。没有新增 retain/release 算法，也没有自行推导所有权。
缺失、未解析、布局不可用或显式 root morphology 与解析结果矛盾时，E0701 且无产物，
不再通过字符串补造清理类型。

## 定向结果

- 新 enum gate：4 个运行及 normal/shadow 对照全部通过；2 个不完整元数据
  object/IR 拒绝检查通过，零跳过。
- 覆盖独立 Option unique/shared、共享副本仍存活、None、整体移出、unwrap 后
  不重复析构、嵌套／多字段、custom drop、never-initialized／初始化后清理、
  raw/reference 不误清理 pointee。
- [Vec 门禁](enum_cleanup_vec_report.json)：17/17，零跳过。
  原 handle pop 及两个独立 Option 漏析构对照均恢复运行返回 0。
- 上述两个 CTest 合并运行 2/2（21.71 秒）；这不是完整测试套件。

## 保留的上游问题（不伪装成语言限制）

`Both(^Resource, ~Resource)` 表示两个不同字段的类型，完全合法；并不是同一变量
同时具有两种 owner。当前 direct enum 声明路径会把这些 TypeSyntax 的 root hats
在 Sema 解析中丢掉，构造布局也按普通 Resource 生成。这不是清理代码可以猜回来的事实。

因此本候选做的是只读一致性检查：声明中显式 pointer morphology 不得消失。
检测到矛盾就拒绝 CodeGen；不改变其 Sema 声明解析或 ABI。该声明路径尚未修复，
不能把拒绝说成语言禁止这两种字段共存。

多字段正向回归使用现有能保留完整解析类型的泛型实例
`Event<^Resource, ~Resource>`，实测两个不同资源及 shared 副本的生命周期。
另保留 `incomplete_concrete_morphology.tk`，明确验证上述直接声明路径不产生错误产物。

相关集成组另行记录；未运行全量 PASS/FAIL 或四平台 CI。

## 相关集成检查

[一次相关 CTest 检查](enum_cleanup_integration.txt)：3/10，108.56 秒。
binding、standalone、dyn fn lifecycle 保持通过；此前 7 项失败仍在相同检查点失败：
thread callable、method rollback oracle、indirect parameter、return/source、
return matrix、nominal probe、signature-driven remaining routes。
未改动这些路由或旧 oracle，没有把它们描述为已经恢复。

本轮 tokac 增量构建及 `git diff --check` 通过。定向 enum/Vec 两项 CTest 为 2/2，
其中 Vec 为 17/17，零跳过；不代表整体 binding 或全仓资格通过。
无提交、推送、PR 或 Actions 触发，当前候选仍等待独立审计。
