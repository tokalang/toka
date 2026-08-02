# Current `@Encap` Core Contract

**Status:** Normative summary of the implemented clean-break `@Encap`
surface. The public grammar is defined by [`../syntax.md`](../syntax.md) and
[`../syntax_zh.md`](../syntax_zh.md); this document states the corresponding
semantic boundary and points to its evidence.

**Scope:** Access governance, Copy/Dup, lifecycle separation, and their
source-less TKI facts. This is intentionally a current contract, not a record
of the removed epoch designs.

## 1. Policy is separate from ownership and lifecycle

A shape with no `impl Type@Encap` policy is transparent: its accessible fields
follow ordinary type visibility, morphology, and PAL authority. A shape with
one valid `impl Type@Encap` policy is a governed capsule: its fields are closed
outside the defining module except for exact `pub field` grants in that policy.

`@Encap` proves only that this explicit access policy exists. It does not prove
Copy, explicit duplication, resource ownership, or a particular drop plan.
Those facts are checked independently so no subsystem can manufacture one from
another.

```toka
impl Device@Encap {
    pub public_config, shared_state

    fn drop(self#) {}
}
```

The policy grammar has no wildcard or scoped grants: `pub *`, `pub(crate)`,
and `pub(path)` are rejected. A future field is private by default until its
exact name is granted.

## 2. Copy and explicit duplication

`@Copy` is a compiler-verified, empty marker. Implicit copying is available
only when the complete field graph proves Copy-safe; an `@Encap` policy alone
does not affect that proof.

`@Dup` is the sole explicit resource-duplication capability:

```toka
impl Device@Dup {
    pub fn dup(self) -> Self { ... }
}
```

It is never selected by assignment, construction, iterator lowering, or an
ordinary closure capture. `[dup value]` is the explicit closure-capture form
and invokes a validated provider exactly once. An ordinary method named
`clone` is only an ordinary method; it supplies no Copy, Dup, policy, or
lifecycle evidence.

## 3. Lifecycle is compiler-owned

An `@Encap` policy may contain one private hook, `fn drop(self#)`. The hook is
not an ordinary callable method. After it returns, compiler-owned structural
cleanup handles the remaining field tail exactly once. Ordinary methods belong
in an ordinary `impl`, not in the policy block.

This makes authored access policy, explicit user cleanup, and structural field
cleanup distinct facts. No old `@Clone`, `@Drop`, `clone = delete`, structural
`@Encap` witness, or callable-drop compatibility surface remains.

## 4. Interface and resolver agreement

TKI v2 transports the field graph, normalized policy grants, Copy proof and
witness, generic Copy recipe, Dup provider, custom-drop ABI, and resolver
identity. Structural cleanup is recomputed from the transported field graph;
it is not a replay marker. Source and source-less interface compilation must
therefore make the same policy, Copy/Dup, and lifecycle decisions.

## 5. Evidence and history

- [`encap_slice6_library_audit.md`](encap_slice6_library_audit.md) is the
  migration and current-language audit.
- [`encap_slice4_copy_audit.md`](encap_slice4_copy_audit.md) and
  [`encap_slice5_tki_audit.md`](encap_slice5_tki_audit.md) record the Copy/Dup
  and source-less interface evidence.
- [`encap_hybrid_policy_rfc.md`](encap_hybrid_policy_rfc.md) is archived
  history only. It describes removed scoped/wildcard policy forms and must not
  be used to decide current syntax or implementation behaviour.
