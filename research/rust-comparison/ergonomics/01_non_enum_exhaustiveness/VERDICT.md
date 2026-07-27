# Verdict: non-enum value-domain exhaustiveness

Rust proves that the two `bool` values cover a match.  Toka 1.0 requires a
wildcard/default or unconditional variable arm for every non-enum target, so
the two-arm bool form is rejected with `E0553`; the default-arm baseline runs.

This is a **conservative surface/ergonomics difference**.  Toka deliberately
does not implement full literal, range, integer, or string value-domain
reasoning in its 1.0 match checker.  The case does not establish a PAL or
ownership limitation and does not justify a language extension without real
evidence that this conservative rule causes material friction.
