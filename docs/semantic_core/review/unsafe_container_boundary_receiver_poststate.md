# Receiver poststate dependencies: implementation closeout

**Historical mechanism checkpoint.** The subsequent production Vec candidate
and its separate verification are recorded in
`unsafe_container_boundary_production_integration.md`.

This change is isolated on `design/unsafe-container-boundary`, based on
`a1fdc191b4be453654f458d465dd9f3fb1735696`. It does not change the
production `raw_take` check, native ABI, the release candidate, or any release
asset. The archived `pop_remove.patch` remains a diagnostic overlay applied
only to a temporary copy of `lib/std/vec.tk` by the test driver. No escaping
fixture is executed.

## First loss and repair

The binding-ID trace in `unsafe_container_boundary_first_loss.md` locates the
first missing persistent fact after `values#.push(owner.as_str())`: the
argument still carried the actual owner source, but the receiver binding did
not inherit it. The return check later saw an empty dependency set. The
compiler now accepts an explicit receiver postcondition:

```toka
pub fn push(self#, cede val: T)
effects:
    self <- val
```

After a valid call, the receiver retains its old external sources and joins
the argument's actual external sources. Sources are resolved through binding
IDs, not current spellings. Unknown remains unknown. Rejected calls restore
dependency and transfer state; the effect grants no write permission and does
not bypass PAL or `cede` checking. Branch joins conservatively union sources.
The declaration checker requires a mutable `self#`, real formal sources and
resolved projections, and checks writes into receiver fields against declared
source paths. A contract naming a different existing field cannot justify the
write.

The compiler distinguishes an empty source set (an independently usable
value) from an unknown set. It recognizes the absence of external **safe
borrows** structurally through concrete shapes, enum payloads, arrays and
generic arguments. A raw pointer is not treated as a safe borrow, but this is
not proof that the pointed-to storage is owned or valid. Shape and enum value
flows carry actual sources into `push`; an aggregate type name is not a
whitelist.

## Extraction and interface boundary

The `return <- self.external` route on `remove`, `take`, consuming iterator
`next`, and `Option::unwrap` transfers the receiver's external sources to the
result without making the result depend mechanically on the retired container
slot. The diagnostic overlay adds the same route to `pop`; its production
`raw_take` body is untouched. A separate storage borrow still depends on the
live Vec allocation. Existing `Vec<&T>::appended` return contracts continue
to preserve their legal reference source.

TKI exports receiver and external-result routes as symbolic formal paths,
never as a compilation's binding IDs. Import maps those paths to the current
call's bindings. Interface format 5 and compiler interface version `0.9.9-25`
reject old caches; the `replay_surface_hash` covers the exact declaration
surface so a changed source-hidden effect cannot silently replay. The native
ABI is unchanged.

## Incremental verification

With the new compiler and the explicitly marked temporary Vec overlay,
`test_unsafe_container_boundary_probe.py --mode closed` passed all 16 escape
rejections in normal and shadow analysis. Each was rejected at E0455, with no
object or LLVM IR. This includes whole-Vec return, `pop`, `remove`, owned
iteration, shape and enum payloads, `unwrap`, shadowed bindings, aliases,
branch joins, `insert`, `set`, `clear`, `take`, and `resize`. Six runtime
controls passed: local view while owner lives, owned string and token return,
exact-once cleanup, extracted view after Vec destruction while owner lives,
owned pop after Vec destruction, and shadowed parameter use. The existing
reference growth path passed; reference escape, element storage escape,
active-borrow modification, and rejected-call rollback remained rejected.

`test_unsafe_container_boundary_probe.py --mode diagnose` verifies that the
production `raw_take` guard still rejects the four archived cases and that a
temporary copy of the old unannotated Vec now rejects an undeclared receiver
write. The original pre-fix admission trace remains in the diagnostic
checkpoint. `test_receiver_poststate_tki.py` passed source-hidden import and
local-owner rejection, absent/misdirected effect rejection, altered interface
surface rejection, and old interface version rejection, again with no output
artifact on rejected cases.

The following adjacent gates passed with the same compiler: Vec reference
domains, binding value dependencies, stage-1 Vec pop (20/20), call transfer
shadow (53 cases), stage-1 return matrix, pure nominal overload, TKI cache
validation, TKI unsafe revalidation, and TKI excluded-syntax revalidation.
`test_encap_slice5_tki_audit.py` remains red at an unrelated duplicate closure
E04658; the same failure occurs with the unchanged 0.10 baseline compiler.

## Remaining boundary

This closes the diagnosed safe-interface dependency escape. It does not make
production nested Vec available: `raw_take` retains its conservative guard.
Container allocation, initialized-slot accounting, retirement and raw memory
safety remain the unsafe library implementation's responsibility. External
sources are conservative and may remain after `remove` or `clear`; precise
subtraction is outside this package. This change adds neither generic
constraints nor a general parameter poststate system.
