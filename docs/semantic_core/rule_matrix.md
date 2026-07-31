# Toka 1.0 Semantic Rule Matrix

This matrix records the semantic-core freeze audit. It maps the current
language contract to implementation areas and existing tests. It does not
define new language behavior.

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
- Replay tests: `tests/semantics/tki_replay/cases/pal_call_001_alias` covers
  read/read acceptance, disjoint member mutation, overlapping member
  rejection, and cede/read alias rejection through source and source-less
  imports.
- Coverage closure: none known for the frozen simultaneous-call group.

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
  `tests/pass/g08_pal_stress_test_borrow.tk`,
  `tests/conformance/ownership/mutable_borrow_fixed_array_index_write.tk`,
  `tests/conformance/ownership/match_mutable_reference_fixed_index_write.tk`,
  `tests/conformance/ownership/guard_mutable_reference_fixed_index_write.tk`,
  `tests/conformance/ownership/nested_pattern_mutable_reference_write.tk`,
  `tests/conformance/ownership/nested_pattern_reference_disjoint_field_cede.tk`,
  `tests/conformance/ownership/destructure_reference_disjoint_field_cede.tk`,
  `tests/conformance/ownership/for_reference_fixed_array_lifetime.tk`
- Negative tests: `tests/fail/borrow.tk`, `tests/fail/borrow_field.tk`,
  `tests/fail/safety_double_mut.tk`,
  `tests/fail/pal_member_mut_borrow_duplicate.tk`,
  `tests/fail/pal_member_mut_borrow_payload_read.tk`,
  `tests/fail/pal_member_mut_borrow_payload_write.tk`,
  `tests/conformance/diagnostics/match_reference_fixed_index_cede_conflict.tk`,
  `tests/conformance/diagnostics/guard_reference_fixed_index_cede_conflict.tk`,
  `tests/conformance/diagnostics/nested_match_reference_field_cede_conflict.tk`,
  `tests/conformance/diagnostics/nested_guard_reference_field_cede_conflict.tk`,
  `tests/conformance/diagnostics/destructure_reference_field_cede_conflict.tk`,
  `tests/conformance/diagnostics/for_reference_fixed_array_cede_conflict.tk`
- Interface replay requirements: no callee-body dependence for local borrow
  facts; escaping borrowed views use `EFF-*` rules.
- Coverage closure: `tests/pass/g08_pal_stress_test_borrow.tk` exercises deep
  member chains, disjoint mutable/immutable paths, handle-bearing members, and
  payload/interior mutation. No cross-module fact is required for this local
  rule.

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
  `tests/pass/g08_pal_shared_borrow_array_payload_write.tk`,
  `tests/conformance/ownership/shared_borrow_fixed_array_disjoint_write.tk`
- Negative tests: `tests/fail/borrow_move.tk`,
  `tests/fail/cede_borrowed.tk`, `tests/fail/fail_pal_move_locked.tk`
- Interface replay requirements: escaping shared borrow dependencies must be
  declared in signatures or `effects:`.
- Replay tests: `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`
  covers returned references, `str`, and `bytes`, including source replacement
  rejection after source-less import.
- Coverage closure: none known for the frozen shared-borrow validity rule.

### PAL-INTERIOR-001: A `field#` declaration grants local interior mutation

- Status: Core guarantee
- Source form: `auto &shared = &object; shared.interior_field = value`
  where `interior_field#` is declared on the shape.
- Decision: the field declaration is the Payload capability source. It remains
  writable through a shared aggregate view without making ordinary sibling
  fields writable. Borrows of that field use the normal PAL shared/exclusive
  operation classes; an exclusive borrow still rejects overlapping access.
