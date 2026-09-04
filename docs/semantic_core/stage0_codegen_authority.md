# Stage 0 CodeGen Fail-Closed Authority

**Status:** audit-only implementation slice. It does not change source-language
diagnostics or activate Stage 1 caller-spelling behavior.

**Frozen inputs:**

- call transactions: `fea743ba2379260187eec64e48d2a1cd341ade0e`;
- non-call destinations: `34e9430de86d0a3af09baa42acb51f2d3b748394`;
- generic-body qualification: `a57fb373b8474f984737facaa2f546e83ef92e0b`.

The internal qualification command is:

```text
tokac --stage0-codegen-authority source.tk -o artifact
```

It requires artifact emission and cannot be combined with check-only, JSON,
Evidence, or other audit modes.

## Authority carrier

Sema attaches `Stage0CodeGenAuthority` to the exact AST site and source edge
that CodeGen later lowers. The carrier records:

- call transaction, non-call item, or non-call group kind;
- required-authority bit independent of the optional plan payload;
- Sema validation and transaction/group completion;
- destination matching and snapshot revision;
- frozen call route or non-call destination; and
- the immutable item, whole-call, or group plans.

Selected generic instances also carry their stable specialization identity and
`Stage0BodyQualificationComplete`. Candidate-prepared but unpromoted instances
cannot satisfy this gate.

## CodeGen rule

CodeGen validates the carrier before lowering the corresponding edge. It
rejects and emits no artifact when any required carrier is:

- missing;
- rejected by Sema;
- incomplete;
- destination/route mismatched;
- transaction/group incomplete; or
- attached to an unqualified generic specialization.

Carrier presence and carrier requirement are separate. If a carrier exists,
CodeGen always validates it, including bare expressions whose frozen plan is
rejected. `RequiresAuthority` only decides whether an absent carrier is an
error; it can never bypass an attached `SemaValidated=false` plan.

The gate reads the frozen Sema plan only. It does not call type resolution,
PAL, ownership classification, Drop analysis, source-place discovery, or
obligation planning. After admission, the existing legacy lowering still
produces IR; switching lowering behavior belongs to Stage 1 and later cleanup.

Generic, overload, indirect `fn`/`dyn fn`, and dynamic-trait combinations that
the frozen Stage 0 model cannot prove remain rejected. This is intentional
fail-closed behavior, not a reason to infer authority in CodeGen.

## Qualification

`tools/scripts/test_stage0_codegen_authority.py` covers all eight call routes,
all seven non-call destinations, and generic-body qualification. It performs
missing-plan and mismatch injection for each of the 16 boundaries and verifies
that no object or executable is produced. It also proves:

- admitted non-call, direct-generic, and extern call edges produce runnable or
  linkable artifacts;
- all admitted non-call destinations preserve runtime behavior;
- Sema-rejected payload, indirect, dynamic-trait, and overload plans cannot
  produce artifacts;
- imported-overload initialization and array-aggregate bare rejected carriers
  fail without fault injection while normal compilation still emits objects;
- selected direct generic bodies require completed qualification; and
- normal compilation output and diagnostics remain unchanged.

Fault injection is available only in testing builds through
`--stage0-codegen-fault=missing:<boundary>` and
`--stage0-codegen-fault=mismatch:<boundary>`.

This slice does not modify Parser syntax, TKI, ABI, the compiler-interface key,
or legacy transfer lowering.
