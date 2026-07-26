# RFC: Two-Mode Permission Flow

**Status:** Proposed semantic-core rule; not a statement of current compiler
behaviour.

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
`cede` an unrestricted privilege-escalation mechanism.

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

Independent whole-object `cede` is not an exception to this rule: it consumes
the old owner and creates a **fresh owner**, not a shared view. That fresh
owner may derive P from its own declaration, subject to the referent ceiling.

## 2. Terms

- **Fresh binding:** a newly introduced local, parameter, field initializer,
  or pattern binder. Its declaration fixes its local H/P authority.
- **Existing binding:** an already introduced storage location. A later
  assignment supplies a value but cannot redefine its H/P authority.
- **Referent ceiling:** a property of the object/path being observed, such as
  `$`/freeze/sealed payload immutability. It survives transfer.
- **Capability:** permission supplied by a declaration plus its flow ceiling.
- **Intent:** the operation requested by source syntax, including `#`.
- **PAL permission:** the path-anchored ledger's current alias/borrow result.

## 3. Flow classification

Every transfer must be classified before permissions are derived.

| Mode | Sources | Meaning |
|---|---|---|
| **Independent** | owned value or `^` unique ownership, transferred as a whole with `cede` | The source is invalidated and the fresh binding becomes a new ownership root. |
| **Shared** | `~`, `&`, and ordinary non-moving alias propagation | The fresh binding is another view of an existing referent. |
| **Unsafe raw boundary** | `*` | Safe code treats it as non-upgrading shared observation. Any payload capability requires an explicit `unsafe` boundary. |

Reference lifetime is not a third permission mode. It is an additional
well-formedness condition on Shared flow.

## 4. Normative rules

### OWN-FLOW-01: Independent flow

For a whole-binding `cede` from an owned value or `^` unique source:

1. The exact source path must be non-null, not moved, and PAL-invalidatable.
2. The source is invalidated exactly once when the transfer succeeds.
3. A fresh LHS obtains H from its own declaration.
4. A fresh LHS obtains P from its own declaration, capped by the transferred
   referent ceiling. The old binding's local P marker does not permanently
   reduce a newly owned root.
5. An existing LHS retains its declared H/P; `cede` supplies a value, not a
   redeclaration.

Thus the following is valid when `Data` is not frozen and `p` is a whole
unique owner:

```toka
auto ^p = new Data(...)
auto ^#q# = cede ^p
```

It is not valid if the object carries a `$`/freeze/sealed ceiling that forbids
payload writes. `cede` transfers ownership; it does not erase referent
restrictions.

### OWN-FLOW-02: Shared flow

For `~`, `&`, and non-moving alias propagation:

1. The LHS H remains a property of the new handle slot and comes from its
   declaration.
2. The LHS P is capped by both its declaration and the incoming referent P.
   A readonly/shared source cannot become payload-writable merely because the
   destination spells `#`.
3. `cede` is not a payload-upgrade operator for Shared sources. If syntax
   permits moving such a handle, it may transfer only the handle ownership
   described by its type; it does not increase referent capability.
4. References additionally require the existing lifetime/escape rules.
5. Raw pointers follow this rule in safe code; an explicit `unsafe` operation
   is required before raw payload authority can be used.

## 5. Nullability and guards

Independent flow from a nullable source to a non-null destination requires a
dominating guard over the **same canonical path**. A guard refines the current
null-state only; it neither adds H/P authority nor removes a referent ceiling.

No implicit `cede` conversion from nullable to non-null is permitted without
that proof. The initial implementation may conservatively reject all such
transfers until path-sensitive guard evidence is available.

## 6. Patterns and partial moves

Pattern binding must use the same classifier and derivation rules as local
initialization. Until explicit owned move-pattern semantics and per-field move
state are implemented, patterns are conservative Shared/borrow bindings: they
must not re-root payload authority.

The first implementation supports only whole-binding independent `cede`.
Partial moves from members, indexes, or destructuring remain a conservative
rejection. This avoids inventing incomplete-object drop semantics as an
incidental part of permission flow.

## 7. Call and return boundaries

Parameter and return signatures are declarations. A call-site marker requests
an operation permitted by that signature; it cannot clean a weaker incoming
capability. A `cede` parameter can accept an Independent transfer only when
its source and PAL state satisfy `OWN-FLOW-01`; shared/ref/raw arguments keep
the ceilings of `OWN-FLOW-02`.

## 8. Required conformance evidence

Before this RFC can be promoted from Proposed:

- negative tests for every shared-source attempt to obtain P through a fresh
  binding, argument, return, method receiver, field, or pattern;
- positive and negative whole-`^` `cede` tests, including source invalidation
  and frozen referent ceilings;
- nullable guarded and unguarded transfer tests over canonical paths;
- `.tki` replay preserving flow-relevant signature facts;
- PAL conflict tests proving transfer does not bypass active borrows; and
- diagnostics that identify whether rejection came from declaration authority,
  flow ceiling, null proof, or PAL.

## 9. Non-goals

This RFC does not add implicit partial moves, arbitrary raw-pointer upgrades,
or a general capability cast. It also does not redefine the already-frozen
static declaration/signature authority rule.