- Rationale: this is Toka's zero-cost, field-level interior mutability. It is
  not a use-site permission elevation and is not a thread-safety proof.
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/Sema/Sema_Expr_Member.cpp`, `src/Sema/Sema_Expr_Binary.cpp`, and
  `src/Sema/PAL_Checker.cpp`.
- Positive tests: `tests/pass/g04_token_interior_mut.tk`,
  `tests/pass/g08_pal_stress_test_borrow.tk`, and
  `tests/conformance/ownership/interior_mutable_field_shared_view.tk`.
- Negative tests:
  `tests/conformance/diagnostics/interior_mutable_sibling_requires_declaration.tk`
  , `tests/conformance/diagnostics/interior_mutable_field_exclusive_borrow_conflict.tk`,
  and `tests/fail/pal_member_mut_borrow_payload_write.tk`.
- Replay tests:
  `tests/semantics/tki_replay/cases/interior_mutability_001_field` verifies
  that exported field-level capability is preserved for both source-backed and
  source-less consumers.

### PAL-PATH-001: Overlap is path-prefix based, with disjoint fields allowed

- Status: Core guarantee
- Source form: borrow `obj.left`, mutate/read `obj.right`
- Operation class: path overlap for all PAL operation classes
- Decision: overlapping path prefixes conflict; disjoint fields and distinct
  constant fixed-array indices may be borrowed and mutated independently when
  represented as distinct source paths.
- Rationale: PAL is a local Path-Anchored Ledger.
- Primary diagnostics: `E0441`, `E0442`, `E0443`
- Implementation areas: `PALChecker::pathsOverlap`,
  `PALChecker::verifyOperation`
- Positive tests: `tests/pass/g08_pal_stress_test_borrow.tk`,
  `tests/pass/g08_pal_stress_test.tk`,
  `tests/conformance/ownership/shared_borrow_fixed_array_disjoint_write.tk`
- Negative tests: `tests/fail/fail_pal_path_prefix.tk`,
  `tests/fail/borrow_field.tk`
- Interface replay requirements: member morphology and field structure needed
  for semantic checking must survive `.tki` export even for private fields.
- Coverage closure: downstream code cannot name private fields, so there is no
  runnable private-field disjointness call-site path. Private structural facts
  are preserved and replayed by
  `tests/semantics/tki_replay/cases/own_resource_001_private_field` and
  `own_resource_002_spread_generic`; public member paths are covered by
  `pal_call_001_alias`.

### PAL-CFG-001: Local control-flow states are merged conservatively

- Status: Core guarantee
- Source form: `if`, `guard`, `match`, `loop`, `for`, `break`, `continue`
  with move, unset, or borrow state changes
- Operation class: `Invalidation`, move state, init state, borrow state
- Decision: branch and loop state must merge without allowing use-after-move,
  use-before-init, or invalid borrowed state.
- Rationale: local control-flow analysis may be used inside a function, but
  calls consume only signatures.
- Primary diagnostics: `E0410`, `E0438`, `E0440`, `E04501`
- Implementation areas: `src/Sema/Sema_Stmt.cpp`,
  `src/Sema/PAL_Checker.cpp`
- Positive tests: `tests/pass/g08_pal_if_branch_restore.tk`,
  `tests/pass/g08_pal_guard_branch_state.tk`,
  `tests/pass/g08_pal_match_branch_state.tk`,
  `tests/pass/g08_pal_labeled_break_state_merge.tk`,
  `tests/pass/g08_pal_labeled_continue_local_move.tk`,
  `tests/pass/g08_for_break_or_state_merge.tk`,
  `tests/pass/g08_loop_break_state_merge.tk`,
  `tests/pass/g09_async_suspension_state.tk`
- Negative tests: `tests/fail/pal_labeled_break_move_state.tk`,
  `tests/fail/pal_labeled_continue_move_state.tk`,
  `tests/fail/loop_break_state_maybe_unset.tk`,
  `tests/fail/loop_continue_move_state.tk`,
  `tests/fail/for_continue_move_state.tk`,
  `tests/fail/async_suspension_branch_move_state.tk`,
  `tests/fail/async_suspension_maybe_unset.tk`,
  `tests/fail/async_suspension_continue_move_state.tk`
- Interface replay requirements: none for purely local facts.
- Coverage closure: none known for frozen local state merges.

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
- Replay tests: `tests/semantics/tki_replay/cases/own_cede_003_generic_methods`
  covers generic resource transfer through imported functions and methods,
  including use-after-move rejection.
- Coverage closure: none known for frozen generic unique transfer.

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
- Replay tests: `tests/semantics/tki_replay/cases/own_cede_001_signature` and
  `tests/semantics/tki_replay/cases/own_cede_003_generic_methods` cover plain,
  generic, and method cede signatures.
- Coverage closure: none known for frozen cede parameter obligations.

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
- Replay tests: `tests/semantics/tki_replay/cases/own_cede_002_return` covers
  valid binding, double consumption, and use after transfer through source and
  source-less imports.
- Coverage closure: none known for frozen cede returns.

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
- Positive tests: `tests/pass/g08_explicit_clone_verified.tk`,
  `tests/pass/g04_destruct_match_ideal.tk`,
  `tests/pass/g07_implicit_borrow_match.tk`
- Negative tests: `tests/fail/destruct_resource_copy.tk`,
  `tests/fail/closure_copy_capture_resource.tk`,
  `tests/fail/spread_resource_no_cede.tk`
- Interface replay requirements: shape resource facts, drop/clone facts, and
  field morphology must remain compiler-visible.
- Replay tests: `tests/semantics/tki_replay/cases/own_resource_001_private_field`
  checks private resource structure, `drop`/deleted `clone`, copy capture, and
  naked destructuring through a source-less interface;
  `tests/semantics/tki_replay/cases/own_resource_002_spread_generic` covers
  generic private resource fields, spread, and copy capture.
- Coverage closure: none known for the frozen copy-prevention forms.

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
- Replay tests: `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`
  covers reference, `str`, and `bytes` return dependencies and rejects source
  replacement while each returned view is live;
  `ergonomics_002_closure_dependencies` preserves `fn(...) <- source` and
  rejects moving the captured source through source and source-less paths.
- Coverage closure: none known for frozen whole-return dependencies.

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
  member-specific `effects:` declarations, selective member transfer,
  unrelated-source release, and swapped member routing.
- Cache invalidation tests:
  `tests/semantics/tki_cache/cases/member_effect_swap`.
- Coverage closure: none known for frozen structural return routing.

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
- Revalidation tests: `tools/scripts/test_tki_excluded_syntax_revalidation.sh`
  proves forged shape-header and member dependency syntax is rejected with
  `E01247` and `E01248` during interface parsing.
- Coverage closure: none known for the 1.0 syntax exclusion.

## Async And Execution Boundary Rules

### ASYNC-EFFECT-001: Async effects must be consumed by `.await`, `.wait`, or `.start`

- Status: Core guarantee
- Source form: async-producing call used as a dangling expression
- Operation class: async effect consumption
- Decision: async/task effects cannot be dropped silently.
- Rationale: suspension and scheduling are visible control-flow costs.
- Primary diagnostics: `E0702`, `E0715`, `E04585`
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/CodeGen/CodeGen_Expr.cpp`
- Positive tests: `tests/pass/g09_async_basic.tk`,
  `tests/pass/g09_async_wait_syntax.tk`,
  `tests/pass/g09_async_main.tk`,
  `tests/pass/g10_async_io_test.tk`,
  `tests/pass/g10_async_net_test.tk`
