# RC13：31 个负例目的核对候选

基于已接受的 `e084c990`，本包只修改测试、测试注册及记录。没有修改
`src`、`include`、`lib`，没有启动 E、推送或发布。保留其他 worker 的 RFC
未提交修改；不将它包含在本提交中。本包待集中复审，不自称 Accepted。

## 结果与口径

- 本轮完整 FAIL：**465/478**，前次 **447/478**；按名称恢复 **18**，新增失败 **0**，异常退出 **0**。
- 新增 `toka_rc13_negative_purposes`：**1/1，38.58 秒**。实际检查 18 个目标、
  7 个运行正例、normal/call-shadow/non-call-shadow parity、36 次 object/IR 不产物。
- 完整 FAIL 只运行一轮，117.44 秒；未 Bless、未排除任何 FAIL 用例。
- 剩余 **13** 个原失败及其预期原样保留，不算通过。不要求先修改编译器来清零。
- 未运行完整 PASS/CTest；不得把本次 FAIL 数字拼成一次新的三套完整基线。
  前次完整基线仍是 421/457、447/478、107/109；本轮只刷新 FAIL 并新增一个定向 CTest。

证据目录：`/Users/zhyi/GitDP/tokalang/validation/rc13-negative-20260916.h1JjKQ`。
`final-run.json`、`final-inputs.json` 保存准确命令、编译器 SHA-256 和冻结输入；
配置、定向 CTest、完整 FAIL 前后输入均未变化。机器结果见
[rc13_negative_purposes_results.json](rc13_negative_purposes_results.json)。

## 31 项逐项裁定

“恢复”不是仅仅编译失败：新门禁要求第一诊断、目标诊断及其文件/行号匹配，
不能由 E0402 等级联代替；同时验证相应合法操作实际运行。

| 用例（省略 .tk） | 原验证目的 | 本次处理 / 状态 |
| --- | --- | --- |
| alias_generic_test | alias 可兼容；nominal newtype 不可混赋 | 保留失败：合法 StrongNode 构造先被 IncompleteFacts 拒绝 |
| borrow | 独占借用期间不可读原绑定 | 恢复：删除 RHS 非法 `#`，保持名字侧权限，真实 E0441 |
| borrow_field | 独占借用期间不可读 owner 字段 | 恢复：同上，真实 E0441 |
| for_alias_shape_field_ceiling | alias 不得提升 frozen 字段 | 保留失败：显式数组类型后，合法可写字段对照仍 E04645 |
| for_alias_shared_array_interior_sibling | interior writable 不授予 sibling 写权限 | 恢复：显式完整数组类型；目标写入报 E04572/E04573/E0443；只写 reads 的对照通过 |
| init_custom_drop_reference_full_write | 不得通过引用初始化未初始化 custom-drop 对象 | 恢复：原 E0410 仍在；快照补录绑定失败后的级联，不以级联判过 |
| init_custom_drop_reference_sibling_read | 未初始化对象借用和 sibling 读取均拒绝 | 恢复：同上，保留两处 E0410 |
| init_immutable_live_array_element_reassign_after_cede | 移出一项不允许覆盖 immutable 的 live sibling | 恢复：补显式数组类型，真实 E04572；可写数组替换对照通过 |
| init_immutable_maybe_moved_array_element_reassign | 分支 maybe-moved 不授予重赋值权限 | 恢复：同上；正例运行两个分支 |
| member_access_sema_prefix_fail | 非法旧成员 quote 组合 | 恢复：声明迁至 Box<T>/data，保留两处非法成员 quote；不再由声明 quote 提前遮蔽，报 E01268 |
| morphic_constraint_violation | 显式类型实参必须匹配完整实参 | 恢复：合法 Cell 构造，`take_val<Cell>(cede ^p)` 报 E04571；`<^Cell>` 对照通过。没有继续使用旧“泛型默认 rigid”的解释 |
| owned_lazy_iterator_consuming_callable | repeatable mapper 不得是 consuming callable | 恢复：mapper binding 明确可写，抵达真实 E04591；重复调用普通 mapper 对照通过 |
| pal_branch_borrowed_field_escape | 两个分支都不能存入短命 referent | 恢复：两处 E0456 保留，快照补录失败后未初始化诊断 |
| pal_member_mut_borrow_duplicate | 重复独占 loan 拒绝 | 保留失败：合法 `info.&accesses` 先被 TypeIncompatible 拒绝 |
| pal_member_mut_borrow_payload_read | loan 活跃时原 payload 读取拒绝 | 保留失败：同一成员引用入口缺口 |
| pal_member_mut_borrow_payload_write | loan 活跃时原 payload 写入拒绝 | 保留失败：同一成员引用入口缺口 |
| projection_reference_hole_fill_rejected | 不得借用 partially-moved aggregate 填洞 | 恢复：原 E0410 保留，补级联；完整对象借用对照通过 |
| readonly_member_ref_decl_upgrade_from_plain_cast | 普通 cast 不得提升只读引用 | 保留失败：最初成员引用构造已误拒，未到 cast 权限检查 |
| ref_life_bound | outer binding 不能依赖 inner local | 恢复：原 E0456 保留，补失败后 b.ref 未初始化诊断 |
| return_ref | 缺返回依赖、错误依赖、短命引用重绑定 | 恢复：保留两个非法函数体；main 不再使用非法函数返回值，去掉 RHS `#`；三处目标 E0454/E0456 均触达 |
| slab_lookup_miss_blocks_remove | lookup loan 阻止 remove | 保留失败：Slab 库 AccessCapabilityMismatch 遮蔽，按授权不改库 |
| slab_uaf_decons | match referent 不能逃出允许的来源/生命周期 | 恢复：参数来源现报 E0454，局部 default_val 仍报 E0455；删除调用无效函数后的无关使用。此例用 Option，不修改 Slab 库 |
| smart_ptr_from_stack | 不得从 stack 构造 owning handle | 保留失败：首个 unique 表达式报 ContradictoryFacts，不将内部矛盾固化为新契约 |
| sync_guard_non_send | guard 不得跨线程发送 | 恢复：使用当前 shared factory 和真实闭包，不给拒绝的 spawn 追加 unwrap；准确 E0477；本线程取锁读取对照通过 |
| thread_spawn_copy_non_sync_capture | copy capture 要求 Sync | 保留失败：原 raw 字段拼写有误，修正探针仍在闭包构造处 IncompleteFacts |
| thread_spawn_implicit_capture_nested_closure | nested closure 不得携带隐式借用跨线程 | 保留失败：去掉 thin-fn 标注后，inner borrowed closure 仍无法构造 |
| thread_spawn_implicit_capture_via_assignment | 赋值不洗掉隐式捕获来源 | 保留失败：dyn-fn 替代在转换处 E04579 拒绝，未到原执行边界；不改 oracle 冒充到达 |
| thread_spawn_implicit_capture_via_binding | binding 不洗掉隐式捕获来源 | 保留失败：移除旧 thin-fn 标注仍在构造处 IncompleteFacts |
| thread_spawn_non_send_capture | raw capture 不得跨线程发送 | 保留失败：明确 raw handle 后仍在闭包构造处 IncompleteFacts |
| unique_value_local_move_noncede_parameter | borrowed owning 参数不能移入 owned local | 恢复：精确映射 E04661/InvalidIntrinsicUniqueMove；非任意 E04661。显式 cede 形参转移对照通过 |
| unset_escape | 未初始化存储不得建立可逃逸引用 | 恢复：保留四处 E0410，补绑定失败级联；正常初始化后的借用对照通过 |

