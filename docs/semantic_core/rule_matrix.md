# Toka 1.0 Semantic Rule Matrix

This matrix records the first phase of the semantic-core freeze audit. It maps
the current language contract to implementation areas and existing tests. It
does not move tests or change compiler behavior.

Primary references:

- `docs/syntax.md`
- `docs/1_0_freeze_decision_list.md`
- `include/toka/PAL_Checker.h`
- `src/Sema/PAL_Checker.cpp`
- `src/AST/TKIExporter.cpp`
- `include/toka/DiagnosticDefs.def`

## PAL Rules

### PAL-CALL-001: Call arguments form a simultaneous borrow group

- Status: Core guarantee
- Source form: `f(x, x)`, `f(x#, x)`, `f(cede x, x)`
- Operation class: `SharedPayloadBorrow`, `ExclusivePayloadBorrow`,
  `Invalidation`
- Decision: all arguments in one call are checked together; overlapping paths
  conflict if any argument requires exclusive access or transfer.
- Rationale: payload parameters are not invisible value copies for PAL.
- Primary diagnostics: `E0475`
- Implementation areas: `src/Sema/Sema_Expr_Call.cpp`,
  `include/toka/PAL_Checker.h`, `src/Sema/PAL_Checker.cpp`
- Positive tests: `tests/pass/g08_call_payload_alias_readonly.tk`,
  `tests/pass/g08_call_payload_alias_disjoint.tk`,
  `tests/pass/g08_pal_call_borrow_statement_boundary.tk`
- Negative tests: `tests/fail/call_payload_alias_mut_read.tk`,
  `tests/fail/call_payload_alias_mut_mut.tk`,
  `tests/fail/call_payload_alias_cede_read.tk`,
  `tests/fail/call_readonly_ref_alias_mut_params.tk`,
  `tests/fail/call_readonly_member_ref_alias_mut_params.tk`
- Interface replay requirements: callee parameter access class must be visible
  from the signature and `.tki`; call sites must not inspect callee bodies.
- Replay tests: `tests/semantics/tki_replay/cases/pal_call_001_alias`.
- Missing coverage: broader alias-conflict replay for member paths and cede
  arguments.

### PAL-BORROW-001: Active exclusive borrows block overlapping reads and writes

- Status: Core guarantee
- Source form: `auto &r# = &x#` followed by overlapping access to `x`
- Operation class: `ExclusivePayloadBorrow`, `PayloadWrite`,
  `SharedPayloadBorrow`
- Decision: an active exclusive borrow conflicts with overlapping access.
- Rationale: exclusive mutation requires exclusive permission over the path.
- Primary diagnostics: `E0441`, `E0442`, `E0443`
- Implementation areas: `src/Sema/PAL_Checker.cpp`,
  `src/Sema/Sema_Expr.cpp`, `src/Sema/Sema_Expr_Member.cpp`
- Positive tests: `tests/pass/g03_borrow_ok.tk`,
  `tests/pass/g08_pal_stress_test_borrow.tk`
- Negative tests: `tests/fail/borrow.tk`, `tests/fail/borrow_field.tk`,
  `tests/fail/safety_double_mut.tk`,
  `tests/fail/pal_member_mut_borrow_duplicate.tk`,
  `tests/fail/pal_member_mut_borrow_payload_read.tk`,
  `tests/fail/pal_member_mut_borrow_payload_write.tk`
- Interface replay requirements: no callee-body dependence for local borrow
  facts; escaping borrowed views use `EFF-*` rules.
- Missing coverage: broader member-chain examples with mixed handle and payload
  mutability.

### PAL-BORROW-002: Active shared borrows protect validity but do not globally freeze every payload write

- Status: Core guarantee
- Source form: shared borrow of a path followed by non-invalidating payload
  writes allowed by the path model
- Operation class: `SharedPayloadBorrow`, `PayloadWrite`, `Invalidation`
- Decision: shared borrows protect the borrowed path from invalidation and
  conflicting exclusive operations; ordinary payload writes are permitted when
  the rule can prove they do not invalidate the active borrowed view.
- Rationale: Toka 1.0 separates ordinary payload writes from invalidating
  replacement.
- Primary diagnostics: `E0440`, `E0441`, `E0443`
- Implementation areas: `src/Sema/PAL_Checker.cpp`,
  `src/Sema/Sema_Expr_Binary.cpp`, `src/Sema/Sema_Expr_Member.cpp`
- Positive tests: `tests/pass/g08_pal_shared_borrow_payload_write.tk`,
  `tests/pass/g08_pal_member_shared_borrow_payload_write.tk`,
  `tests/pass/g08_pal_shared_borrow_array_payload_write.tk`