- Negative tests: `tests/fail/async_wait_conflict.tk`,
  `tests/fail/async_await_requires_async_function.tk`,
  `tests/fail/async_wait_inside_async.tk`,
  `tests/fail/async_dangling_expression_contexts.tk`
- Interface replay requirements: async return shape and dependency annotations
  must remain visible from the signature.
- Coverage closure: dangling direct and conditional-block async calls are
  rejected; none known for the frozen expression contexts.

### ASYNC-CAPTURE-001: Execution boundaries cannot carry hidden borrowed state

- Status: Core guarantee
- Source form: `thread_spawn({ => use outer })`, `worker(data).start`
- Operation class: `ExecutionBoundaryCapture`
- Decision: closures crossing a thread/task boundary must use explicit
  `[cede ...]` or `[copy ...]` capture for state that crosses the boundary.
  `.start` accepts only non-borrowing scalars or values transferred through a
  `cede` parameter and explicit `cede` call argument. It rejects PAL
  dependencies and does not implicitly copy ordinary shapes.
- Rationale: detached execution must not retain undeclared borrowed state.
- Primary diagnostics: `E04582`, `E04583`, `E04584`
- Implementation areas: `src/Sema/Sema_Expr_Call.cpp`,
  `src/Sema/Sema_Expr_Closure.cpp`
