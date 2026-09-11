# JSON recovery: accepted concrete leaf slice and deferred designs

Status: concrete leaf slice Accepted at `94eaaa46c410054c46ae50c54f51d1075535540d`.
Local freeze: `freeze/json-leaf-94eaaa46`. This first slice is closed.
The subsequent combined JSON-recovery implementation is in progress on
`impl/json-recovery`; neither that work nor its container evidence is Accepted.
The original eight nonempty JSON targets remain required positives. The earlier
scope decisions below are historical; they are not additional helper approval gates.
This is separate from managed-element morphology alignment. No raw_take,
Copy/Dup, thread, ABI or interface rule changes are authorized here.

## Combined recovery implementation checkpoint (2026-09-11)

- Separate `@JsonFactory` constructs complete `Parsed<T>` values; generic
  Option/Result/Vec/HashMap and JsonNode code no longer constructs arbitrary T
  through byte-zero initialization. Library migration is WIP, not qualified
  container support.
- Generic factory bodies are prepared on demand in their captured declaration
  scope, after shape analysis, rather than eagerly during global registration.
  Invalid/unprepared bodies cannot obtain code generation through this path.
- The new factory-values regression checks scalar/Option/Result values,
  resource cleanup including a missing outer delimiter, both producer orders,
  normal/shadow parity, invalid-body cache rejection, and local borrowed-result
  escape rejection. Incremental tool build passed; the eight directly related
  CTest gates passed 8/8 in 165.44 seconds. No full PASS/FAIL run was performed.
- Original-target check: 1/9, but that one is an existing empty main. Actual
  JSON recovery is **0/8**. First errors remain `ElementDependenciesUnproven`;
  `Vec<i32>` factory integration separately reports `IncompleteFacts` for
  `Parsed<Vec<i32>>`. Neither is converted to an expected negative.
- Still required: verified recursive container descriptor/instance evidence,
  mutation preservation/invalidation and cleanup, followed by the original
  JSON runtime matrix. No blanket raw_take or recursive-type exemption exists.

Reproduce the distinct gates with `tools/scripts/test_json_factory_values.py`
and `tools/scripts/test_json_recovery.py`, each with `--build-dir BUILD`.
The latter defaults to compile **and run**; `--check-only` is diagnostic only.

### Subsequent source-chain WIP (not an acceptance)

- Checked null raw fields now survive local value transfer; unknown operations
  revoke the fact, and rejected calls restore it. No old initializer is
  reinterpreted by its current spelling. Live enum variants are checked without
  treating inactive raw payloads as initialized storage.
- Allocation ancestry, actual raw writes and declared releases are distinct
  observations. None authorizes a load/take, proves a live set, or supplies an
  ownership/cleanup plan. The real-source C++ test checks their identities.
- The concrete recursive parser now retains numeric cursors across iterations
  and derives views from its original input. The inspected JSON failure no
  longer reports the prior local `rem`/`key.rest` lifetime errors; it still
  fails on unqualified recursive storage. Its runtime is NOT qualified.
- Tool build and nine directly related CTest gates passed (9/9, 178.81 s).
  No full PASS/FAIL run; original JSON recovery is not claimed.
- Execution review rejected proposed allocation-to-storage admission and an
  enum fallback. Those changes are not applied. The later empty-owner pattern
  verifier was also rejected and is not applied. Full B6 production admission
  was awaiting scope confirmation at that checkpoint. The user has now
  explicitly authorized complete B6 production implementation, including
  descriptors, instance evidence, Sema admission and CodeGen consumption.
  That authorization does not accept the rejected implementations or this WIP.

### Recorded-slot production work (in progress)

The first additional dependency provider recognizes a current, checked write
to an exact constant slot of a local raw allocation. It requires the written
value's existing null-opaque-storage certificate and independently excludes
borrowed value fields. This is a dependency proof only: it does not discharge
raw_take's unsafe initialization, addressing, retirement or remainder duties.

The provider records slot/element/write/allocation identities, invalidates on
unknown effects and release, retires on take, restores rejected-expression
state, and joins only slots proved on every reaching branch. Joined receipts
retain all checked write alternatives; CodeGen validates each leaf. A missing
branch write cannot be filled from another branch. Unqualified loop backedges and match
continuations discard the new facts. CodeGen validates the attached receipt;
missing/mismatched receipts fail without artifacts. Existing structural-type
raw_take proofs are unchanged.

