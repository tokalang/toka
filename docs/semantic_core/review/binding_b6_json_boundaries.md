# JSON recovery: two bounded designs, no activation

Status: **proposed for scope decision**, not Accepted or implemented.
This is separate from managed-element morphology alignment. No raw_take,
Copy/Dup, thread, ABI or interface rule changes are authorized here.

## 下一实施裁定：先做完整值 factory 的具体类型验证

shared/iterator 已在 c81ecdd6 验收，本设计不再向该切片追加工作。
建议先验证工厂构造，再决定泛型迁移；递归容器证明仍独立待裁定。

首片建议范围（**尚未授权实施**）：

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

   以上是待验证的签名，不是声称已经可编译。Parsed 沿用下文的 value/rest
   设计；每个字段的实际来源须独立验证。
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
