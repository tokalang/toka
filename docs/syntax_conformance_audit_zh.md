# Toka 语法契约审计

审计日期：2026-06-29

本文以 `docs/syntax.md` 与 `docs/syntax_zh.md` 为公开语法声明源，对照当前编译器实现、标准库用例与测试集，检查语法设计是否已经被测试锁定，以及哪些规则仍需要补充更直接的 pass/fail 用例。

本审计不改变语言语义。它的目标是帮助 Toka 进入“1.0 语义闭合”阶段：少增加新语法，多确认已有语法是否自洽、可解释、可维护。

## 输入范围

- 公开语法文档：`docs/syntax.md`、`docs/syntax_zh.md`
- 测试集：`tests/pass/*.tk` 约 284 个，`tests/fail/*.tk` 约 138 个
- 标准库与工具库：`lib/**/*.tk` 约 89 个
- 辅助验证：已有 `test_pass.sh`、TKI cache validation、incremental build 流程

## 总体结论

当前公开语法整体是自洽的，没有发现需要立即回滚或重写的大块设计。

已经可以认为基本闭合的区域：

- `auto` 绑定、`#` 可变权限、nullable payload / nullable handle
- payload / handle 分层，以及 `&`、`*`、`^`、`~` 的基本使用
- `cede` 调用契约与未消费检查
- shape 具名初始化、默认字段、禁止位置初始化
- trait `@Trait`、trait facet set `@{A, B}`、`where:` 约束
- 关联类型 `type` / `per type`
- morphic generic / morphic field 的基本规则
- named destructuring、match、range match、loop / for
- closure 与 `dyn fn`
- raw pointer / unsafe / FFI 的基本路径

仍需要补锁的区域主要不是“功能缺失”，而是“规则组合缺少专门测试”：

- `@encap` 的 `pub(path)`、`pub(crate)`、`pub * ! ...` 跨模块访问矩阵已补最小正反测试；后续可继续细化 crate 边界模型
- `dyn @{A, B}` 作为 trait object 被拒绝的直接 fail case
- 常见错误表里的若干项缺少专门 fail 测试，例如 `let` / `var`、`for x in ...`、字符串 `+`
- hatted 参数的“契约义务”目前只部分被诊断覆盖，仍需决定哪些情况是 error、warning，还是仅作为风格规则
- TKI / 跨模块场景下的 associated type、dyn trait、visibility 组合还需要更直接的用例

## 逐节审计矩阵

| 语法章节 | 状态 | 证据 | 缺口 / 建议 |
| --- | --- | --- | --- |
| 1. Core Model | 基本锁定 | pointer、member、morphic、PAL 相关 pass/fail 已覆盖 payload/handle 差异 | 建议新增一个最小化“裸名读 payload、帽子读 handle、带帽赋值重绑定”的单文件说明性 pass/fail 对 |
| 2. Files / Imports / Entry | 基本锁定 | `g03_import_item.tk`、`g03_module.tk`、relative import、import fail cases | `pub import` 的 TKI 导出行为可补一个更直接的跨模块 snapshot 测试 |
| 3. Bindings / Mutability / Nullability | 基本锁定 | nullable、borrow、mutation、strict pointer、null fail cases 均存在 | nullable handle 可按 `*` / `^` / `~` 做更小的矩阵化测试 |
| 4. Hats / Handles | 基本锁定 | raw pointer、smart pointer、rebind、member hat、PAL stress 均有用例 | hatted 参数的“用或传递”义务尚未形成完整诊断矩阵 |
| 5. Functions / Parameters / `cede` | 基本锁定 | `cede_param_missing`、`cede_param_unconsumed`、`cede_non_cede_parameter`、default args、effects tests | 需要补“签名要求 cede，函数内部仅检查但不转移”的更细粒度错误说明；hatted non-cede 参数是否必须使用 handle 仍是规则决策点 |
| 6. Shapes / Enums / Init | 锁定较好 | named init、default field、positional init fail、alias/newtype tests | enum-like variant 的 fail 侧可再补未覆盖的错误形态 |
| 7. Methods / Traits / Encapsulation | 功能强，组合继续收紧 | trait、trait bounds、where、associated types、dyn trait、encap visibility 均有测试 | `@encap` 可见性矩阵和 `dyn @{A, B}` 拒绝已补最小锁；dyn trait 跨模块 visibility 仍可继续细化 |
| 8. Member Access / Morphic Fields | 锁定较好 | `g08_member_audit`、`g08_member_parsing`、`g08_morphic_member_identity`、morphic type-side fail cases | 建议补一个专门 fail：`box.'field` 与 `box.field` 在不匹配使用处的错误对照 |
| 9. Generics | 锁定较好 | rigid generic、morphic generic、generic alias/newtype、trait bounds、sizeof tests | 可补 TKI 下 generic associated type projection 的跨模块用例 |
| 10. Control Flow | 锁定较好 | loop、conditional loop、for、while fail、match range fail | `for x in iter` 已补专门 fail case；后续可继续细化 iterator trait 相关错误 |
| 11. Pattern Matching / Destructuring | 锁定较好 | named destructuring、elision、wildcard、resource destruct fail cases | 有些测试注释仍带“proposal/goal”历史语气，后续可清理注释避免误导 |
| 12. Closures | 基本锁定 | explicit / implicit params、capture list、`dyn fn`、escape fail | capture mode 的失败矩阵可更细：`copy`、`cede`、`~`、borrow escape |
| 13. Strings / Formatting | 基本锁定 | string API、println placeholder mismatch、UTF-8 str index fail | `string + string` 已补直接 fail case；`str + str` 可按需再补 |
| 14. Unsafe / FFI | 基本锁定 | extern fn、大量 sys/libc 用例、raw alloc/free、cast、unsafe escape fail | public raw pointer API 限制已有 fail；建议补“raw/unsafe 命名豁免”正例或明确暂不豁免 |
| 15. Common Mistakes | 基本有测试锚点 | `while`、position init、type-side `'T`、type-side `&T`、`let` / `var`、`for x in ...`、`dyn @{...}`、字符串 `+` 均已有 fail | 当前主要缺口转为诊断质量：部分错误仍落在较通用的 parser/sema 诊断上 |