This is not a Vec/HashMap recursive descriptor or a JSON recovery claim. The
dynamic-index/private-helper and recursive-instance contract work remains;
the original eight nonempty targets and cleanup/lifetime matrix still define
the acceptance boundary. No additional implementation authorization is needed.

Current verification: the recorded-slot runner passes five runtime positives
(including both branch choices and a stored Vec value), twelve rejection/parity
cases, and ten object/IR fault checks. The tool build passes. Re-running the
original JSON targets in check-only mode still gives **0/8 nonempty positives**;
all eight retain `ElementDependenciesUnproven`. This checkpoint does not claim
completion of the recursive container contract or request another acceptance.
The six related CTest gates passed together (6/6, 124.32 s), including the
recorded-slot matrix, existing raw_take/Vec gates, factory values, binding
dependencies and allocation ancestry. No full PASS/FAIL run was performed.

### Fixed index binding follow-up

An immutable local integer binding initialized directly from an in-range
literal may identify the slot. The allocation extent must still be a literal;
runtime indices and unknown extents remain rejected. Binding identity is
preserved in the receipt and checked against both the actual write and take
by CodeGen. No old initializer is followed through a name lookup or alias.

The preceding proposal to support runtime indices and nonliteral extents was
rejected by execution review and was not applied. The accepted execution
alternative retains both literal-index-value and literal-extent checks; it
does not bypass that rejection. The broader dynamic/private-helper work is
still unfinished and is not covered by the fixed-index tests.
Incremental compiler build and the recorded-slot/Vec CTest pair passed (2/2,
62.48 s). The expanded slot runner includes six runtime positives, seventeen
rejection/parity cases and ten fault checks. No original JSON recovery is
claimed from this follow-up; no full suite or release qualification was run.

### Dynamic-index implementation (2026-09-12, not Accepted)

Following the explicit review of the rejected proposal, the production
implementation now admits an immutable local integer index whose value is
determined at runtime, and nonliteral allocation extents. A constant out-of-range
index/extent pair still rejects. No bounds or initialization proof is claimed:
the receipt proves the checked stored value's dependencies, and the existing
unsafe addressing, initialized-element and remainder obligations remain intact.

The current supported index form is a bare binding or a value-preserving unsafe
wrapper (parentheses do not introduce a value conversion). Cast/ascription index
expressions are conservatively ineligible, rather than identifying a converted
value with the original binding. Writable indices, aliases, unresolved sources
and different binding identities remain ineligible. No old initializer is replayed.

Every possibly mutating operation and every successful take discards other slot
receipts conservatively; different lexical roots are not assumed disjoint.
Rejected writes restore the pre-expression snapshot. Joined receipts require
the same index identity and all reaching write leaves. CodeGen checks actual
write/take bindings, index types, element types, allocation ancestry and every
leaf, with missing/index/allocation/leaf fault rejection and no artifacts.

The old temporary worktree was missing its git pointer and 2,872 tracked files.
Its remaining source files were left intact. This candidate is on
`impl/json-dynamic-index` in `/private/tmp/toka-b6-dynamic-20260912`, based on
`3c665233401ba8d20c00f48966084dfb278d0818`; frozen refs remain unchanged.
Raw storage formals/cross-function helper receipts and recursive container
descriptors are still not implemented by this change. This is not B6 completion.

Validation: fresh Release compiler/SDK build passed; recorded-slot, raw_take,
Vec, factory-values and binding-dependency CTest gates passed together (5/5,
77.09 s). The slot runner includes runtime indices, unsafe wrappers, nonliteral
extents, both dynamic branch outcomes, conversion/shadowing/missing-write
rejections, rejected-write rollback, and 22 object/IR fault checks. The original
nonempty JSON targets were rechecked and remain 0/8, each still reporting
`ElementDependenciesUnproven`. No full PASS/FAIL run or acceptance is claimed.

## 下一实施裁定：先做完整值 factory 的具体类型验证

shared/iterator 已在 c81ecdd6 验收，本设计不再向该切片追加工作。
建议先验证工厂构造，再决定泛型迁移；递归容器证明仍独立待裁定。

首片范围（**已验收并收口，不追加任务**）：

1. 将三个具体解析器需要的扫描/解码逻辑提取到不依赖 JsonNode 的小型
   内部模块；保持算法、错误文本和现有入口行为，不复制第二套解析算法。
   这是为了让叶子验证不被旧 json 模块中主动检查的递归容器路径阻断，
   不是从最终 JSON 门禁中排除 JsonNode。
