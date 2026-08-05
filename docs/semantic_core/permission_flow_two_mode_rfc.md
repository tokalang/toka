# RFC: Two-Mode Permission Flow

**Status:** Bounded design contract frozen. The P-1 baseline is qualified at
`8d680fea`; current-HEAD evidence narrows the bounded
partial-`cede` matrix but does not promote the historical audit's `Partial`
rows or close the PlaceState Core. Any unlisted or unqualified generalization
must fail closed.

**Rule IDs:** `PERM-STATIC-01`, `OWN-FLOW-01`, `OWN-FLOW-02`

## 1. Purpose and boundary

Toka separates two permissions on an indirection:

- **Handle permission (H):** may this binding rebind its handle identity?
- **Payload permission (P):** may this binding write the referent payload?

The already-frozen static rule is:

> A use-site marker is an operation request, never a source of authority.
> Authority comes from the declaration of the binding, parameter, field, or
> callable receiver.

This RFC specifies only the next layer: what happens when a value flows into a
**fresh** binding. It does not weaken the static rule, and it does not make
`cede` an unrestricted privilege-escalation mechanism.  Its authority rules
have implementation evidence for bounded subsets documented here; the
qualification status of each surface remains explicit rather than implied by
the syntax.

The effective permission for an operation remains:

```text
declaration authority ∩ flow ceiling ∩ use-site request ∩ PAL permission
```

Fields are declarations for this purpose. Assignment to an existing binding
never changes that binding's declared H/P authority.

### Iron rule: shared views never amplify payload authority

> A shared view may preserve or restrict the Payload permission supplied by
> its source, but it may never amplify it.

This applies uniformly to aliases, shared handles, references, raw-pointer
views in safe code, parameters, returns, receiver views, field projections,
and pattern bindings. A use-site `#` is still only an intent request.

An explicit whole-object unique transfer is not an exception to this rule: it
consumes the old owner and creates a **fresh owner**, not a shared view. Toka
has two visible spellings for that transfer: `cede ^p` and a direct hatted
unique move such as `auto ^q = ^p`. That fresh owner may derive P from its own
declaration, subject to the referent ceiling.

## 2. Terms

- **Fresh binding:** a newly introduced local, parameter, field initializer,
  or pattern binder. Its declaration fixes its local H/P authority.
- **Existing binding:** an already introduced storage location. A later
  assignment supplies a value but cannot redefine its H/P authority.
- **Referent ceiling:** a compiler-represented property of the object/path being
  observed. The currently evidenced example is `$` field/payload immutability;
  any future freeze/sealed carrier requires its own implementation and replay
  qualification. An established ceiling survives transfer.
- **Capability:** permission supplied by a declaration plus its flow ceiling.
- **Intent:** the operation requested by source syntax, including `#`.
- **PAL permission:** the path-anchored ledger's current alias/borrow result.

## 3. Flow classification

Every transfer must be classified before permissions are derived.

| Mode | Sources | Meaning |
|---|---|---|
| **Independent** | owned value or `^` unique ownership, transferred as a whole by `cede` or a direct hatted unique move | The source is invalidated and the fresh binding becomes a new ownership root. |
| **Shared** | `~`, `&`, and ordinary non-moving alias propagation | The fresh binding is another view of an existing referent. |
| **Unsafe raw boundary** | `*` | Safe code treats it as non-upgrading shared observation. Any payload capability requires an explicit `unsafe` boundary. |

Reference lifetime is not a third permission mode. It is an additional
well-formedness condition on Shared flow.

## 4. Normative rules

### OWN-FLOW-01: Independent flow

For a whole-binding explicit unique transfer—either `cede` from an owned value
or `^` unique source, or a direct hatted unique move—from an owned value or
`^` unique source:

1. The exact source path must be non-null, not moved, and PAL-invalidatable.
2. If the destination is an existing place, Sema must compare
   `CanonicalPlace(source)` and `CanonicalPlace(destination)` before any
   lifecycle effect. Only a proven `Disjoint` relation is admitted. `Equal`,
   either ancestor/descendant relation, and an unknown relation reject before
   retirement, capture, invalidation, or cleanup-mask change. This comparison
   concerns storage places/handle slots, not whether distinct handles happen
   to denote the same referent. In particular, `^x = cede ^x` and `^x = ^x`
   are rejected rather than defined as no-ops.
