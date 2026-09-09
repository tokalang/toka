# 显式 unsafe raw-element take：最小契约草案

Status: Accepted for first-batch implementation — binding slice not accepted
Date: 2026-09-07
Authorized scope: dedicated primitive and fault gates, then minimal Vec integration,
then tools/failed CTest, then one full PASS/FAIL run

## 1. 目的与非目标

为 raw container 从一个已初始化槽位取出完整值提供显式低层操作，解除当前
初始化／整绑定赋值切片的 raw-element extraction 阻断。该操作不声称编译器
已证明动态 initialized extent，也不把普通 `cede pointer[index]` 自动放行。

现有边界保持不变：`alloc` 只提供存储；`free[count]` 的 count 是调用方声明的
live prefix，而非此前初始化状态的证明；普通 `init` 不因此支持 raw-pointer
target。参见 [alloc/free 规范](../syntax.md) 与 [init 契约](init_contract_rfc.md)。

不引入完整 raw-storage/extent capability、capture/receiver 改动、TKI 重构、
carrier ABI 或引用计数算法改动。本草案不接受当前 binding 实现，也不修改已冻结切片。

## 2. 操作及责任

冻结拼写为 `auto 'value = unsafe raw_take buffer[index]`，解析为专用操作节点。
首批仅接受已解析 raw-storage 索引 place；完整元素类型及 morphology 来自存储声明。
不接受普通值、managed place 或单纯 Addr，不将 `*ptr` handle selector 当解引用，
不要求先构造 `&T`；nullable 基址仍必须满足现有检查。操作身份由专用节点确定，
不能通过函数名、标准库位置或普通 `unsafe` 块猜测。

操作数仅求值一次，确定一个完整 `T` 槽位；操作将值交给结果，并逻辑上退休该槽位。
不隐式 clone/retain，不释放原始 allocation，也不消费容器其余元素。

| 责任层 | 要求 |
|---|---|
| 编译器必须验证 | 已解析的完整 `T`、精确 morphology、合法显式操作及 unsafe 上下文、操作数类型、已知 PAL/权限/依赖约束、结果到 destination 的正常兼容性与清理计划。缺失事实不得猜测。 |
| unsafe 调用方保证 | 地址有效、对齐、范围足够；完整 `T` 已初始化且可读；拥有该槽位的转移权；没有违反转移的未跟踪别名或并发访问；后续 remainder 清理排除该槽位。 |
| 操作后条件 | 槽位退出 live 集合，不得再次读、取出或析构，除非合法重新初始化；结果接收该值原有的清理责任及实际依赖，allocation/remainder 仍由原 raw owner 管理。 |

unsafe 承担不覆盖编译器已经发现的借用冲突、类型不匹配、权限提升或生命周期逃逸。
也不能用“没有诊断”证明地址有效、已初始化或 dependency-free。

退休是逻辑状态变化，不要求清零原字节。未跟踪 raw alias 的重复取出可能无法静态发现；
那是违反 unsafe 前置条件，不是本方案提供的静态安全保证。已有静态冲突仍必须拒绝。
本草案不新增 raw 槽位重新初始化语法。

## 3. 值类别与来源不得混淆

- Copy 元素可以通过复制值产生结果，但取出操作仍使原槽位逻辑退休；不是普通读取。
- NonCopy 元素转移完整值。没有 Drop hook 不代表 Copy，也不能凭空增加清理责任。
- unique/shared 元素交接其原有 owning handle；shared take 不额外 retain。
- raw/reference/callable 元素保留原有 identity、consuming mode、环境责任及依赖；
  不因操作而获得 payload ownership、写权限或更长生命周期。
- “独立结果”只指值与其清理责任完成交接，**不表示依赖消失**。带借用字段的值必须
  保留实际 referent/dependencies；不能从类型签名补造实际来源。