- Negative tests: `tests/fail/borrow_move.tk`,
  `tests/fail/cede_borrowed.tk`, `tests/fail/fail_pal_move_locked.tk`
- Interface replay requirements: escaping shared borrow dependencies must be
  declared in signatures or `effects:`.
- Missing coverage: explicit source-less replay for shared-borrow-returned views.

### PAL-PATH-001: Overlap is path-prefix based, with disjoint fields allowed

- Status: Core guarantee
- Source form: borrow `obj.left`, mutate/read `obj.right`
- Operation class: path overlap for all PAL operation classes
- Decision: overlapping path prefixes conflict; disjoint fields may be borrowed
  and mutated independently when represented as distinct source paths.
- Rationale: PAL is a local Path-Anchored Ledger.
- Primary diagnostics: `E0441`, `E0442`, `E0443`
- Implementation areas: `PALChecker::pathsOverlap`,
  `PALChecker::verifyOperation`
- Positive tests: `tests/pass/g08_pal_stress_test_borrow.tk`,
  `tests/pass/g08_pal_stress_test.tk`
- Negative tests: `tests/fail/fail_pal_path_prefix.tk`,
  `tests/fail/borrow_field.tk`
- Interface replay requirements: member morphology and field structure needed
  for semantic checking must survive `.tki` export even for private fields.
- Missing coverage: generated `.tki` replay for private-field disjointness.

### PAL-CFG-001: Local control-flow states are merged conservatively

- Status: Core guarantee
- Source form: `if`, `guard`, `match`, `loop`, `for`, `break`, `continue`
  with move, unset, or borrow state changes
- Operation class: `Invalidation`, move state, init state, borrow state
- Decision: branch and loop state must merge without allowing use-after-move,
  use-before-init, or invalid borrowed state.
- Rationale: local control-flow analysis may be used inside a function, but
  calls consume only signatures.
- Primary diagnostics: `E0410`, `E0440`, `E04501`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/Sema/PAL_Checker.cpp`
- Positive tests: `tests/pass/g08_pal_if_branch_restore.tk`,
  `tests/pass/g08_pal_guard_branch_state.tk`,
  `tests/pass/g08_pal_match_branch_state.tk`,
  `tests/pass/g08_pal_labeled_break_state_merge.tk`,
  `tests/pass/g08_pal_labeled_continue_local_move.tk`,
  `tests/pass/g08_for_break_or_state_merge.tk`,
  `tests/pass/g08_loop_break_state_merge.tk`
- Negative tests: `tests/fail/pal_labeled_break_move_state.tk`,
  `tests/fail/pal_labeled_continue_move_state.tk`,
  `tests/fail/loop_break_state_maybe_unset.tk`,
  `tests/fail/loop_continue_move_state.tk`,
  `tests/fail/for_continue_move_state.tk`
- Interface replay requirements: none for purely local facts.
- Missing coverage: async suspension combined with local branch merge.

## Ownership Rules

### OWN-MOVE-001: Unique ownership is exclusive and moves invalidate the source path

- Status: Core guarantee
- Source form: moving or `cede`-ing a `^T` resource, then using the source
- Operation class: `OwnershipTransfer`, `Invalidation`
- Decision: the moved source path cannot be used after transfer.
- Rationale: a unique resource has one valid owner at a time.
- Primary diagnostics: `E0438`, `E0440`, `E04501`
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/Sema/Sema_Expr_Init.cpp`, `src/Sema/Sema_Stmt.cpp`
- Positive tests: `tests/pass/g08_sema_move_ok.tk`,
  `tests/pass/g08_linked_list_uniqueptr.tk`,
  `tests/pass/g08_smart_ptr_borrow.tk`
- Negative tests: `tests/fail/safety_use_moved.tk`,
  `tests/fail/sema_move.tk`, `tests/fail/unique_freeze_move.tk`,
  `tests/fail/unique_freeze_mutate.tk`, `tests/fail/move_in_loop.tk`,
  `tests/fail/move_direct_in_loop.tk`
- Interface replay requirements: function signatures must expose consuming
  parameters and cede returns.
- Missing coverage: source-less replay for generic unique-handle transfer.

### OWN-CEDE-001: `cede` parameters are explicit transfer obligations

- Status: Core guarantee
- Source form: `fn f(cede r: R)`, call `f(cede r)`, body consumes `r`
- Operation class: `CedeObligation`, `OwnershipTransfer`
- Decision: callers must explicitly pass `cede`; callees must consume, forward,
  store, return, or otherwise complete the obligation.