- Positive tests: `tests/pass/g09_thread_example.tk`,
  `tests/pass/g08_sync_mpsc_multi.tk`,
  `tests/pass/g09_std_atomic.tk`,
  `tests/pass/g10_net_read_exact.tk`,
  `tests/semantics/tki_replay/cases/async_start_001_cede_handoff`
- Negative tests: `tests/fail/thread_spawn_implicit_capture_escape.tk`,
  `tests/fail/start_borrowed_shape.tk`,
  `tests/fail/start_borrowed_method_self.tk`,
  `tests/fail/start_borrowed_static_argument.tk`,
  `tests/fail/start_borrowed_str.tk`,
  `tests/fail/start_cede_borrowed_str.tk`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Interface replay requirements: execution-boundary consumers must be known from
  signatures/imports; closure capture mode must be checked at the call site.
- Replay tests: `tests/semantics/tki_replay/cases/async_start_001_cede_handoff`
  covers concrete and generic shape/resource handoff through both sides of the
  cede contract.
- Coverage closure: none known for the frozen `.start` contract. Trait-gated
  Send/Sync widening is Post1.0 and is not a missing 1.0 case.

### ASYNC-SUSPEND-001: Suspension preserves local state and async-result dependencies

- Status: Core guarantee
- Source form: `fn f(x: str) -> async str <- x`, local state used across
  `.await`, and suspension inside branch/loop control flow
- Operation class: `EscapingDependency`, async return dependency, local
  init/move/PAL state
- Decision: suspension does not end scope or reset analysis state. Async return
  dependencies describe the eventual value and remain active after resume;
  they do not authorize detached tasks to keep undeclared borrowed state.
- Rationale: async color and dependency routing are orthogonal, while a
  coroutine frame is still governed by the same local ownership and PAL rules.
- Primary diagnostics: `E0410`, `E0438`, `E0440`, `E04501`, `E0454`, `E0457`,
  `E04583`, `E04584`
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/Sema/Sema_Stmt.cpp`, `src/Sema/Sema_Expr_Call.cpp`,
  `src/AST/TKIExporter.cpp`
- Positive tests: `tests/pass/g09_async_prove.tk`,
  `tests/pass/g09_context.tk`,
  `tests/pass/g09_async_suspension_state.tk`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Negative tests: `tests/fail/async_suspension_borrow_move.tk`,
  `tests/fail/async_suspension_branch_move_state.tk`,
  `tests/fail/async_suspension_maybe_unset.tk`,
  `tests/fail/async_suspension_continue_move_state.tk`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Interface replay requirements: async return type and dependency facts must be
  preserved together.
- Replay tests: `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
  checks export, source-less parsing, and caller-side dependency enforcement
  after consuming `async str <- x` with both `.wait` and `.await`, rejects
  invalidation after resume, and rejects the same borrowed result across
  `.start`.
- Coverage closure: none known for the frozen 1.0 suspension/state boundary.

### ASYNC-LIFECYCLE-001: Task frames and detached state retain one explicit owner

- Status: `Frozen` for normal completion and detach; cancellation remains
  `Post1.0`.
- Runtime form: `TaskHandle` drop, `detach_forget`, scheduler completion,
  context propagation helpers.
- Rule: dropping or explicitly detaching a live handle transfers frame cleanup
  to the scheduler; a completed frame is destroyed exactly once. Detached
  helpers must own state carried across their execution boundary rather than
  retain an unowned raw address.