原 raw source 在操作自身记录中仍是槽位来源，不能改写成 `NoSourcePlace`。
只有成功操作的结果，才在外层初始化中作为新的无既存 source place 结果处理。
是否具备 whole-owned-temporary eligibility 仍由其真实 ownership、完整依赖证明
及现有 planner 决定；依赖未知或不满足该矩阵时继续拒绝。

## 4. Vec 对接与失败边界

预期用途是 Vec 的末元素取出，而不是通过识别 `len -= 1` AST 来授权。
Vec 实现必须自行维持“取出一个元素，剩余清理只覆盖 remainder”的不变量。

已接受的最小运行时契约：地址、索引及检查先求值一次，随后 take 本体不分配、
不调用用户代码、不析构、不 retain、不抛出／展开、不引入 suspension/cancellation 点。
这不是并发原子性保证；并发访问仍由 unsafe 调用方负责。
Vec 可在完成边界与空检查后，保存末槽地址、缩减 live prefix，再执行 take；
缩减到交接之间不得插入可失败或可展开的工作。取出后若构造包装结果失败，
已取出的临时值仍须有唯一清理者，容器清理不得再次包含它。

这只是待验证的集成顺序，不是现有实现事实。非连续 live 集合仍由容器独立管理；
`free[count]` 不升级成静态 extent 证明。无效地址/未初始化等 unsafe 违约不承诺运行时回滚。
编译期拒绝必须维持现有 Sema 失败原子性且不生成 artifact。

## 5. Plan / Evidence 边界

必须区分以下事实，而非合成一个“已证明初始化”布尔值：

- 契约身份、精确源表达式与投影、已解析元素类型及 morphology；
- `InitializationBasis = UnsafeCallerPrecondition`，并列出调用方承担的条件；
- source slot retirement、结果 value production、原有 Drop liability 的交接；
- 实际 referent/dependencies、已知 PAL 检查、remainder 由调用方维护；
- 操作结果到外层 destination edge 的关联，以及最终验证／拒绝结果。

这些是待实现字段需求，不冻结新公开协议。语义 edge identity 不等于运行时地址、
动态 extent 或每次循环执行的唯一实例身份；不能拿它证明两个 raw 指针不别名。

CodeGen 只能消费正常 Sema 验证成功的精确操作计划；missing/rejected/mismatch
必须 fail-closed，不能重建 ownership 或初始化事实。类型／来源在某一路由不可证明时，
该路由暂不激活；不通过扩大 TKI 或回退函数名识别绕过。

## 6. 验证矩阵（接受后实施）

| 场景 | 必须验证的结果 |
|---|---|
| Copy 与无 Drop 的 NonCopy 元素 | 正确 value production；两者槽位均逻辑退休；不虚构 Drop。 |
| Drop-bearing / unique / shared 元素 | 结果 exact-once；原槽位不再清理；shared 无多余 retain；allocation 保留。 |
| Vec 空、单元素、多元素，多次 pop 后销毁 | 空路径不执行 take；Some/None 正确；取出结果与 remainder 各 exact-once，无泄漏。 |
| 操作数有副作用 | 地址与索引只求值一次；取出后包装／提前退出保持责任唯一。 |
| borrowed 字段或 raw/reference 元素 | 实际来源不丢失；合法依赖保持；局部逃逸、未知来源、权限提升拒绝。 |
| 普通／consuming callable 元素 | 模式和环境责任保持；普通转移不多 retain；禁止降级或重复消费。 |
| 已知活跃借用、类型／morphology 不匹配 | 拒绝、状态回滚、无 artifact；unsafe 不取消这些检查。 |
| 无 explicit take 的 raw 索引／普通 unsafe／同名用户函数 | 不获得本契约准入；原索引不变成 NoSourcePlace。 |
| 未跟踪重复 take、未初始化槽位、错误 remainder count | 明确属于 unsafe 违约；不将其列为“编译器必须静态检出”的测试承诺，也不以偶然运行成功为合格。 |
| result 初始化记录 | 保留原操作 source，并单独关联结果 edge；temporary eligibility 不绕过依赖门禁。 |
| missing/rejected/mismatched plan | CodeGen 拒绝且无 `.o`/IR artifact；不得现场补计划。 |
| 既有 raw/managed source 对照 | 普通 cede、managed partial move、free[count]、init 行为不扩大。 |

