# Toka 语法契约审计

初始审计日期：2026-06-29
当前更新日期：2026-07-03

本文以 `docs/syntax.md` 与 `docs/syntax_zh.md` 为公开语法声明源，对照当前编译器实现、标准库用例与测试集，检查语法设计是否已经被测试锁定，以及哪些规则仍需要补充更直接的 pass/fail 用例。

本审计不改变语言语义。它的目标是帮助 Toka 进入“1.0 语义闭合”阶段：少增加新语法，多确认已有语法是否自洽、可解释、可维护。

## 输入范围

- 公开语法文档：`docs/syntax.md`、`docs/syntax_zh.md`
- 测试集：`tests/pass/*.tk` 约 301 个，`tests/fail/*.tk` 约 181 个
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
- PAL 主体：局部路径借用、move / `cede`、分支合并、loop / for 回边、`break` /
  `continue` 状态合并已经形成同一套保守安全模型

仍需要补锁的区域主要不是“功能缺失”，而是“更高阶规则组合缺少专门测试”：

- `@encap` 的 `pub(path)`、`pub(crate)`、`pub * ! ...` 跨模块访问矩阵已补最小正反测试；后续可继续细化 crate 边界模型
- `dyn @{A, B}` 作为 trait object 被拒绝的直接 fail case 已补；`dyn @Trait` 跨模块 pub/private 方法边界也已补最小正反测试
- 常见错误表里的 `let` / `var`、`for x in ...`、字符串 `+` 已补最小 fail 测试
- hatted 参数的“契约义务”已按 warning 级规则冻结：`&param` 冗余写法直接拒绝，`*` / `^` / `~` 参数只读 payload 时触发 `W0407`
- TKI / 跨模块场景下的 associated type projection 已补最小 source-less `.tki` 回放用例；dyn trait、visibility、where 的 TKI replay 组合仍可继续细化

## 逐节审计矩阵

