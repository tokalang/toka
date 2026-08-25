# Tokalang Handle Grammar Phase 2 Specification: Admission Invariants, Diagnostic Enforcement & Zero-Violation Migration

**Status**: Historical implementation baseline under design review; not a
final Toka 1.0 syntax freeze
**Target Milestone**: Phase 2 Admission & Zero-Violation Migration
**Previous Baseline**: `c6b2e72f` (tag: `handle-grammar-phase1-baseline`)

> **Design review notice:** Parameter-root policy, morphic substitution, and
> substitution-time SFINAE in this document are being reconsidered. See the
> non-normative
> [Handle Morphology Candidate Model](semantic_core/handle_morphology_candidate_model.md)
> for the current design direction. This specification remains useful as an
> implementation and audit history, but must not be cited as the final Toka
> 1.0 surface contract. In particular, examples in this historical document
> that infer `&^T` from `&u` are under review: the candidate model distinguishes
> payload borrowing (`&u -> &T`) from explicit handle-identity borrowing
> (`&^u -> &^T`).

---

## 1. Grammatical Admission & Boundary Invariants

### 1.1 Admitted vs Prohibited Continuous Handle Chains

A **continuous handle chain** is a sequence of direct pointer/handle morphologies applied without an intervening structural or nominal boundary node.

- **Admitted Level 1 (Pure Handles)**:
  - `*T` (raw pointer)
  - `nul *T` (nullable raw pointer)
  - `^T` (unique owning heap pointer)
  - `~T` (shared ARC pointer)
  - `&T` (borrow view reference)
  - `&T#` (mutable borrow view reference)
- **Admitted Level 2 (Borrow Extensions)**:
  - `&^T` (borrow view of unique heap pointer)
  - `&~T` (borrow view of shared ARC pointer)
  - `&&T` (double borrow / reference to reference)
- **Admitted Raw Depth $N$**:
  - `**T`, `***T`, `****T`... (arbitrary depth pure raw pointers).
- **Prohibited Forms (Strict Compiler Rejection)**:
  - **`InvalidManagedLayerOrder`**: `^&T`, `~&T` (managed pointer whose inner layer is a borrow reference; at managed depth 2, the outer layer must be `&`).
  - **`ExceededManagedDepth`**: `^^T`, `~~T`, `^~T`, `~^T`, `&^^T`, `&&^T`, `&&~T`... (nested managed owning layers).
  - **`ExceededBorrowDepth`**: `&&&T`, `&&&&T`... (continuous borrow depth $> 2$).
  - **`MixedManagedRaw`**: `*^T`, `^*T`, `*~T`, `~*T`, `*&T`, `&*T`, `**^T`, `*&^T`, `&*~T`, `***^T`... (mixing raw pointer and managed handle layers within one continuous handle chain).

### 1.2 Boundary Semantics

A continuous handle chain terminates at **any non-Pointer Type node** in the type AST/graph:
- Nominal shapes (`ShapeType`), generic applications (`ShapeType::GenericArgs`)
- Arrays (`ArrayType`), slices (`SliceType`)
- Tuples (`TupleType`), functions (`FunctionType`, `DynFnType`)
- Uninitialized slots (`UninitType`)

**Boundary Examples**:
- `*Box<&T>`: **Legal** (`Box` is a nominal shape boundary separating outer `*` and inner `&`).
- `Option<*&T>`: **Illegal** (inner generic argument `*&T` is classified independently and rejected).
- `[*^T; 4]`: **Illegal** (array element `*^T` is classified independently and rejected).
- `*[&T; 4]`: **Legal** (array `[&T; 4]` is a structural boundary separating outer `*` and inner `&`).

### 1.3 Raw Nullability Invariants

- Nullability (`nul`) is exclusively valid on raw pointer layers.
- Multi-layer raw nullability binds per layer:
  - `nul **T`: Outer raw pointer is nullable; inner raw pointer is non-null.
  - `*nul *T`: Outer raw pointer is non-null; inner raw pointer is nullable.
  - `nul *nul *T`: Both outer and inner raw pointers are nullable.
- Managed handles (`^`, `~`, `&`) are strictly non-nullable by language construction.

Canonicalization, semantic reification, and mangling separation for the three
two-layer nullability arrangements are covered by
`tests/TypeSyntaxCanonicalization.cpp`. Source parsing/lowering and source-hidden
TKI replay are covered by `g08_raw_layered_nullability_types.tk` and
`handle_003_raw_nullability`.

### 1.5 Parameter Root Single-Depth Boundary & Binding-Side Morphology

In all formal parameter declarations, the root binding chain is limited to one
handle layer. Non-`cede` parameters already capture/alias their argument, so a
second borrow layer on the formal root is redundant.

- Admitted roots include `x: T`, `^x: T`, `~x: T`, `&x: T`, `*x: T`,
  `nul *x: T`, and `cede ^x: T`.