- Conservative boundary: force-destroy cancellation, task groups, recursive
  child cancellation, and awaiter unlinking are not part of the 1.0 contract.
- Evidence: `g09_async_detached_lifecycle.tk`,
  `task_group_cancellation_post_1_0.tk`, and
  `semantic_core/async_runtime_lifecycle_audit.md`.

## Iterator Protocol Rules

### ITER-PROTOCOL-001: Non-array iteration is resolved through formal facets

- Source form: `for auto x in values` and `for auto &x in values`
- Operation class: protocol resolution and morphology-selected iteration
- Decision: a non-array collection must implement `@Iterable`; its associated
  cursor must implement `@Iterator`, and reference iteration additionally
  requires `@BorrowIterator`. Inherent methods with the same names do not
  satisfy the protocol.
- Diagnostics: `E04587` for a structural-only collection and `E04588` for a
  cursor missing the required facet. Existing `E04498`/`E04500` retain the
  `Option<Item>` return-shape checks.
- Positive tests: `tests/pass/g07_for_iterators.tk`,
  `tests/pass/g08_iterator_pal_protocol.tk`, and
  `tests/pass/g08_iterator_hidden_drop.tk`.
- Negative tests: `tests/fail/iterator_structural_protocol_rejected.tk` and
  `tests/fail/iterator_missing_source_dependency.tk`.
- Interface replay requirements: `@Iterable::Iter`, `@Iterator::Item`,
  `@BorrowIterator::BorrowedItem`, method signatures, and dependencies are
  exported together.
- Replay tests: `tests/semantics/tki_replay/cases/iterator_001_protocol` covers
  value and borrowed loops plus a source-mutation rejection through both
  source-backed and source-less imports.

### ITER-LIFETIME-001: Cursor lifetime remains anchored to its source

- Source form: `auto cursor# = values.iter()` or an implicit cursor created by
  `for`.
- Operation class: escaping dependency, PAL shared borrow, and deterministic
  cleanup.
- Decision: `@Iterable::iter` and `@BorrowIterator::next_ref` must declare
  `<- self`. A live cursor prevents mutation, replacement, move, or cede of the
  source. A cursor cannot escape a local source. The compiler-generated cursor
  is dropped on normal exhaustion, `break`, and function return.
- Diagnostics: `E04589` for a missing protocol dependency, `E0441` for source
  mutation while borrowed, and `E0455` for a cursor escaping a local source.
- Positive tests: `tests/pass/g08_iterator_pal_protocol.tk` proves the borrow is
  released after the loop; `tests/pass/g08_iterator_hidden_drop.tk` observes
  exact hidden-cursor drop on `break` and return.
- Negative tests: `tests/fail/iterator_mutate_source_in_loop.tk`,
  `tests/fail/iterator_saved_cursor_mutate_source.tk`, and
  `tests/fail/iterator_escape_local_source.tk`.
- Library closure: Vec, HashMap, and HashSet cursors carry explicit borrowed
  slices or nested dependent cursors. The build manifest update path now uses
  two phases rather than mutating a HashMap while traversing it.
- Coverage closure: synchronous value and borrow iteration are closed. Toka
  1.0 intentionally has no consuming-iterator or async-iterator contract.

## Callable Protocol Rules

### CALL-MODE-001: Invocation permission is receiver morphology

- Status: Core guarantee
- Source form: `fn(...)`, `fn#(...)`, `cede fn(...)`, and closure literals
- Operation class: shared access, exclusive mutation, or ownership transfer
- Decision: closure bodies infer shared, mutable, or consuming invocation from
  operations rooted at captures. Calls must use ordinary, `#`, or `cede`
  morphology respectively, and exclusive calls require a writable binding.
- Rationale: callable access must use the same ownership vocabulary and PAL
  conflict model as methods instead of a parallel `Fn/FnMut/FnOnce` taxonomy.