| 语法章节 | 状态 | 证据 | 缺口 / 建议 |
| --- | --- | --- | --- |
| 1. Core Model | 基本锁定 | pointer、member、morphic、PAL 相关 pass/fail 已覆盖 payload/handle 差异；最小 payload/handle 视图对照已补 | 后续缺口转向更细的诊断质量，而不是核心规则本身 |
| 2. Files / Imports / Entry | 基本锁定 | `g03_import_item.tk`、`g03_module.tk`、relative import、import fail cases；`pub import` source-less TKI re-export 已补 | 后续可按需细化多级 re-export 与 wildcard import 的组合 |
| 3. Bindings / Mutability / Nullability | 基本锁定 | nullable、borrow、mutation、strict pointer、null fail cases 均存在 | nullable handle 的 raw / unique / shared 正例和非 nullable 反例已补最小矩阵；`nul &` 已锁为非法 |
| 4. Hats / Handles | 基本锁定 | raw pointer、smart pointer、rebind、member hat、PAL stress、hatted parameter warning matrix 均有用例 | 后续可转向 handle 组合场景，而不是基础帽子规则 |
| 5. Functions / Parameters / `cede` | 锁定较好 | `cede_param_missing`、`cede_param_unconsumed`、`cede_non_cede_parameter`、default args、effects tests | 后续主要是诊断措辞与组合矩阵细化；核心 cede 契约已经实施 |
| 5a. PAL / Borrow Safety | 主体冻结 | `g08_pal_stress_test`、branch restore、loop local move、labeled break / continue、borrowed-field escape、borrow / move fail cases | 后续转向精度审计与诊断，不继续扩展主体规则 |
| 6. Shapes / Enums / Init | 锁定较好 | named init、default field、positional init fail、alias/newtype tests；enum variant constructor 失败矩阵已补 | 后续可转向 variant pattern 诊断细化 |
| 7. Methods / Traits / Encapsulation | 功能强，组合继续收紧 | trait、trait bounds、where、associated types、dyn trait、encap visibility 均有测试 | `@encap` 可见性矩阵与 TKI replay、`dyn @{A, B}` 拒绝、`dyn @Trait`、generic impl `where:` 的 TKI replay 已补最小锁；后续转向更高阶交叉组合 |
| 8. Member Access / Morphic Fields | 锁定较好 | `g08_member_audit`、`g08_member_parsing`、`g08_morphic_member_identity`、morphic type-side fail cases；`box.'field` / `box.field` 错误对照已补 | 普通字段误写 `.'field` 已收紧为专用诊断；后续转向更高阶组合 |
| 9. Generics | 锁定较好 | rigid generic、morphic generic、generic alias/newtype、trait bounds、sizeof tests；associated type projection 的 source-less TKI replay 已补 | 后续可转向更高阶 generic / trait / TKI 组合 |
| 10. Control Flow | 锁定较好 | loop、conditional loop、for、while fail、match range fail | `for x in iter` 已补专门 fail case；后续可继续细化 iterator trait 相关错误 |
| 11. Pattern Matching / Destructuring | 锁定较好 | named destructuring、elision、wildcard、resource destruct fail cases；历史提案式测试注释已清理 | 后续可转向 exhaustiveness 与资源析构组合诊断 |
| 12. Closures | 基本锁定 | explicit / implicit params、capture list、`dyn fn`、escape fail；resource copy capture 已拒绝浅拷 | capture mode 后续可继续细化非 `dyn fn` 闭包的局部/逃逸边界 |
| 13. Strings / Formatting | 基本锁定 | string API、println placeholder mismatch、UTF-8 str index fail；`string + string` 与 `str + str` 均已补直接 fail case | 后续可继续细化格式化错误诊断 |
| 14. Unsafe / FFI | 基本锁定 | extern fn、大量 sys/libc 用例、raw alloc/free、cast、unsafe escape fail；public raw pointer API 的拒绝与命名豁免均已脚本锁定 | 后续可继续细化 unsafe 边界诊断措辞 |
| 15. Common Mistakes | 基本有测试锚点 | `while`、position init、type-side `'T`、type-side `&T`、`let` / `var`、`for x in ...`、`dyn @{...}`、字符串 `+` 均已有 fail | 当前主要缺口转为组合矩阵：`let` / `var`、`dyn @{...}`、普通字段误写 `.'field` 均已收紧为专用 diagnostic |

## 高优先级补测清单

### P1：已补齐的最小语法锁

1. Core payload / handle view contrast

   已新增一个最小 pass / fail 对：

   - `tests/pass/g01_core_handle_views.tk`
   - `tests/pass/g01_handle_hash_position_matrix.tk`
   - `tests/fail/core_handle_rebind_requires_hash.tk`
   - `tests/fail/core_handle_suffix_hash_not_rebindable.tk`
   - `tests/fail/core_raw_suffix_hash_not_rebindable.tk`
   - `tests/fail/core_reference_suffix_hash_not_rebindable.tk`

   这直接锁住核心模型的两条基础规则：裸名访问 payload 视图，带帽赋值操作 handle identity；如果 handle binding 没有 identity-side `#` 重绑定权限，则不能通过 `^name = ...`、`*name = ...`、`&name = ...` 改写 handle。`^p#`、`*p#`、`&p#` 只给 payload / soul 侧可写权限，不等价于 `^#p`、`*#p`、`&#p`。

2. `dyn @{A, B}` 拒绝

   当前文档明确说稳定 trait object 语法只支持单个 facet `dyn @Trait`。已新增最小 fail 锁：

   - `tests/fail/dyn_trait_set_object.tk`