- Globally illegal chains still report `E0490`–`E0492`.
- Globally legal multi-layer types such as `&^T`, `&~T`, `&&T`, and `**T`
  remain valid outside formal roots, but their use as a formal root reports
  `E0495`.
- Parameter type annotations beginning with root morphology remain rejected by
  `E0494`; hats belong on the binding name.

Level-2 borrows remain valid as first-class local, return, and structural child
types. Borrow target selection is explicit: `&u` borrows the payload as `&T`,
while `&^u` borrows the unique handle identity as `&^T`.

The admitted return surface is qualified by
`tests/pass/g08_level2_return_views.tk` and the source-hidden
`tests/semantics/tki_replay/cases/handle_002_level2_returns` case. They cover
`&^T`, `&~T`, and constrained morphic `&&T`, plus structural aliases containing
each form.

### 1.6 Type Alias Boundary & Root Morphology Invariants

- Type aliases (`alias Name = Target` and `type Strong = Target`) define aliases for **soul and nominal types**.
- An `alias` whose root AST node begins with handle morphology (`^`, `~`, `&`, `*`, `nul *`, or parenthesized equivalents `(^T)`) is strictly rejected with diagnostic `E0493`. Root hats must remain visible at variable binding use sites.
- Subtypes located behind structural/nominal boundaries within an alias (e.g. `alias OptUnique = Option<^i32>`, `alias VecRef = Vec<&i32>`) are fully legal because the container boundary isolates the inner capability.
- Nested type parameters within aliases remain subject to recursive Handle Grammar validation (`Option<^^i32>` is rejected under `E0491`).

---

## 2. Diagnostic Allocations & Error Codes

The following diagnostic identifiers are reserved in `include/toka/DiagnosticDefs.def`:

```text
E0490 DIAG(ERR_ILLEGAL_HANDLE_ORDER,             Error, "E0490", "Illegal handle grammar ordering in type '{}'")
E0491 DIAG(ERR_EXCEEDED_HANDLE_DEPTH,            Error, "E0491", "Exceeded maximum handle depth in type '{}'")
E0492 DIAG(ERR_MIXED_HANDLE_RAW,                 Error, "E0492", "Illegal mixing of managed handles and raw pointers in type '{}'")
E0493 DIAG(ERR_ALIAS_ROOT_HANDLE_MORPHOLOGY,      Error, "E0493", "Alias target '{}' cannot begin with root handle morphology '{}'; root hats must remain visible on the binding at each use site")
E0494 DIAG(ERR_PARSER_TYPE_SIDE_PARAM_MORPHOLOGY,Error, "E0494", "Parameter type '{}' cannot begin with root handle morphology '{}'; place the root morphology on the parameter binding instead (did you mean '{}'?)")
E0495 DIAG(ERR_PARAM_HANDLE_DEPTH_FORBIDDEN,     Error, "E0495", "Parameter '{}' cannot use root handle chain '{}'; formal parameter roots are limited to one handle layer (did you mean '{}'?)")
E0760 DIAG(ERR_CODEGEN_ILLEGAL_HANDLE_GRAMMAR,   Error, "E0760", "Internal CodeGen invariant violation: illegal Handle Grammar type '{}' reached LLVM lowering")
```

---

## 3. Type-Layer Recursive Validator & Diagnostic Issue Model

### 3.1 Shared Issue Data Structure

Defined in `include/toka/Type.h`:

```cpp
struct HandleGrammarIssue {
  HandleGrammarViolation Violation = HandleGrammarViolation::None;
  std::vector<unsigned> TypePath; // Structural navigation path in composite type graph
  unsigned OuterLayer = 0;
  unsigned InnerLayer = 0;
  std::shared_ptr<Type> OffendingType;
  TypeSyntaxPtr OffendingSyntax;
  std::string CustomMessage;
};
```

### 3.2 Recursive Traversal Rules

`Type::findHandleGrammarIssueRecursive()` recursively traverses:
1. `PointerType::PointeeType`
2. `ArrayType::ElementType` & `SliceType::ElementType`
3. `ShapeType::GenericArgs` & concrete member types
4. `FunctionType::ParamTypes` & `FunctionType::ReturnType`
5. `DynFnType::ParamTypes` & `DynFnType::ReturnType`
6. `MissOutcomeType::PayloadType`
7. `UninitType::InnerType`
8. Anonymous record fields & associated type projections
9. Cycle-visited guard for recursive shape graphs.

### 3.3 Consumer Unification

- **Sema Admission**: Emits `E0490`–`E0492` with exact source spans and generic instantiation notes.
- **CodeGen Invariant**: Asserts `!findHandleGrammarIssueRecursive(type).has_value()` in `src/CodeGen/CodeGen_Decl.cpp:3454` (`getLLVMType()`), reporting `E0760` on violation.
- **Classifier Unit Tests**: Directly asserts on composite and recursive type fixtures.

### 3.4 Diagnostic Evolution: Indexed Handle Rebind (`E0408` → `E04572`)