2. 先提供内部具体函数，暂不引入公共 @JsonFactory 或迁移泛型 bounds：

   ```toka
   parse_i32_value(json: str) -> Result<Parsed<i32>, str> <- json
   parse_string_value(json: str) -> Result<Parsed<string>, str> <- json
   parse_str_view(json: str) -> Result<Parsed<str>, str> <- json
   ```

   以上三个签名已在具体 factory 候选中编译、运行验证。Parsed 沿用下文的
   value/rest 设计；验证及范围见 `json_leaf_closeout_2026_09_11.md`。
3. i32/string 从其合法初值构造；str view 直接来自输入的已验证切片。
   不使用 memset(T,0)、假默认值或“解析成功即证明任意 T 已构造”的推断。
4. 成功交出完整结果；失败清理已拥有的字符串/中间结果。借用 str 与 rest
   保留输入依赖，静态错误文本使用独立静态 witness。不得把整个结果标成
   dependency-free，也不得回填声明依赖来伪造实际 referent。
5. 先覆盖正常/空/畸形输入、转义及非 ASCII、失败清理、输入存活/局部逃逸、
   owning string 脱离输入后存活、normal/shadow 一致和拒绝不产物。
   所有公开 JSON/return-matrix 正例继续保留，不能将局部门禁算作恢复。

完成线：三个具体 factory 的构造和逐字段依赖均有真实运行/拒绝对照，
现有解析算法未分叉。到此提交完整候选，再决定 @JsonFactory、组合解析器
和 mutating adapter 的迁移；不按 helper 追加接受点。

本次进一步核对的依据：

- `Sema_Expr.cpp:5860` 的方法 MemberDependencies 应用先检查
  `returnTypeHasMember`，随后写入 `m_LastFieldDependencies`。
- `Sema_Expr_Call.cpp:10568` 的普通调用同样映射返回字段依赖。
- `syntax.md:323-374` 描述的是返回值/返回成员的依赖契约。

这些已检查路径不能用作通用 receiver-write 依赖证明。因此首片不新增
借用 payload 的通用 mutating adapter；不将“暂未验证”描述成已经支持。
下文两份较完整设计仍是候选，尤其 container witness 不在该首片内。

## 1. Recursive owning-container evidence

The concrete JsonNode parser already constructs valid values: HashMap::new,
Vec::new, string::from and JsonNode::NullNode (json.tk:818/833/841/849/864).
Its problem is not zero initialization. The missing closure is
JsonNode -> Vec/HashMap storage -> JsonNode. Neither a nominal type name nor
the absence of an explicit lifetime argument proves that closure.

### Evidence entry and minimum carrier

Propose an internal, source-visible contract descriptor tied to the **resolved
declaration and complete generic substitution**, plus an instance witness.
The descriptor is not a name allowlist: acceptance requires verification of
the actual factory, element-transfer, replacement and cleanup implementations.
Missing source/provider contract means unknown; no TKI extension in this first
slice. A descriptor/recipe alone cannot publish an instance witness.

Minimum witness: exact container owner and backing-storage identities, complete
element types, actual transitive dependency/static-storage summaries, ownership
of cleanup, mutation revision, and Known/Unknown status. It is **not a compiler
proof of dynamic initialized extent**. Unsafe containers still maintain the
Vec live prefix or HashMap occupied metadata under their existing contract.

| Existing operation / evidence entry | Required preservation / publication |
| --- | --- |
| `Vec<T>::new/with_capacity`, `HashMap<K,V>::new` | Verify allocation ownership and empty initialized set; publish only on successful construction |
| `push(cede value)` / `insert(cede key, cede value)` | Consume validated incoming element evidence; publish new dependency summary only after a complete slot is live |
| reserve/grow/resize | Transfer storage and every live element responsibility; retire old storage before it can be cleaned again |
| pop/remove | Returned value inherits its actual dependencies; remainder keeps conservative dependencies (no unsound subtraction) |
| replacement / clear / drop | Use the existing validated cleanup/retirement plan; replacement must not retain a stale summary |
| explicit move / permitted shared ownership | Keep the same underlying witness identity, not a newly invented empty summary |
| `from_raw`, public raw-field construction, unknown raw writes | No automatic witness; invalidate existing proof unless a separately verified import/operation contract supplies it |

### Closing recursion, without treating a cycle as success