3. `@encap` path / crate / wildcard 可见性矩阵

   当前 `pub(crate)` 在 `g08_encap_syntax.tk` 中出现，`pub(std)`、`pub *` 在标准库和若干 pass 测试中使用。已新增一个最小跨模块矩阵：

   - `tests/import_test/encap_visibility_lib.tk`
   - `tests/pass/encap_visibility_matrix.tk`
   - `tests/fail/encap_visibility_path_denied.tk`
   - `tests/fail/encap_visibility_wildcard_exclusion.tk`

4. 常见错误 fail cases

   文档常见错误表中仍有几项需要最小 fail 锁。已新增：

   - `tests/fail/let_var_binding.tk`
   - `tests/fail/for_missing_auto.tk`
   - `tests/fail/string_concat_plus.tk`

   其中 `let` / `var` 已收紧为 `E01112` / `E01244` 专用 parser diagnostic，`dyn @{...}` 已收紧为 `E01245`，普通字段误写 `.'field` 已收紧为 `E04580` 专用 Sema diagnostic。`for x in ...` 和字符串 `+` 也已有明确错误码，避免落到泛泛的级联错误。

5. hatted parameter obligation

   当前已有 `param_type_side_reference.tk` 与 `redundant_param_borrow.tk`，可以拒绝 `info: &Info` 和无意义的 `&info: Info`。同时编译器已经增加 `W0407`，当函数参数声明了 handle morphology 但函数体没有使用 handle 视图时，会给出 warning。

   这条规则已经按 warning 级契约冻结，不升级为 hard error。已补 `tools/scripts/test_verify_warn.py` 与 `tests/warn/hatted_param_handle_unused.tk`，并精确锁定 raw / unique / shared 三类 handle 参数：只读 payload 的 hatted 参数会触发 `W0407`，显式使用 handle 视图（如 `*s == null`）以及把 handle 视图继续传递给另一个 hatted 参数（如 `accept_raw(*s)`）都满足 handle 使用义务。

### P2：适合随后补的组合测试

1. Associated type + TKI + cross-module

   当前 associated type 有基础 pass/fail，标准库 `ReadDir@Iterator::Item` 也有使用。已在 `tools/scripts/test_tki_cache_validation.sh` 增加 source-less `.tki` 回放用例：先把定义 `trait @Readable`、`type Item`、`impl IntBox@Readable { type Item = i32 }` 的模块编译为 `.o + .tki`，再移走 `.tk` 源文件，让主程序通过接口缓存解析 `IntBox@Readable::Item` 并调用 `read()`。

2. `where:` + trait prerequisite + generic impl

   现有 `where` 测试覆盖了 shape、function、trait prerequisite。已新增 generic impl 的正反锁：

   - `tests/pass/g08_where_generic_impl.tk`
   - `tests/fail/where_generic_impl_unsatisfied.tk`

   这确认了 `impl<T> Box<T> where: T: @Marked` 在满足约束时可实例化方法，在不满足约束时不会让该 impl 泄漏为可用方法。

3. `dyn @Trait` + visibility + module boundary

   当前 `dyn_privacy.tk` 能锁 private trait method 调用。已新增一个最小跨模块矩阵：

   - `tests/import_test/dyn_visibility_lib.tk`
   - `tests/pass/dyn_trait_cross_module_visibility.tk`
   - `tests/fail/dyn_trait_cross_module_private_method.tk`

   这确认了 `dyn @Trait` 参数可以跨模块动态派发 trait 中的 `pub fn`，同时 trait 中的 private method 不会在导入模块通过 dyn 对象泄漏为可调用方法。

4. Closure capture fail matrix

   已补最小 fail 锁：

   - `tests/fail/closure_cede_capture_consumes.tk`
   - `tests/fail/closure_dyn_implicit_capture_escape.tk`
   - `tests/fail/closure_fn_implicit_capture_escape.tk`

   这确认了 `[cede env]` 会消费外层变量，转换为 `dyn fn` 的 owned closure
   不能隐式捕获外部变量，普通 `fn` 闭包若带隐式借用捕获也不能作为返回值逃逸。
   需要保存闭包状态时应显式 `[cede ...]` 或 `[copy ...]`。本轮补上
   `tests/fail/closure_copy_capture_resource.tk`，锁定 `[copy ...]` 不能浅拷资源所有权。
   后续如果继续细化，应集中在 async suspension/capture 边界，而不是基础闭包逃逸。