- Primary diagnostics: `E04590`, `E04591`, `E04592`
- Implementation areas: `src/Sema/Sema_Expr_Closure.cpp`,
  `src/Sema/Sema_Expr_Call.cpp`, `src/Sema/Sema_Type.cpp`, `src/Type.cpp`
- Positive tests: `tests/pass/g08_callable_protocol.tk`,
  `tests/pass/g08_dyn_closure.tk`, and
  `tests/pass/g09_iterator_closure_async_composition.tk`
- Negative tests: `tests/fail/callable_mutable_shared_call.tk`,
  `tests/fail/callable_mutable_immutable_binding.tk`, and
  `tests/fail/callable_consuming_without_cede.tk`
- Runtime closure: consuming invocation owns one drop-live obligation per value
  capture; moved captures and remaining captures are finalized independently.
- Interface replay requirements: callable receiver mode is part of `fn` and
  `dyn fn` type identity, and mutable call syntax in retained generic bodies is
  emitted as `f#(...)`.

### CALL-PROTOCOL-001: User callables require formal `@Callable` conformance

- Status: Core guarantee
- Source form: `impl Type@Callable { fn call(self...) ... }`
- Operation class: protocol resolution and generic bound checking
- Decision: closures implement the implicit-prelude protocol automatically;
  user shapes require explicit conformance. An inherent `call` method alone
  does not grant call syntax.
- Rationale: generic algorithms need one inspectable, replayable callable
  contract without structural-name accidents.
- Primary diagnostic: `E04593`
- Positive tests: shared and mutable user callables plus `F: @Callable` in
  `tests/pass/g08_callable_protocol.tk`
- Negative test: `tests/fail/callable_protocol_missing.tk`
- Composition coverage: the positive case combines an exclusive callback with
  value iteration; the ERG-5 case covers shared, exclusive, and consuming
  generic callbacks plus detached async iteration across suspension;
  synchronization tests exercise exclusive thread callbacks.
- Replay tests: `tests/semantics/tki_replay/cases/callable_001_modes` covers a
  retained generic callable body, a `dyn fn#` return signature, and source-less
  rejection of a shared call.

## Error Propagation Rules

### ERROR-PROP-001: `!` is a consuming, typed early return

- Status: Core guarantee
- Source form: `result!`, `.await!`, and `E1: @ErrorInto<E2>`
- Operation class: ownership transfer, error conversion, deterministic cleanup
- Decision: same-type errors move directly; different errors require exactly
  one `into_error(cede self) -> Target` implementation. Numeric widening,
  structural compatibility, raw layout copying, and conversion chains do not
  participate.
- Rationale: propagation must preserve ordinary Toka ownership and make every
  cross-type conversion inspectable at the call site and interface boundary.
- Primary diagnostics: `E04594` for a missing conversion, `E04595` for a
  partial path, `E04596` for the frozen entry return boundary, and `E04597`
  for a non-consuming or otherwise incompatible protocol method.
- Implementation areas: `src/Sema/Sema_Expr.cpp`,
  `src/CodeGen/CodeGen_Expr.cpp`, `src/Parser/Parser_Decl.cpp`, and
  `lib/core/traits.tk`.
- Positive tests: `tests/pass/g08_error_conversion_protocol.tk` and
  `tests/pass/g09_error_propagation_cleanup_async.tk`.
- Negative tests: `tests/fail/error_conversion_missing.tk`,
  `tests/fail/error_conversion_numeric_implicit.tk`,
  `tests/fail/error_conversion_non_consuming_impl.tk`,
  `tests/fail/error_propagation_complex_path.tk`, and
  `tests/fail/main_result_return.tk`.
- Cleanup contract: the operand is evaluated once, the Result and selected
  payload move once, and every remaining local drops before return. The async
  fixture observes the same rule after suspension.
- Interface replay requirements: the parameterized trait implementation and
  `into_error` signature must survive `.tki`; callers never inspect its body.
