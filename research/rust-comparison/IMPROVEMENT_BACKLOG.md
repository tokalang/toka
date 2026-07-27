# Improvement candidates discovered by comparison cases

This is a two-way design backlog.  A candidate exists because a real comparison
case exposed an ergonomic, diagnostic, or abstraction cost.  It is not a
release blocker unless independently entered into the 1.0 gap ledger.

## EXP-001 — Make H/P diagnostics capability-aware

- Status: implemented locally; verified by comparison diagnostics and pending
  ordinary release review
- Evidence: `expressiveness/01_handle_payload_permissions` rejects an attempted
  use-site elevation with `E04571` (`expected ^Cell#, got ^#Cell`).  This is
  correct, but it presents the distinction as a morphology mismatch rather
  than naming the semantic rule: use-site `#` requests a capability and cannot
  create one absent from the declaration/signature.
- Improvement: retain `E04571`, but add a targeted note explaining whether the
  missing axis is handle replacement or payload write, and point to the
  parameter/binding declaration that controls it.
- Non-goal: changing the frozen H/P authority rule.

## EXP-002 — Avoid misleading unused-variable warnings for payload use

- Status: implemented locally; verified by comparison diagnostics and pending
  ordinary release review
- Evidence: the valid `fn overwrite_payload(^p#: Cell) { p.value = 13 }` case
  emits `W0402` for `p` and `W0407` for the unused handle view.  The latter is
  useful; the former suggests that the complete variable is unused even though
  its payload is used.  Similarly, a payload-only mutation performed in a
  callee can trigger a mutable-binding warning at the caller.
- Improvement: make unused analysis H/P-aware.  Report an unused Handle view
  only when appropriate, suppress the generic whole-binding unused warning
  after a payload use, and distinguish local mutation from capability consumed
  by a call.
- Non-goal: suppressing legitimate unused-capability warnings.

## EXP-003 — Explain field-level interior-mutability failures in field terms

- Status: implemented locally; verified by comparison diagnostics and pending
  ordinary release review
- Evidence: `expressiveness/02_field_interior_mutability` correctly rejects
  `shared.ordinary = 10` with `E04572`, `E04573`, and `E0443`.  The combined
  messages are technically correct but do not state that `reads#` is local to
  that field and does not grant a sibling capability.
- Improvement: preserve the stable codes, but add a primary or follow-up note:
  "`ordinary` is not declared with payload-side `#`; a shared aggregate view
  cannot write it."  This directly teaches the intended 1.0 model.  The first
  implementation keeps the frozen diagnostic-code sequence and adds the
  authority explanation as a note; reducing cascades requires a separately
  audited diagnostic compatibility change.
- Non-goal: allowing writes to ordinary sibling fields or weakening PAL.

## What this does not establish

These cases do not compare performance, compilation speed, ecosystem scale,
unsafe escape hatches, thread safety, or the full expressive power of either
language.  Those require separate questions and separate experiments.

## EXP-004 — Consider explicitly bound associated types on dyn objects

- Status: evidence only; no RFC yet
- Evidence: `expressiveness/07_dyn_associated_type_object` shows that Rust can
  erase `dyn Readable<Item = i32>`, while Toka 1.0 rejects the corresponding
  `dyn @Readable<Item = i32>` form with `E0617`.
- Improvement question: whether a post-1.0 dyn ABI can encode an explicitly
  bound associated type without weakening existing object-safety, visibility,
  or source-less `.tki` contracts.
- Non-goal: treating this as a PAL limitation or adding syntax before an ABI
  and object-safety design exists.

## SEM-001 — Investigate a dynamic-borrowing container only on evidence

- Status: exploratory; not a current gap
- Context: `field#` is static field-level authority, while `Mutex` and
  `RwMutex` already provide synchronization-oriented runtime policies.
- Question: whether single-thread runtime borrow checking offers a use case
  that is clearer than the existing mechanisms.
- Next step: use the contract in
  [`dynamic_borrowing_exploration.md`](../../docs/semantic_core/dynamic_borrowing_exploration.md)
  to build an API sketch and tests before deciding whether an RFC is justified.

## EXP-005 — Resolve the static async-trait contract mismatch

- Status: audit required; not an implementation gap claim
- Evidence: `expressiveness/08_async_trait_protocol` compiles and runs a
  concrete Toka trait method returning `async i32`, but rejects the generic
  borrowed-receiver await with `E04583`. `docs/1_0_scope.md` still lists async
  traits as post-1.0.
- Decision: either qualify/freeze the accepted static subset with an
  async-interface RFC, or reject it until receiver lifetimes, cancellation,
  cross-module/TKI replay, visibility, and dyn interaction are specified.
- Non-goal: treating the Rust `async fn` keyword spelling as the capability.

## EXP-006 — Evaluate fallible entry and universal error erasure separately

- Status: evidence only; no RFC yet
- Evidence: `expressiveness/09_error_entry_and_erasure` demonstrates Rust's
  `main -> Result` and `Box<dyn Error>` against Toka's typed helper-result
  baseline and `E04596` entry rejection.
- Non-goal: weakening one-step typed `@ErrorInto` conversion or presenting
  `?`/`!` punctuation as the design question.

## ERG-001 / ERG-002 — Keep conservative surface differences small and honest

- Evidence: `ergonomics/01_non_enum_exhaustiveness` and
  `ergonomics/02_partial_result_propagation` lock Toka's wildcard and
  whole-binding propagation requirements against runnable Rust forms.
- Status: no RFC.  Revisit only if real Toka programs show material friction;
  both relaxations require nontrivial value-domain or partial-move evidence.