5. Nullable handle matrix

   `tests/pass/g08_polymorphic_null.tk` 已覆盖 `nul *`、`nul ^`、`nul ~` 接受 `null`。本轮新增：

   - `tests/fail/non_null_unique_null.tk`
   - `tests/fail/non_null_shared_null.tk`
   - `tests/fail/nullable_borrow_handle.tk`

   这补上了非 nullable unique/shared handle 不能接收 `null`，以及 borrow handle `&` 不能被 `nul` 标记的最小反例。raw pointer 的非 nullable 反例已有 `tests/fail/non_null_nullptr.tk` / `tests/fail/strict_null_identity.tk`。

6. Morphic member view contrast

   `tests/pass/g08_morphic_member_identity.tk` 已覆盖 `box.'data` 保留 handle 形态的正例，`tests/pass/g08_generic_morphic.tk` 已覆盖 `box.data` 请求 payload 视图的正例。本轮新增：

   - `tests/fail/morphic_member_missing_quote.tk`
   - `tests/fail/morphic_member_quote_on_plain_field.tk`

   这确认了 morphic 字段漏写 quote 时不能把 payload 视图当作 handle 形态返回；普通字段误写 `.'field` 时也不会被当成 morphic identity，并会触发 `E04580` 专用诊断，提示应使用普通字段访问 `.field`。

7. `pub import` through source-less TKI

   已在 `tools/scripts/test_tki_cache_validation.sh` 增加 `Test 7.14`：底层模块导出 `base_value`，中间模块通过 `pub import ./base::{base_value}` 重导出；生成 `.o + .tki` 后移走底层和中间 `.tk` 源文件，主程序只能通过中间 `.tki` 的 re-export 记录解析并调用该符号。

   这确认了 declaration-level `pub import` 不只是 source 模式可见，也会进入模块接口缓存；增量构建或 source-less replay 不会丢失 re-export 契约。

8. Enum variant constructor failures

   已新增：

   - `tests/fail/enum_variant_unknown.tk`
   - `tests/fail/enum_variant_unit_args.tk`
   - `tests/fail/enum_variant_arg_mismatch.tk`

   审计中发现 `Shape::Variant(...)` 调用路径原本只检查实参表达式，没有验证 variant payload 形状，导致 unit variant 带参数、payload variant 参数个数错误仍可通过。已在 Sema 的 enum variant constructor 分支补上 `E0550` / `E0551` 检查，并保留未知变体的 `E04551` 诊断。

9. `dyn @Trait` through source-less TKI

   已在 `tools/scripts/test_tki_cache_validation.sh` 增加 `Test 7.15`：模块定义 `pub trait @VisibleShape`、`DynBox` 与 `DynBox@VisibleShape` 实现，生成 `.o + .tki` 后移走 `.tk` 源文件。主程序通过 `.tki` 中的 trait/interface 信息调用 `dyn @VisibleShape` 的 `pub fn public_id`，并确认 private method 不会在 source-less replay 下泄漏为可调用方法。

   这说明当前 `dyn @Trait` 的基础跨模块语义不需要重新讨论；后续若要讨论，应集中在新能力，例如是否支持 multi-facet trait object、object lifetime/ownership 表达、或 dyn object ABI 稳定性。

10. Generic impl `where:` through source-less TKI

   已在 `tools/scripts/test_tki_cache_validation.sh` 增加 `Test 7.16`：模块定义 `impl<T> Box<T> where: T: @Marked`，生成 `.o + .tki` 后移走 `.tk` 源文件。满足约束的 `Box<Token>.marker()` 可以通过接口缓存编译并运行；不满足约束的 `Box<Plain>.marker()` 在 source-less replay 下仍被拒绝。

   当前 TKI 会把 `where: T: @Marked` 规范化为接口中的 `impl<T: @Marked> Box<T>`。这没有改变语义，并让接口文本与推荐约束风格保持一致。