六个仅更新级联的快照，其原目标诊断没有被删除。其余十二项是源码或已确定
契约的测试迁移；没有“生成标识变了，所以把整段错误都归一化”的处理。
特别是线程例中的 `__Closure_<number>` 差异不能掩盖 E04582/E0477/E0478
尚未触达，因此这五个测试仍失败。没有修改通用 FAIL verifier。

## 保留的最小反例

`tests/semantics/rc13_negative_purposes/blockers/` 保存九个隔离探针：
newtype 构造、qualified unique alias、成员引用、stack unique，以及五条 closure
路径。当前实际诊断列于机器结果；Slab 则保留原最小 FAIL 源码。
这些探针**不作为“拒绝即通过”的新负例注册**。尤其三个合法设置探针
`alias_good`、`for_alias_shape_field_ceiling_good`、`member_reference` 仍须后续
恢复；没有因它们现在失败而宣布新语言限制。

成员引用的等价 `&(owner.field)` 正例通过，但原 `. &field` 入口（实际拼写
无空格）仍误拒。最终特意恢复四份原负例及其 oracle，不以替换拼写隐藏缺口。

开发中加入已接受的 Channel mixed-capture 程序作为额外 call-shadow 对照时，
观察到 std/thread 泛型 JoinHandle 的额外诊断；对已拒绝 spawn 追加 unwrap
也出现类似差异。没有在本批修复或改写该已接受程序。最终专项目的门禁
使用本线程 guard 正例验证 guard 构造与使用，未将上述额外观察算作通过；
已有 Channel/thread 接受范围不被本包重新裁定。

## 重现

```
ctest --test-dir /Users/zhyi/GitDP/tokalang/builds/rc13-integration \
  -R '^toka_rc13_negative_purposes$' --output-on-failure
TOKAC=/Users/zhyi/GitDP/tokalang/builds/rc13-integration/bin/tokac \
  TOKA_LIB=$PWD/lib BLESS=0 python3 tools/scripts/test_verify_fail.py
```

第一项只报告修复目的与正例，并明确打印 13 blocked not counted；第二项仍会
以非零退出并显示剩余 13 项。源码 SHA 和具体诊断证据在结果文件中，不把
局部门禁绿色冒充 RC13 已就绪。后续需按剩余真实共同原因另选实现范围。