Use induction over verified value construction, not `visited => true`.
Null/scalar/string cases are bases; empty containers are bases. Adding a child
requires that child's valid evidence first. Recursive parser summaries may
state the resulting invariant only after all constructor/return paths and all
recursive transitions have been checked. A pending recursive summary cannot
grant authority by itself. Opaque/raw edges, unmatched mutation or unresolved
summary propagation keep the result Unknown. No special exemption for JsonNode.

Before implementation, the descriptor storage/publication point and the
existing AST mutation hooks must be pinned down. If those hooks cannot observe
an operation, the first implementation rejects that path rather than silently
treating its storage as closed. Do not introduce a general dynamic-extent
analysis as a side effect.

### Success/failure responsibility and tests

Factory failure publishes no witness; existing initialized locals retain their
cleanup. Rejected transfer restores source/destination state. A partial parser
owns only its completed child values/containers, which existing cleanup must
release. No new unwinding guarantee is made for fatal termination.

Minimum controls: empty and nested JsonNode arrays/objects; nested parse error
after several owned children; reallocation and removal exact-once; owning
string versus borrowed str; same-short-name user container; raw-imported Vec;
post-witness raw overwrite; borrowed child; invalid recursive callee; missing
or stale witness. All negatives must remain rejected without artifact.

## 2. Generic FromJson: factory-first construction

This is a different problem. The existing generic Option/Result/Vec/HashMap
paths allocate one T, memset it to zero, then call `parse_json(self#)`.
The current trait only describes mutation of a receiver; it cannot certify
that zero bytes formed a valid arbitrary T. `free[0]` on an error also does not
account for resources constructed by a partially successful parser.

### Proposed library signatures (design notation, not new accepted syntax)

```toka
shape Parsed<'T>('value: T, rest: str)
trait @JsonFactory {
    fn parse_value(json: str) -> Result<Parsed<Self>, str> <- json
}
// Existing compatibility API for an already-initialized receiver:
fn parse_json(self#, json: str) -> JsonRes <- json
```

`@JsonFactory` is a proposed separate construction contract; it is not inferred
from an existing @FromJson implementation. Each primitive/record/container
factory must build a complete value using its legitimate constructor. No
generic default value, byte-zero validity, or new implicit lifetime extension.
Generic parser bounds migrate to this capability only after its scope is
approved; this is a public library contract change, not facts-only plumbing.

`Parsed<T>.rest` always has a real input-derived view. If T=str, its value also
keeps the actual input slice/referent; if T owns string storage, that value's
ownership is independent but the **whole Parsed result still borrows via rest**.
The declared `return <- json` is an allowed upper bound, not the actual
per-field proof. Static error literals carry a separate static witness; errors
containing input views retain their actual dependency. Never classify all
factory results as independent owned temporaries.

### Construction and cleanup transitions

| Path | Responsibility |
| --- | --- |
| Before factory call | Existing receiver stays live; factory has no uninitialized T receiver |
| Child parse succeeds | Factory local owns a complete child; after insertion the completed container owns it |
| Later child/delimiter fails | All completed children/containers are cleaned once; return Err without a fabricated partial T |
| Complete success | Caller receives the whole validated result and its field dependencies |
| Mutating adapter succeeds | Destructure the complete result through admitted routes, then replace self; old self cleaned once |
| Mutating adapter fails | Old self unchanged; no new initialized receiver or stale dependencies |

The adapter for borrowed payloads additionally needs a valid **receiver-write
dependency contract** tying the stored view to json. `return <- json` alone
does not express that. Confirm the existing effect/dependency representation
can carry it before claiming a generic adapter works for str; otherwise keep
the borrowed factory path usable and defer that adapter, rather than erasing
its dependency. Do not activate arbitrary partial move just to unpack Parsed.

Minimum tests: nonzero-invariant/no-default resource; malformed input before
and after child construction; incomplete object separator; Some/None and both
Result variants; receiver unchanged on Err; old receiver exact-once on success;
borrowed str input survives, local input escape rejects; static error text;
owned string independence; resource-valued map/array; unknown custom factory.

## Decisions required before implementation of these two designs

1. Approve the exact source-visible container descriptor and instance-witness
   boundary, including raw-import/mutation invalidation. No canonical proof
   is claimed to exist today.
2. Approve the separate factory capability and generic-bound migration; pin
   down the borrowed receiver adapter dependency representation.

Managed-element fixes can proceed independently. Neither design reopens
thread, Copy, clone, or blanket raw_take admission.