In Toka 1.0, indexed elements (such as `~values[0] = cede ~replacement`) do not have an independent handle-rebind surface. Historically, assignment into indexed handle expressions produced type conversion error `E0408` due to AST morphology unwrapping. Under Phase 2:
- Handle rebind validity is checked through capability and immutability rules, classifying indexed handle reassignments under `E04572` (immutable/unrebindable target surface).
- The conformance suite test `diag_index_handle_rebind_not_surface_01` verifies that attempting to rebind an indexed handle continues to be rejected at compile-time with `E04572`.

---

## 4. SFINAE Tri-Classification Policy

When evaluating generic templates and candidate methods:

1. **Template Definition Contains Illegal Syntax**:
   - Immediate compilation error at template declaration (e.g. `fn f<'T>(x: *^'T)`).
2. **Generic Parameter Substitution Produces Illegal Signature**:
   - SFINAE filtering: candidate method is cleanly dropped from the overload/method candidate set.
   - Audited with `Decision=RejectedSFINAE`, `is_instantiated=false`, `is_llvm_lowered=false`.
3. **Template Signature Legal, Body Instantiation Produces Illegal Local Type**:
   - Hard compilation error during instantiation.
   - Diagnostic emitted at the offending statement with an attached `Note` pointing to the template instantiation call site.

---

## 5. Audit Event Taxonomy & Lifecycle Flags

To prevent conflating orthogonal lifecycle stages with filtering decisions:

- **`Decision`**:
  - `Observed`: Standard compiler type observation.
  - `RejectedSFINAE`: Candidate filtered out during template candidate evaluation.
- **`Lifecycle Flags`**:
  - `is_transient`: Internal compiler intermediate lowering AST type.
  - `is_admitted`: Surface/TKI type accepted by compiler frontend.
  - `is_instantiated`: Template monomorphized instance created.
  - `is_llvm_lowered`: Type lowered to LLVM IR via `getLLVMType()`.

---

## 6. Migration Inventory: 34 JSON Transients + 17 Lowering Transients

The Phase 1 baseline identified 51 `IntermediateLowering` transient violations. They are strictly partitioned and resolved across M2 and M4:

### 6.1 M2 Scope: 34 JSON Storage Transients ($N=34$)
- `34 *^[Uninit<T>]`: Uninitialized buffer allocations in `lib/stdx/serde/json.tk`.
- **M2 Resolution Goal**: Redesign generic JSON uninitialized storage, clearing all 12 LLVM lowering violations and 34 JSON storage transients.
- **M2 Acceptance Metric**: `JSON IntermediateLowering violations = 0`.

### 6.2 M4 Scope: 17 Lowering Transients ($N=17$) Across 6 Discrete Clusters
The remaining 17 transients are resolved across 6 independent commits:

| Cluster | Count | Pattern | Subsystem / Location | Target Equivalent |
|---|---|---|---|---|
| 1. ARC Identity | 7 | `*~Data`, `*~Resource`, `&*~Resource` | Match guard runtime identity (`g08_match_guard_leak.tk`) | ARC identity token / intrinsic |
| 2. Shared Cell Re-wrapping | 5 | `~~Cell`, `~#~#Cell` | Permission transfer AST lowering | Deduplicate shared wrapper synthesis |
| 3. Existing Unique Handle | 2 | `^^Point` | Re-wrapping existing unique handles (`g08_test.tk`, `g08_noshared.tk`) | Preserve unique handle identity without re-wrapping |
| 4. FFI Out-Parameter | 1 | `&nul *char#` | `LLVMModule_verify` | Pure raw `**char` ABI pointer |
| 5. Raw-on-Borrow Address | 1 | `*&i32` | Address-of reference in `main` | Address-of referent |
| 6. Raw-on-Unique Address | 1 | `*^SummaryData` | Memory summary pointer in `source_summary.tk` | Address of pointee payload storage |
| **Total** | **17** | | | **Strict Sum = 17** |

---

## 7. TKI Interface Format Version 3 & Fail-Closed Strategy

- In `include/toka/InterfaceVersion.h`:
  - `TOKA_INTERFACE_FORMAT_VERSION`: Bump `2 -> 3`.
  - `TOKA_COMPILER_INTERFACE_VERSION`: Bump `0.9.9-12 -> 0.9.9-13`.
- **Fail-Closed Rule**: All format v2 TKI modules are rejected fail-closed upon import.
- **Cache Invalidation**: Cache salt updated to invalidate all existing precompiled v2 interface files.

---

## 8. Final Acceptance Criteria

Phase 2 completion requires the clean audit scan to verify:

```text
SourceSurface admitted violation : 0
TKIImport admitted violation     : 0
Instantiated violation           : 0
LLVMTypeLowered violation        : 0
Generated non-SFINAE transient   : 0
RejectedSFINAE candidates        : Permitted non-zero (strictly is_instantiated=false, is_llvm_lowered=false)
All 9 Verification Suites        : 100% Passed (rc=0)
CTest Matrix (16+)               : 100% Passed
```