11. `@encap` visibility through source-less TKI

   已在 `tools/scripts/test_tki_cache_validation.sh` 增加 `Test 7.17`：模块定义带 `impl VisibilityBox@encap { pub open_val }` 的 shape，生成 `.o + .tki` 后移走 `.tk` 源文件。主程序可通过接口缓存访问 `open_val`，但访问未授权的 `secret_val` 仍会失败。

   这确认了 `@encap` 字段可见性不是仅在源码解析时生效，而是模块接口契约的一部分。

## 细节复查补充

### Trait facet 与 `where:`

`trait @Name` 是当前唯一公开的 trait 声明形态，裸 `trait Name` 已有 `tests/fail/trait_requires_at.tk` 拒绝。Facet set 的公开规则是 `@{Trait1, Trait2}`，集合内部使用裸 trait 名称；`T: @{Send, Sync}` 已由 `tests/pass/g08_where_trait_bounds.tk` 覆盖。历史 `T impl @Trait` 形式仍作为兼容写法解析，但公开推荐形式统一为 `:`。

这部分语法目前自洽。generic associated type projection 的 source-less TKI replay、以及 dyn trait 跨模块 visibility 均已有最小锁。后续更值得补的是更高阶组合场景，而不是改基础写法。

### Morphic 与内部可变性标记

`'T` / `'field` / `'param` 的规则已经比较一致：单引号属于 morphic 绑定名，不属于类型侧。`tests/pass/g08_generic_morphic.tk` 覆盖了 morphic field 与 morphic parameter 的正例，type-side 错误已有 fail case。

`#` 的位置规则也基本自洽：名字侧 `x#` 表示 payload/soul 可写，帽子侧 `^#p` / `*#p` / `~#p` / `&#p` 表示 handle identity 可重绑定。实现层已有 `BindingPermission` 和 TKI exporter 对这些标记做结构化保存。`tests/pass/g01_core_handle_views.tk`、`tests/pass/g01_handle_hash_position_matrix.tk`、`tests/fail/core_handle_rebind_requires_hash.tk`、`tests/fail/core_handle_suffix_hash_not_rebindable.tk`、`tests/fail/core_raw_suffix_hash_not_rebindable.tk` 与 `tests/fail/core_reference_suffix_hash_not_rebindable.tk` 已经补上 payload / handle 视图对照，并锁住 `^p#` / `*p#` / `&p#` 不等价于 `^#p` / `*#p` / `&#p`。

`$` 当前是字段 / 参数 / 绑定名侧的内在不可变标记；公开文档和测试集中没有 `^$x` 这类 handle-side freeze 形态，因此不应把它当作已冻结语法。若未来需要 handle-level freeze marker，应单独设计并补测试，而不是从 `$` 的字段语义自然外推。

`W0407` 是 hatted 参数义务的第一步工程化：它只提示“签名声明了 handle，但函数体没有使用 handle 视图”，不禁止 payload 读取，也不把该规则升级为错误。这样可以先暴露可疑签名，同时避免误伤 raw buffer、FFI adapter、测试和标准库中的历史写法。

### `@encap pub(path)` 当前边界

当前 `pub(path)` 的语义实现已经从任意子串匹配收紧为路径段匹配，可以正确区分 `std` 与 `stdx` 这类前缀相似路径。新增测试使用 `pub(tests/pass)`，同时锁定关键字路径段解析和跨目录授权/拒绝行为。