- Rationale: declared transfer is both permission and obligation.
- Primary diagnostics: `E0473`, `E0474`, `E04509`, `E04570`
- Implementation areas: `src/Sema/Sema_Expr_Call.cpp`,
  `src/Sema/Sema_Stmt.cpp`, `src/Sema/Sema_Type.cpp`
- Positive tests: `tests/pass/g03_test_cede.tk`,
  `tests/pass/g08_cede_param.tk`, `tests/pass/cede_exemptions.tk`
- Negative tests: `tests/fail/cede_param_missing.tk`,
  `tests/fail/cede_param_unconsumed.tk`,
  `tests/fail/cede_non_cede_parameter.tk`,
  `tests/fail/cede_param_double_unwrap.tk`,
  `tests/fail/cede_resource_missing.tk`
- Interface replay requirements: parameter cede-ness and return cede-ness must
  be preserved in `.tki`.
- Replay tests: `tests/semantics/tki_replay/cases/own_cede_001_signature`.
- Missing coverage: imported generic cede signatures and method cede
  signatures.

### OWN-CEDE-002: `cede` return types require explicit transfer at return sites

- Status: Core guarantee
- Source form: `fn make() -> cede R { return cede r }`
- Operation class: `CedeObligation`, `OwnershipTransfer`
- Decision: a function with a cede return must return through explicit `cede`.
- Rationale: ownership transfer across the function boundary must be visible.
- Primary diagnostics: `E0464`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/AST/TKIExporter.cpp`
- Positive tests: `tests/pass/g09_thread_example.tk`,
  `tests/pass/g08_sync_mpsc_bounded.tk`
- Negative tests: `tests/fail/expect_cede_return.tk`
- Interface replay requirements: return type cede marker must survive `.tki`.
- Replay tests: `tests/semantics/tki_replay/cases/own_cede_002_return`.
- Missing coverage: negative replay cases for misuse of cede-returned resources.

### OWN-RESOURCE-001: Resource values cannot be silently copied

- Status: Core guarantee
- Source form: copy capture, naked destructuring, spread, or implicit deref of
  a resource-bearing value
- Operation class: resource-copy prevention
- Decision: resource duplication requires explicit `clone`, explicit borrow, or
  explicit transfer, depending on the form.
- Rationale: silent bitwise copies can duplicate ownership and cause double
  drop.
- Primary diagnostics: `E0468`, `E0554`, `E04581`, `E04535`
- Implementation areas: `src/Sema/Sema_Expr_Closure.cpp`,
  `src/Sema/Sema_Expr_Init.cpp`, `src/Sema/Sema_Expr_Member.cpp`
- Positive tests: `tests/pass/g08_auto_clone_verified.tk`,
  `tests/pass/g04_destruct_match_ideal.tk`,
  `tests/pass/g07_implicit_borrow_match.tk`
- Negative tests: `tests/fail/destruct_resource_copy.tk`,
  `tests/fail/closure_copy_capture_resource.tk`,
  `tests/fail/spread_resource_no_cede.tk`,
  `tests/fail/implicit_deref_err.tk`
- Interface replay requirements: shape resource facts, drop/clone facts, and
  field morphology must remain compiler-visible.
- Missing coverage: `.tki` replay for private resource fields and clone/drop
  obligations.

## Effects And Escaping Dependency Rules

### EFF-RET-001: Escaping borrowed views require signature dependencies

- Status: Core guarantee
- Source form: `fn f(x: T) -> &T <- x`
- Operation class: `EscapingDependency`
- Decision: any borrow-like value crossing a function boundary must declare its
  dependency source in the signature or `effects:` block.
- Rationale: callers must not inspect callee bodies, and `.tki` replay must be
  semantically equivalent to source builds.
- Primary diagnostics: `E0454`, `E0455`, `E0456`, `E0457`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/Sema/Sema_Expr.cpp`, `src/AST/TKIExporter.cpp`
- Positive tests: `tests/pass/g03_escape_effects_basic.tk`,
  `tests/pass/g03_escape_func_life.tk`,
  `tests/pass/g04_escape_ref_life.tk`,
  `tests/pass/g07_escape_return_ref.tk`
- Negative tests: `tests/fail/return_ref.tk`,
  `tests/fail/ref_life_bound.tk`,
  `tests/fail/return_str_dependency_missing.tk`,
  `tests/fail/unsafe_cast_escape.tk`,
  `tests/fail/call_elision_escape.tk`
- Interface replay requirements: whole-return dependencies must be emitted and
  imported exactly.