## 高优先级补测清单

### P1：应尽快补的语法锁

1. `dyn @{A, B}` 拒绝

   当前文档明确说稳定 trait object 语法只支持单个 facet `dyn @Trait`。已新增最小 fail 锁：

   - `tests/fail/dyn_trait_set_object.tk`

2. `@encap` path / crate / wildcard 可见性矩阵

   当前 `pub(crate)` 在 `g08_encap_syntax.tk` 中出现，`pub(std)`、`pub *` 在标准库和若干 pass 测试中使用。已新增一个最小跨模块矩阵：

   - `tests/import_test/encap_visibility_lib.tk`
   - `tests/pass/encap_visibility_matrix.tk`
   - `tests/fail/encap_visibility_path_denied.tk`
   - `tests/fail/encap_visibility_wildcard_exclusion.tk`

3. 常见错误 fail cases

   文档常见错误表中仍有几项需要最小 fail 锁。已新增：

   - `tests/fail/let_var_binding.tk`
   - `tests/fail/for_missing_auto.tk`
   - `tests/fail/string_concat_plus.tk`

   其中 `let` / `var` 和 `dyn @{...}` 当前已经被拒绝，但诊断还比较通用；后续可作为诊断质量改进项。

4. hatted parameter obligation

   当前已有 `param_type_side_reference.tk` 与 `redundant_param_borrow.tk`，可以拒绝 `info: &Info` 和无意义的 `&info: Info`。同时编译器已经增加 `W0407`，当函数参数声明了 handle morphology 但函数体没有使用 handle 视图时，会给出 warning。

   这还不是 hard error。更一般的规则仍需形成设计决议：

   - 如果作为 hard error：补 `tests/fail/hatted_param_unused.tk`
   - 如果保持 warning：补 warning snapshot 或诊断测试工具
   - 如果仅作为风格规则：文档需要明确“不保证诊断”

### P2：适合随后补的组合测试

1. Associated type + TKI + cross-module

   当前 associated type 有基础 pass/fail，标准库 `ReadDir@Iterator::Item` 也有使用。但可以补一个专门跨模块导入 `.tki` 后使用 projection 的测试，防止后续 TKI exporter / parser 回归。

2. `where:` + trait prerequisite + generic impl

   现有 `where` 测试覆盖了 shape、function、trait prerequisite。建议补一个 generic impl 中的 `where:`，确认约束传递与 impl 选择不会漂移。

3. `dyn @Trait` + visibility + module boundary

   当前 `dyn_privacy.tk` 能锁 private trait method 调用，但不是完整的跨模块 visibility matrix。建议补同模块 / 跨模块各一例。

4. Closure capture fail matrix

   目前 closure 功能可用，但 capture mode 的错误侧还可以更直接，尤其是 `copy ~r`、`cede env`、borrow escape 的组合。

5. Nullable handle matrix

   将 `nul *p`、`nul ^p`、`nul ~p`、非 nullable handle 赋 `null` 分成小测试，错误信息会更稳定。

## 细节复查补充

### Trait facet 与 `where:`

