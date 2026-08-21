# Init & Projection Fact Tracking Requirements

**Status:** Draft exploration; not an admitted Toka 1.0 language commitment.

---

## 1. Frozen Toka 1.0 P1 Initialization Invariants

Toka 1.0 P1 is strictly whole-place and synchronous. The declared language
boundary mandates:

```text
Never whole place
  ├─ init whole = complete value       allowed
  ├─ pass to explicit init formal      allowed
  ├─ Outcome Contract handoff          allowed
  ├─ ordinary borrow                   rejected (E04617 / E0460)
  ├─ ordinary member/index access      rejected (E04617 / E0460)
  └─ ordinary member/index assignment  rejected (E04617)
```

- **No Field-Wise Delayed Initialization**: Field-wise delayed init
  (`init x.field = ...`) and dirty-reference partial construction remain
  **Deferred**.
- **No InitAuthority via Ordinary References**: Ordinary `&` / `&#` borrows
  never carry `InitAuthority`. Borrowing a `Never` or incompletely initialized
  place is statically rejected.
- **Whole-Place Construction Mandatory**: Custom-drop shapes, shared-member
  aggregates, and arrays must be constructed as a complete whole value via
  `init whole = Shape(...)` or literal constructors.

---

## 2. Deferred Capabilities (Future RFC Scope)

The following architectural designs are purely exploratory and remain deferred
outside the Toka 1.0 boundary:

### 2.1 Deferred: `InitializationProjectionPlan`
- In Toka 1.0, only `PartialMovePlan` exists for admitted destructible structs.
- An independent `InitializationProjectionPlan` to enable field-by-field delayed
  construction is a future proposal.

### 2.2 Deferred: Structured Referent Initialization (`ReferentAuthority`)
- In Toka 1.0, references cannot borrow uninitialized places.
- Passing `InitAuthority` through reference wrappers remains deferred.

### 2.3 Deferred: Dynamic Fact Vectors for >64 Fields
- Handled via whole-place construction only in Toka 1.0.

---

## 3. P0 Remediation Directive

Any path where an ordinary reference or projection write on a `Never` place
modifies legacy `InitMask` or admits an uninitialized sibling read is an
unauthorized bypass of the P1 contract and must be sealed fail-closed.