运行期正例用析构计数及可用的内存检查器核验；既有 callable/binding/disposition
专项必须保持通过。先定向，再恢复工具构建与失败 CTest，最后一轮全量 PASS/FAIL；
不得通过逐个排除标准库或改写历史 oracle 假造恢复。

## 7. 首批实施边界与顺序

按实例的证明能力开放，不使用类型名称白名单。先覆盖 Copy、无 Drop 的 NonCopy、
完整 owning 值及 unique/shared；raw/reference/callable 和借用字段只有在实际依赖
完整传播时才接纳，否则拒绝。泛型定义可以延迟，实例化不得留下未解析计划。
已知来源指向 compiler-managed 存储时必须衔接其失效／cleanup tracking；首批不能
衔接就拒绝。不得为恢复 Vec<T> 将全部 T 视为 dependency-free。

实施顺序：独立原语及 fault gates → 最小 Vec 对接 → 工具和失败 CTest → 一轮全量。
不再实验性放行旧 `cede raw[index]`，不建立完整动态 extent 系统；接受本契约不等于
接受 binding 切片，也不将 unsafe 前置条件记为静态 initialized-extent 证明。

## 8. 实施状态（2026-09-07，未验收）

已接入专用 token/AST、Sema 操作验证、内部来源记录及前端专项。当前 origin provider
只接纳 immutable raw binding 的 alloc 初始化链或 raw 形参；重绑定、managed 来源、
不完整依赖和投影基址仍保守拒绝。此限制是首批实现状态，不是新的永久语言规则。
原始槽位与结果 edge 分别记录；初始化依据明确为 `UnsafeCallerPrecondition`。

初版执行 lowering 补丁曾被自动审查拒绝，**该历史补丁未应用**。拒绝理由是只加载结果而没有实现
live-set 更新／remainder 清理可能导致重复析构。已接受设计将 raw live-set 与
remainder 维护作为显式 unsafe 调用方责任，同时拒绝无法衔接的已知 managed 来源；
随后用户逐项审查并授权制作新的候选，要求 Production 正向准入和严格寻址。
新候选已通过编辑审批流程实施，未通过其他工具绕过原拒绝。

初版 CodeGen 的无条件拒绝桩现已由新候选替换；开发期 artifact 拒绝围栏已改为
真正的 runtime／fault gates，详见[当前 lowering 候选](review/raw_take_lowering_candidate.md)。
原语首批 lowering 已经用户独立复审 Accepted，仅限已列范围；后续 Vec::pop
对接单列验证，不视为 binding 整体或通用泛型 ABI 已经接受。

初版前端验证记录：tokac 增量构建通过；前端 2 个类型检查正例、7 个拒绝场景及
normal/shadow 对照通过；与 pure planner、binding、standalone、dyn fn lifecycle、
negative-termination 一起运行的定向 CTest 为 6/6（70.29 秒）。`diff --check` 通过。
未运行全量 PASS/FAIL、工具构建或 previously-failing CTest 集成组；没有提交或推送。

新 lowering 候选验证：tokac 增量构建通过；11 个拒绝场景、6 个运行程序、
13 类 fault × 2 类元素 × object/IR = 52 个不产物检查通过。最终定向 CTest
6/6（104.38 秒），包含上述原语专项及原有五项底线；并非完整 CTest 或全量 PASS/FAIL。
同步 morphic consuming shared 参数 ABI 仍是独立问题；本原语对该未 qualification
的函数体组合 fail-closed，concrete unique/shared 元素的运行正例不代表该 ABI 已修复。