- Replay tests: `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`.
- Missing coverage: dedicated `.tki` source-less replay for `str`/`bytes`
  escaping dependencies.

### EFF-MEMBER-001: Structural return dependencies must route to returned members

- Status: Core guarantee
- Source form: `effects: return.left <- a`
- Operation class: `EscapingDependency`
- Decision: dependency routing for returned members must match the actual
  returned structure; a whole-return dependency is not enough for swapped
  fields.
- Rationale: callers need member-specific dependency facts from the signature.
- Primary diagnostics: `E0457`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/AST/TKIExporter.cpp`
- Positive tests: `tests/pass/g08_pal_stress_test_borrow.tk`,
  `tests/pass/g08_pal_stress_test.tk`
- Negative tests: `tests/fail/return_member_dependency_swapped.tk`,
  `tests/fail/pal_call_return_dependency_replacement.tk`
- Interface replay requirements: member dependencies must be exported as
  `effects:` entries and consumed from `.tki`.
- Replay tests: `tests/semantics/tki_replay/cases/eff_member_001_return_deps`
  checks export, source-less parsing, and caller-side source locking for
  member-specific `effects:` declarations.
- Missing coverage: field-sensitive release or transfer tests proving that
  retaining only one returned member does not lock unrelated source paths;
  negative member-swap replay remains provider-side source validation only.

### EFF-SHAPE-001: Shape-internal dependency declarations are excluded from 1.0

- Status: Syntax exclusion
- Source form: `shape Ref <- owner`, `&view: T <- owner`
- Operation class: unsupported self-referential dependency
- Decision: shape header dependencies and shape-internal member dependency
  annotations are rejected in Toka 1.0.
- Rationale: stable placement and immovable construction semantics are not
  frozen for self-referential shapes.
- Primary diagnostics: `E01247`, `E01248`
- Implementation areas: `src/Parser/Parser_Decl.cpp`
- Positive tests: `tests/pass/g08_pal_stress_test_borrow.tk`
- Negative tests: `tests/fail/shape_header_dependency_removed.tk`,
  `tests/fail/shape_member_dependency_unsupported.tk`
- Interface replay requirements: borrow-like fields themselves must remain
  visible; unsupported dependency syntax must not be emitted as a substitute.
- Missing coverage: none known for parser rejection.

## Async And Execution Boundary Rules

### ASYNC-EFFECT-001: Async effects must be consumed by `.await`, `.wait`, or `.start`

- Status: Core guarantee
- Source form: async-producing call used as a dangling expression
- Operation class: async effect consumption
- Decision: async/task effects cannot be dropped silently.
- Rationale: suspension and scheduling are visible control-flow costs.
- Primary diagnostics: `E0702`
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/CodeGen/CodeGen_Expr.cpp`
- Positive tests: `tests/pass/g09_async_basic.tk`,
  `tests/pass/g09_async_wait_syntax.tk`,
  `tests/pass/g09_async_main.tk`,
  `tests/pass/g10_async_io_test.tk`,
  `tests/pass/g10_async_net_test.tk`
- Negative tests: `tests/fail/async_wait_conflict.tk`
- Interface replay requirements: async return shape and dependency annotations
  must remain visible from the signature.
- Missing coverage: negative tests for dangling async calls in more expression
  contexts.

### ASYNC-CAPTURE-001: Execution-boundary closures cannot carry hidden borrowed state

- Status: Core guarantee
- Source form: `thread_spawn({ => use outer })`
- Operation class: `ExecutionBoundaryCapture`
- Decision: closures crossing a thread/task boundary must use explicit
  `[cede ...]` or `[copy ...]` capture for state that crosses the boundary.
- Rationale: detached execution must not retain undeclared borrowed state.
- Primary diagnostics: `E04582`
- Implementation areas: `src/Sema/Sema_Expr_Call.cpp`,
  `src/Sema/Sema_Expr_Closure.cpp`
- Positive tests: `tests/pass/g09_thread_example.tk`,
  `tests/pass/g08_sync_mpsc_multi.tk`,
  `tests/pass/g09_std_atomic.tk`,
  `tests/pass/g10_net_read_exact.tk`
- Negative tests: `tests/fail/thread_spawn_implicit_capture_escape.tk`
- Interface replay requirements: execution-boundary consumers must be known from
  signatures/imports; closure capture mode must be checked at the call site.
- Missing coverage: `.start` task handoff with implicit borrowed capture.

### ASYNC-SUSPEND-001: Borrow-like async results carry ordinary dependency annotations