`trait @Name` 是当前唯一公开的 trait 声明形态，裸 `trait Name` 已有 `tests/fail/trait_requires_at.tk` 拒绝。Facet set 的公开规则是 `@{Trait1, Trait2}`，集合内部使用裸 trait 名称；`T impl @{Send, Sync}` 已由 `tests/pass/g08_where_trait_bounds.tk` 覆盖。

这部分语法目前自洽。后续更值得补的是组合场景，而不是改语法：generic impl 中的 `where:`、associated type projection 经过 TKI replay、以及 dyn trait 跨模块 visibility。

### Morphic 与内部可变性标记

`'T` / `'field` / `'param` 的规则已经比较一致：单引号属于 morphic 绑定名，不属于类型侧。`tests/pass/g08_generic_morphic.tk` 覆盖了 morphic field 与 morphic parameter 的正例，type-side 错误已有 fail case。

`#` 的位置规则也基本自洽：名字侧 `x#` 表示 payload/soul 可写，帽子侧 `^#p` / `*#p` / `&#p` 表示 handle identity 可重绑定。实现层已有 `BindingPermission` 和 TKI exporter 对这些标记做结构化保存。缺口不是语义定义，而是矩阵测试仍偏分散：后续可以新增一个只覆盖 `x#`、`^#x`、`x$`、`^$x` 的小型 pass/fail 组。

`W0407` 是 hatted 参数义务的第一步工程化：它只提示“签名声明了 handle，但函数体没有使用 handle 视图”，不禁止 payload 读取，也不把该规则升级为错误。这样可以先暴露可疑签名，同时避免误伤 raw buffer、FFI adapter、测试和标准库中的历史写法。

### `@encap pub(path)` 当前边界

当前 `pub(path)` 的语义实现以访问点源文件路径包含 target string 作为授权判断，因此它更接近“源路径目标授权”，还不是完整 module identity 模型。新增测试使用 `pub(encap_visibility_matrix)` 避开关键字路径段，并锁定当前实现行为。

建议：短期保持文档的“模块路径授权”说法，但不要继续扩大承诺；中期若要把 `pub(path)` 做成真正的 module path，需要把 target path 的解析、归一化和 Sema 当前模块身份统一起来。

## 文档表述风险

### 1. `@encap` wildcard 和 path visibility

文档已经写入 `pub *`、`pub * ! field`、`pub(path)`。标准库里有 `pub *`、`pub(std)` 的真实用法，新增测试也已经覆盖排除式 wildcard 和 path-targeted access 的最小正反行为。

建议：文档可以保持，但不要继续扩大 `pub(path)` 的承诺；当前实现仍是源文件路径子串匹配，未来若要升级成严格 module identity，需要单独设计。

### 2. hatted 参数契约

文档表达“只有函数确实需要 handle 本体时才加帽”。这与 Toka 的设计方向一致，但目前还不是完整的编译器义务体系。

建议：下一轮语义冻结时明确：

- 参数带帽但未使用 handle，是 error 还是 warning？
- 参数带帽但只 payload-read，是否一律拒绝？
- 参数带帽后原样传递给另一个函数，是否满足义务？
- `cede` 与 hatted 参数同时出现时，义务如何叠加？

### 3. `dyn @Trait`

文档清楚限制为单 facet，但缺少 fail case。这个点很适合小提交补齐。

### 4. Common Mistakes

常见错误表已经很有价值，但若它公开存在，最好让每一行至少有一个 fail 测试或明确的 parser diagnostic。否则开发者会把它理解成硬承诺。

## 推荐下一阶段路线

### 阶段 A：补最小语法锁

先补 P1 的几个 fail/pass 测试，不改语义，除非发现编译器实际接受了文档明确拒绝的语法。

目标：让公开语法文档中的硬规则都有测试证据。

### 阶段 B：处理 hatted 参数义务

这是当前最需要设计判断的点。它连接了 payload/handle 分层、参数原地捕获、`cede`、PAL、可读性和诊断体验。

目标：确定“带帽即契约”的编译器级规则边界。

### 阶段 C：跨模块 / TKI 组合测试

补 associated type、dyn trait、visibility、where 在 TKI replay 场景下的正反测试。

目标：确保单文件语法规则不会在增量构建和缓存接口中漂移。

### 阶段 D：文档回填

测试补齐后，再同步 `docs/syntax.md` 与 `docs/syntax_zh.md`，把仍是风格建议的地方和已经是编译器硬规则的地方分清楚。

## 当前判断

Toka 当前语法设计不是“散的”。相反，它已经有一个很清楚的中心：

- 裸名是 payload
- 帽子是 handle
- `cede` 是资源转移契约
- trait facet 使用 `@`
- morphic quote 保留 handle shape
- `where:` 承载显式约束

下一步最值得做的是把这些规则的边界用测试钉死，而不是继续扩展新表面语法。