- Replay tests: `tests/semantics/tki_replay/cases/error_001_conversion` compares
  direct and generic conversion decisions plus missing-conversion rejection
  through source-backed and source-less providers.

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
  `tests/semantics/tki_replay/cases/own_cede_003_generic_methods`,
  `tests/semantics/tki_replay/cases/own_resource_001_private_field`,
  `tests/semantics/tki_replay/cases/own_resource_002_spread_generic`,
  `tests/semantics/tki_replay/cases/eff_ret_001_return_deps`,
  `tests/semantics/tki_replay/cases/eff_member_001_return_deps`,
  `tests/semantics/tki_replay/cases/async_start_001_cede_handoff`,
  `tests/semantics/tki_replay/cases/async_suspend_001_return_deps`
- Negative tests: tests driven by `tools/scripts/test_tki_cache_validation.sh`,
  `tools/scripts/test_semantic_replay.sh`, and
  `tools/scripts/test_semantic_cache_invalidation.sh`, plus excluded-syntax
  revalidation in `tools/scripts/test_tki_excluded_syntax_revalidation.sh`
- Interface replay requirements: see `tki_semantic_contract.md`.
- Closure coverage: unsafe public API redlines, resource/generic/trait cache
  changes, generic cede functions and methods, detached async handoff, member
  routing, `str`/`bytes` dependencies, generic private-resource fields, and
  excluded shape-dependency syntax are covered by the dedicated replay,
  revalidation, and cache matrices.
- Coverage closure: none known for the frozen same-version TKI fact classes.

### TKI-NOMINAL-001: Shape identity follows the declaring module

- Status: Core guarantee
- Source form: local and imported shapes with the same unqualified name
- Operation class: nominal type resolution, interface replay, lowering identity
- Decision: a resolved shape is identified by its declaration; caller lexical
  lookup must not rebind an imported value by bare spelling.
- Rationale: process-global name maps otherwise merge private layouts and can
  make source-backed and source-less imports accept, reject, or lower different
  programs.
- Primary diagnostic: `E04571`, with module qualification when both displayed
  type names would otherwise be identical.
- Implementation areas: `src/Sema/Sema.cpp`, `src/Sema/Sema_Type.cpp`,
  `src/Sema/Sema_Expr_Init.cpp`, `src/Type.cpp`,
  `src/CodeGen/CodeGen_Decl.cpp`, and `src/CodeGen/CodeGen_Expr.cpp`
- Positive test: `tests/pass/g09_module_private_shape_isolation.tk`
- Replay tests:
  `tests/semantics/tki_replay/cases/ergonomics_003_shape_identity`
- Interface replay requirements: exported signatures reconstruct a declaration
  owned by the provider module; direct shape parameters and inferred return
  values retain that identity.
- Lowering boundary: collision-free internal struct names are version-bound
  implementation details, not a stable ABI.
- Detailed rule: `docs/semantic_core/module_shape_identity.md`
- Coverage closure: concrete non-generic same-name structs and enums, distinct
  layouts and variants, local use, imported parameter/return use, cross-module
  mismatch, and source-less replay are covered. Same-name generic template
  isolation remains the bounded `FZ-3-R02` audit.

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
  `tools/scripts/test_tki_cache_validation.sh`,
  `tools/scripts/test_semantic_cache_invalidation.sh`
- Positive tests: `tools/scripts/test_incremental_build.sh`,
  `tools/scripts/test_tki_cache_validation.sh`,
  `tools/scripts/test_semantic_cache_invalidation.sh`
- Negative tests: cache validation script cases
- Interface replay requirements: every semantic fact in
  `tki_semantic_contract.md` must participate in interface format stability.
- Semantic-only invalidation tests: `tests/semantics/tki_cache/cases` covers
  parameter mutability, `cede` parameters, effects routing and swapping, async
  effect markers, private resource structure, deleted clone, generic function
  bounds, generic impl where constraints, trait prerequisites, associated type
  bindings, and dyn object safety. Each case proves old-interface acceptance,
  `SourceHashMismatch` fallback, source-side rejection, and rejection through a
  freshly emitted source-less interface.