3. The source is invalidated exactly once when the transfer succeeds.
4. A fresh LHS obtains H from its own declaration.
5. A fresh LHS obtains P from its own declaration, capped by the transferred
   referent ceiling. The old binding's local P marker does not permanently
   reduce a newly owned root.
6. An existing LHS retains its declared H/P; `cede` supplies a value, not a
   redeclaration. Replacing a live resource composes with PlaceState cleanup:
   canonical disjointness and all fallible preparation are proved first, then
   old-destination retirement, destination capture, source invalidation, and
   cleanup-obligation transfer form one non-suspending semantic commit.
   Failure before commit leaves both places and their cleanup ownership
   unchanged.

A fresh local initializer, fresh pattern binder, callee `cede` formal, and
return root introduce distinct destination storage by construction and have no
old-destination retirement. NRVO, `sret`, or another physical storage elision
is valid only when lowering preserves that logical fresh-transfer/disarm
contract; an optimization cannot expose physical aliasing as permission for a
source/destination overlap.

Thus the following is valid when `Data` is not frozen and `p` is a whole
unique owner:

```toka
auto ^p = new Data(...)
auto ^#q# = cede ^p

// Equivalent direct hatted unique move:
auto ^#r# = ^q
```

It is not valid if the object carries an implemented `$` payload ceiling that
forbids writes. `cede` transfers ownership; it does not erase a represented
referent restriction. This sentence does not claim a general freeze/sealed
carrier.

### OWN-FLOW-02: Shared flow

For `~`, `&`, and non-moving alias propagation:

1. The LHS H remains a property of the new handle slot and comes from its
   declaration.
2. The LHS P is capped by both its declaration and the **direct RHS** payload
   capability:

   ```text
   effective-P(LHS) = declared-P(LHS) ∩ effective-P(direct RHS)
   ```

   The direct RHS already carries any ceiling established by an earlier hop;
   no implementation may walk back to the allocation or other initial source.
   A readonly/shared source cannot become payload-writable merely because the
   destination spells `#`.
3. `cede` is not a payload-upgrade operator for Shared sources. If syntax
   permits moving such a handle, it may transfer only the handle ownership
   described by its type; it does not increase referent capability. Any such
   source-invalidating move into an existing destination also inherits
   `OWN-FLOW-01`'s canonical-place `Disjoint` precondition and atomic
   replacement order. Thus an admitted `~x = cede ~x` form is rejected before
   handle release or place-state change rather than treated as self-rebind.
4. References additionally require the existing lifetime/escape rules.
5. Raw pointers follow this rule in safe code; an explicit `unsafe` operation
   is required before raw payload authority can be used.

## 5. Nullability and guards

Independent flow from a nullable source to a non-null destination requires a
dominating guard over the **same canonical path**. A guard refines the current
null-state only; it neither adds H/P authority nor removes a referent ceiling.

The implemented projection form is branch-local and direction-sensitive:

```toka
if record.item is null {
    // record.item remains nullable here
} else {
    auto item = cede record.item:Item // same path, proven non-null
}
```

The same rule applies to a fixed-array index such as `values[0]`. A `guard`
may express the same proof for an exact direct local member or fixed-array
constant index:

```toka
guard record.item {
    auto item = cede record.item:Item
} else {
    return
}
```

This remains a branch-local proof. It is not propagated to siblings, a
different path, a dynamic index, a caller-supplied root, or beyond the guard.
No implicit `cede` conversion from nullable to non-null is permitted without
that proof.

## 6. Patterns and partial moves

Pattern binding must use the same classifier and derivation rules as local
initialization. No owned move-pattern is in this bounded surface. Any currently
admitted pattern/reference binder is conservative Shared/borrow flow and must
not re-root payload authority; pattern forms without current-revision evidence
remain qualification-pending and fail closed if they would need ownership
transfer.

Whole-binding `cede` is the general Independent form. Partial projections are
always Shared for authority, but have a separate, deliberately bounded
lifecycle implementation: a direct named field of an eligible local
compiler-managed record and a constant index of an eligible local fixed array
may be ceded using exact per-projection liveness and drop masks. Other member,
index, destructuring, spread, enum, or custom-drop forms remain rejected in
ordinary safe source. A resolver-owned intrinsic may enforce its own
representation invariant, and an explicit `unsafe` implementation may assume
one inside its audited boundary; neither exception grants the form to safe
source. The lifecycle contract and its evidence are normative in
`partial_cede_lifecycle_rfc.md`; none of these bounded partial forms re-root
Payload authority.

## 7. Call and return boundaries