1.0 决策：Toka 不引入源码层 `mod` 声明。`pub(path)` 的 `path` 应与 `import` 左半部分的模块定位路径同义，不包含 `::{...}` 内部名字选择，也不是 Rust 式 module tree。当前路径段匹配是实现层基础；后续若继续收紧，应沿 import resolver、package root、library root 和跨包归一化方向细化。

### kebab-case 与减号边界

1.0 决策：连字符只属于 filesystem-oriented module-location path。目录名、`.tk` 文件名、`import` 左半部分、以及对齐 import path 的 `pub(path)` 可以使用 kebab-case；`.tk` 内部新产生并进入 Toka 语义名字空间的名字禁止 kebab-case，包括 import alias、字段名、声明名、绑定名和可选择 namespace。表达式层的二元 `-` 保持操作符身份，并要求左右空格。

这条规则把 URL / 包名 / 文件系统生态中的 kebab 命名兼容保留在路径层，同时避免 `max-size` 与 `max - size` 在语言层产生视觉歧义。IDE 双击选择等体验差异属于工具层瑕疵，不应扩大语言名字空间的复杂度。

## PAL 主体冻结审计

本轮冻结结论：PAL 的主体不是继续设计中的提案，而是 1.0 前可以依赖的主干安全模型。

### 已冻结的主体能力

- 基于路径的借用 ledger：同一路径、父路径、子路径的共享 / 可变借用冲突会被检查；可证明互不相交的路径可以共存。
- 调用点临时借用组：函数调用在 PAL 中一次性声明所有实参借用。裸 payload
  传参不是无借用；`x: T` 是共享 payload 借用，`x#: T` 是独占 payload
  借用，`cede x` 是失效性转移。若同一次调用中重叠实参含有独占访问或
  转移，则调用点必须拒绝。
- move / `cede` 安全：已借用路径不能被 move 或 `cede`；非 `cede` 参数不能在函数体中被 `cede`；`cede` 参数必须在函数体中完成转移义务。
- 局部控制流合并：`if`、`guard`、`match` 的 init mask、moved flag 与 PAL 状态会按可达分支合并。
- 循环回边合并：`loop` / `for` 的正常回边、`break` 出口、`continue` 回边都会保存并合并同一类 `AnalysisState`，避免 jump-only 路径丢失安全状态。
- 函数边界：逃逸 borrowed view 必须在签名中通过 inline dependency 或 `effects:` 明示，调用点不穿透函数体推断隐藏生命周期。
- unsafe 边界：raw pointer 与 `unsafe` 代码不属于 PAL 的 safe-borrow 保证范围；公共安全 API 若封装 raw 行为，必须通过签名暴露依赖关系。

### 测试锚点

- 主体压力：`tests/pass/g08_pal_stress_test.tk`、`tests/pass/g08_pal_stress_test_borrow.tk`
- 分支状态：`tests/pass/g08_pal_if_branch_restore.tk`、`tests/pass/g08_pal_guard_branch_state.tk`、`tests/pass/g08_pal_match_branch_state.tk`
- 循环状态：`tests/pass/g08_pal_loop_local_move.tk`、`tests/fail/move_in_loop.tk`、`tests/fail/move_direct_in_loop.tk`
- `break` / `continue` 状态：`tests/pass/g08_loop_break_state_merge.tk`、`tests/pass/g08_for_break_or_state_merge.tk`、`tests/pass/g08_pal_labeled_break_state_merge.tk`、`tests/pass/g08_pal_labeled_break_local_move.tk`、`tests/pass/g08_pal_labeled_continue_local_move.tk`、`tests/fail/loop_break_state_maybe_unset.tk`、`tests/fail/for_break_skips_or_unset.tk`、`tests/fail/loop_continue_move_state.tk`、`tests/fail/for_continue_move_state.tk`、`tests/fail/pal_labeled_break_move_state.tk`、`tests/fail/pal_labeled_continue_move_state.tk`
- 借用冲突：`tests/fail/fail_pal_move_locked.tk`、`tests/fail/fail_pal_path_prefix.tk`、`tests/fail/fail_pal_transient_locked.tk`、`tests/fail/borrow_field.tk`、`tests/fail/array_borrow_aliasing.tk`
- 借用逃逸：`tests/fail/ref_life_bound.tk`、`tests/fail/call_elision_escape.tk`、`tests/fail/pal_branch_borrowed_field_escape.tk`