- Coverage closure: none known for the semantic fact classes listed in the TKI
  contract's cache invalidation rules.

## Public Contract And Runtime Rules

### CONTRACT-ABI-001: Logical parameter capture is independent of target lowering

- Status: Core guarantee
- Source form: scalar or aggregate payload parameter
- Operation class: call borrow, target ABI lowering
- Decision: ordinary parameters use PAL's logical in-place capture semantics;
  physical register, pointer, aggregate, closure, and return-storage lowering
  does not create a source-level copy or a stable binary ABI promise.
- Rationale: source ownership and mutation behavior must not depend on a target
  calling convention.
- Primary diagnostics: ordinary PAL and mutability diagnostics
- Implementation areas: `src/CodeGen/CodeGen_Decl.cpp`,
  `src/CodeGen/CodeGen_Expr.cpp`
- Positive tests: `tests/pass/g05_logical_capture_abi_boundary.tk`
- Interface replay requirements: `.tki` preserves parameter semantics and
  morphology, not cross-version machine calling conventions.

### CONTRACT-EXCLUSION-001: Post-1.0 formatting and global destructuring are explicit exclusions

- Status: Conservative rejection
- Source form: a String/str format specifier or module-scope destructuring
- Operation class: frozen 1.0 surface exclusion
- Decision: plain `{}` text formatting remains valid; text format specifiers
  and global destructuring are outside 1.0.
- Rationale: diagnostics must state a frozen boundary instead of implying an
  untracked temporary implementation gap.
- Primary diagnostics: `E04547`, `E0744`
- Implementation areas: `src/Sema/Sema_Expr_Call.cpp`,
  `src/CodeGen/CodeGen_Decl.cpp`
- Negative tests: `tests/fail/formatted_text_excluded_1_0.tk`,
  `tests/fail/global_destructuring_excluded_1_0.tk`
- Interface replay requirements: none; neither construct creates an exported
  frozen declaration.

### RUNTIME-001: Normal cleanup is deterministic and panic is non-unwinding termination

- Status: Core guarantee
- Source form: normal scope exit, ownership transfer, or runtime panic
- Operation class: resource cleanup, runtime failure
- Decision: each live owned value is cleaned exactly once on normal exits;
  move and `cede` transfer that obligation. Panic is non-returning process
  termination and does not promise catchability, stack unwinding, or cleanup
  after the panic point.
- Rationale: the minimum runtime behavior needed by frozen ownership semantics
  must be public without promising an exception model that does not exist.
- Primary diagnostics: runtime panic output; compile-time ownership diagnostics
- Implementation areas: `src/CodeGen/CodeGen_Stmt.cpp`,
  `src/CodeGen/CodeGen_Memory.cpp`, `lib/sys/toka_rt.c`
- Positive tests: `tests/pass/g09_resource_cleanup_matrix.tk`,
  `tests/pass/g08_nullable_pointer_unwrap_panic.tk`
- Interface replay requirements: ownership transfer and resource facts replay;
  panic transport is a same-toolchain runtime contract, not `.tki` metadata.

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
  `src/Basic/ModuleResolver.cpp`, `tools/scripts/test_public_unsafe_api.sh`,
  `tools/scripts/test_tki_unsafe_revalidation.sh`
- Positive tests: `tests/pass/g03_unsafe_null_privilege.tk`
- Negative tests: `tests/fail/pub_api_exposed_raw_ptr.tk`,
  `tests/fail/pub_shape_exposed_ptr.tk`
- Interface replay requirements: public exported signatures and fields must
  preserve raw pointer exposure; interface metadata cannot grant path-based
  exemptions. Only resolver-proven standard-library interfaces retain the
  trusted-system exemption.
- Source-less coverage: parameter `E0480`, return `E0481`, field `E0482`,
  generic declarations, forged `lib/prelude/tests/pass/build.tk` paths,
  ordinary include paths, explicit unsafe/raw names, and compiler-configured
  trusted roots.