- Status: Core guarantee
- Source form: `fn f(x: str) -> async str <- x`
- Operation class: `EscapingDependency`, async return dependency
- Decision: async return dependencies describe the eventual value; they do not
  authorize detached tasks to keep undeclared borrowed state.
- Rationale: async color and dependency routing are orthogonal.
- Primary diagnostics: `E0454`, `E0457`, `E04582`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/Sema/Sema_Expr_Call.cpp`, `src/AST/TKIExporter.cpp`
- Positive tests: `tests/pass/g09_async_prove.tk`,
  `tests/pass/g09_context.tk`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Negative tests: none dedicated
- Interface replay requirements: async return type and dependency facts must be
  preserved together.
- Replay tests: `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
  checks export, source-less parsing, and caller-side dependency enforcement
  after consuming `async str <- x` with `.wait`.
- Missing coverage: detached `.start` handoff policy and dependency enforcement
  across suspension inside an async function.

## Interface Replay And Cache Rules

### TKI-REPLAY-001: `.tki` must preserve all semantic facts needed by callers

- Status: Core guarantee
- Source form: import from source or from `.tki`
- Operation class: `InterfaceReplay`
- Decision: source builds and `.tki` replay must agree for frozen semantics.
- Rationale: incremental and source-less builds cannot weaken safety.
- Primary diagnostics: source-dependent; cache statuses include
  `CompilerVersionMismatch`, `FormatVersionMismatch`, `TargetTripleMismatch`,
  `SourceHashMismatch`
- Implementation areas: `src/AST/TKIExporter.cpp`,
  `src/Basic/ModuleResolver.cpp`, `src/main.cpp`,
  `include/toka/InterfaceVersion.h`
- Positive tests: `tests/pass/g07_separate_compile_test.tk`,
  `tests/fixtures/incremental_project/src/main.tk`,
  `tests/fixtures/incremental_project/src/lib.tk`,
  `tests/semantics/tki_replay/cases/pal_call_001_alias`,
  `tests/semantics/tki_replay/cases/own_cede_001_signature`,
  `tests/semantics/tki_replay/cases/own_cede_002_return`,
  `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`,
  `tests/semantics/tki_replay/cases/eff_member_001_return_deps`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Negative tests: tests driven by `tools/scripts/test_tki_cache_validation.sh`
  and `tools/scripts/test_semantic_replay.sh`
- Interface replay requirements: see `tki_semantic_contract.md`.
- Missing coverage: rule-by-rule replay tests for private resource fields,
  unsafe public API redlines, cache invalidation from semantic-only annotation
  changes, and async negative dependency-return cases.

### TKI-CACHE-001: Semantic cache metadata must invalidate stale or incompatible interfaces

- Status: Core guarantee
- Source form: cached interface loaded for an imported module
- Operation class: `InterfaceReplay`, cache invalidation
- Decision: cache metadata must reject incompatible compiler versions, interface
  format versions, target triples, missing semantic metadata, and source hash
  mismatches.
- Rationale: stale interfaces can otherwise replay old semantic facts.
- Primary diagnostics: dependency manifest `cache_status` values
- Implementation areas: `src/main.cpp`, `docs/dependency_manifest_schema.md`,
  `tools/scripts/test_tki_cache_validation.sh`
- Positive tests: `tools/scripts/test_incremental_build.sh`,
  `tools/scripts/test_tki_cache_validation.sh`
- Negative tests: cache validation script cases
- Interface replay requirements: every semantic fact in
  `tki_semantic_contract.md` must participate in interface format stability.
- Missing coverage: direct tests that mutate only semantic annotations without
  changing ABI-like shape.

## Unsafe Boundary Rules

### UNSAFE-PUB-001: Public safe APIs must not expose raw unsafe representation silently

- Status: Core guarantee
- Source form: public function or shape exposing raw pointer types without
  unsafe/raw naming
- Operation class: unsafe public API redline
- Decision: public safe APIs must either hide raw pointers behind safe types or
  make unsafe/raw status explicit in the name.
- Rationale: raw pointers are outside PAL's safe-borrow guarantee.
- Primary diagnostics: `E0480`, `E0481`, `E0482`
- Implementation areas: `src/Sema/Sema.cpp`,
  `tools/scripts/test_public_unsafe_api.sh`
- Positive tests: `tests/pass/g03_unsafe_null_privilege.tk`
- Negative tests: `tests/fail/pub_api_exposed_raw_ptr.tk`,
  `tests/fail/pub_shape_exposed_ptr.tk`
- Interface replay requirements: public exported signatures and fields must
  preserve raw pointer exposure.
- Missing coverage: source-less `.tki` public unsafe API redline tests.