### 仍属后续精度工作的边界

这些不是主体安全缺口，而是 1.0 后可以按价值继续提高精度的区域：

- 更高阶 lifetime polymorphism 与复杂 reborrow / freeze 模式。
- 深层 async / closure 捕获中的精细借用表达。
- dyn trait object 的对象生命周期、关联类型绑定与多 facet 组合。
- raw pointer 别名关系的自动证明。当前策略是留在 `unsafe` 或安全封装 API 内。
- 极端 disjointness 证明。若局部路径模型无法证明，当前策略是保守拒绝或要求更显式结构。

因此 PAL 当前冻结为“安全优先、局部可证明、函数边界显式契约”的模型。后续工作应优先改善误报和诊断，而不是改变主体规则。

## 文档表述风险

### 1. `@encap` wildcard 和 path visibility

文档已经写入 `pub *`、`pub * ! field`、`pub(path)`。标准库里有 `pub *`、`pub(std)` 的真实用法，新增测试也已经覆盖排除式 wildcard 和 path-targeted access 的最小正反行为。

建议：文档保持 import-path visibility 的口径。未来如果继续强化 `pub(path)`，应完善 resolver-normalized import identity，而不是引入新的源码级 `mod` 概念。

### 2. hatted 参数契约

文档表达“只有函数确实需要 handle 本体时才加帽”。这条规则已经按 `W0407` warning 级契约冻结：参数带帽但只读取 payload 时报警；显式使用 handle 视图、判空、或继续把 handle 视图传给另一个 hatted 参数时满足义务。它不是 hard error，避免误伤 raw buffer、FFI adapter、测试和标准库中的历史写法。

`cede` 与 hatted 参数同时出现时按独立义务叠加理解：`cede` 负责资源转移路径必须完成，帽子负责 handle 视图必须被实际使用。后续如果继续补，只需要补更细的组合测试，不需要重新讨论基础语法。

### 3. `dyn @Trait`

文档清楚限制为单 facet。`dyn @{A, B}` 拒绝、`dyn @Trait` 跨模块 pub/private 方法边界、source-less TKI replay 下的 dyn trait interface，以及关联类型 / 泛型方法 / 非 receiver `Self` 的对象安全拒绝，都已经有最小测试锁。后续如果继续补，重点应转向显式绑定关联类型后的 dyn object 设计、可见性路径授权的交叉场景，而不是基础语法形态。

本轮继续收紧了类型位置边界：alias、shape member、cast target 中出现的 `dyn @Trait` 也会走对象安全检查，避免通过间接类型位置绕过 `E0617`。

### 4. Common Mistakes

常见错误表已经很有价值，但若它公开存在，最好让每一行至少有一个 fail 测试或明确的 parser diagnostic。否则开发者会把它理解成硬承诺。

## 推荐下一阶段路线

### 阶段 A：诊断质量收口

主干语法锁已经补齐，下一步优先把仍然过于通用的 parser / sema 错误改成面向 Toka 规则的专用诊断。

目标：让公开语法文档中的常见错误不仅被拒绝，而且能以稳定、可理解的错误码解释为什么被拒绝。

### 阶段 B：跨模块 / TKI 组合测试

associated type projection、`pub import` re-export、dyn trait interface、generic impl `where:`、`@encap` visibility 的最小 source-less `.tki` 回放，以及 generic impl `where:` 正反用例、dyn trait 跨模块 pub/private 边界都已经锁定。

目标：确保单文件语法规则不会在增量构建和缓存接口中漂移。

### 阶段 C：文档回填

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