Parameter and return signatures are declarations. A call-site marker requests
an operation permitted by that signature; it cannot clean a weaker incoming
capability. A `cede` parameter can accept an Independent transfer only when
its source and PAL state satisfy `OWN-FLOW-01`; shared/ref/raw arguments keep
the ceilings of `OWN-FLOW-02`.

## 8. Bounded capability matrix and qualification evidence

The following matrix is the complete normative surface of this RFC. A syntax
that can name a broader transfer does not make that transfer part of the
contract.

| Flow form | Authority classification | Contract status |
|---|---|---|
| whole owned/unique transfer into a fresh binding, parameter, return, or whole owned consuming receiver | Independent; destination declaration supplies local H/P subject to retained represented ceilings | design-frozen; current-revision qualification required per surface |
| `~`, `&`, and ordinary alias propagation through currently admitted locals, calls, returns, fields, closures, and conservative reference/pattern binders | Shared; direct source supplies the non-amplifying payload ceiling | design-frozen; historical `Partial` surfaces remain qualification-pending |
| source-invalidating transfer into an existing binding | destination keeps its declaration; only a canonically proven disjoint source/destination pair may enter the atomic PlaceState/cleanup commit in `OWN-FLOW-01`, including an admitted moving Shared-handle form | design-frozen; current-revision cleanup and overlap-rejection evidence required |
| direct nullable whole/member or fixed-array constant-index transfer after a same-path guard | guard refines presence only; it grants no H/P | bounded frozen surface |
| direct-field or fixed-array constant-index partial `cede`, including the direct-field consuming-receiver subset | Shared for authority; lifecycle governed separately by `partial_cede_lifecycle_rfc.md` | bounded surface only where that lifecycle RFC admits and qualifies the exact place |
| safe raw observation | Shared/non-upgrading | frozen conservative boundary |
| dynamic/container index, spread, enum payload, arbitrary nested/nonlocal consuming receiver, custom-drop projection | no general classification or lifecycle authority | safe source rejects; resolver-owned intrinsic or explicit `unsafe` boundary only |
| unsafe raw authority | outside this safe-flow contract | explicit unsafe boundary required |

Promotion of an additional row requires all of:

- negative tests for every shared-source attempt to obtain P through each new
  binding, argument, return, receiver, field, or pattern surface;
- positive and negative whole-transfer tests, including source invalidation
  and retained referent ceilings;
- canonical-place overlap tests rejecting exact self-transfer, both
  ancestor/descendant directions, and an unprovable relation before any drop
  or state change; source-backed and source-less consumers must agree on the
  same rule and diagnostic, while a proven distinct-root control remains
  accepted;
- nullable guarded and unguarded tests over canonical paths where applicable;
- `.tki` replay preserving every flow-relevant signature and declaration fact;
- PAL conflict tests proving transfer cannot bypass an active overlap;
- `if`, `guard`, `match`, and loop joins preserving direct-flow ceilings from
  every continuing predecessor without branch-order dependence; and
- diagnostics that distinguish declaration authority, flow ceiling, null
  proof, PAL conflict, and unsupported lifecycle eligibility.

TKI carries no ambient flow authority. H/P declarations, represented referent
ceilings, field graphs, structural Copy/drop eligibility, and the canonical
field identities used by source/destination disjointness are parsed,
recomputed, and compared by the importer. The consumer always performs the
call-site/place overlap check; an interface cannot assert that two consumer
places are disjoint. A consuming callee's body discharge or async cleanup is
body-derived and requires source/retained-body recheck or a separately
accepted object-bound attestation. Audit comments, standalone TKI labels, and
ordinary package metadata cannot promote either class.

Before the Semantic Manifest payload is qualified, this RFC can close only its
Level-A provider profile: declaration and call-site facts are recomputed from
source or TKI, while consuming-body fulfilment comes from source or a retained
canonical body that the consumer rechecks, lowers, and links as the object from
that same compile action. A provider-supplied object is not covered. Traditional
bodyless `TKI + object` fulfilment is Level B and requires the later accepted-
provenance, exact-object-bound attestation. Historical bodyless execution or
replay runners are recorded ABI/replay evidence only; they do not establish
that fulfilment trust.

Historical audit evidence is recorded in
`permission_flow_two_mode_audit.md`. It does not replace the roadmap's
current-HEAD requalification gate.

## 9. Non-goals

This RFC does not add implicit partial moves, arbitrary raw-pointer upgrades,
or a general capability cast. It also does not redefine the already-frozen
static declaration/signature authority rule.
