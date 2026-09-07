# Stage 1 remaining scope

Stage 1 is **not complete**. Accepted parameter routes are not acceptance of
the entire parameter matrix or the full explicit-cede RFC.

| Area | Current status |
| --- | --- |
| Ordinary parameter slice | Accepted at `01c02392`; covers its reviewed direct/generic-direct/static/extern domain, not all generic/morphic contracts. |
| Concrete instance-method / dynamic-trait / indirect parameters | Accepted at `7bad2cb0`, `014e4d82`, and `dc013ac5`, within their concrete provenance boundaries. |
| Generic/morphic parameters | Not closed across routes. Method, trait, indirect and substitution/provenance gaps require separate activation and qualification. No expansion in standalone work. |
| Identity parameters (borrow/reference/raw/callable contracts) | No unified Stage 1 acceptance. Existing identity behavior and frozen exclusions remain; do not infer owning-transfer authority from Copy or erasure. |
| `@Callable` parameter routes | Separate remaining activation/qualification; preservation of consuming invocation is not acceptance of this parameter route. |
| Return/source behavior | Accepted and frozen at `ba18bc14`; do not append features. |
| Standalone `cede` | Accepted; frozen by the commit containing the standalone acceptance record. |
| Initialization / whole-binding assignment | Authorized next shared-implementation slice: source invalidation, old-target cleanup, overlap rejection, failure rollback, and bare temporary transfer. Not yet implemented or Accepted. |
| Aggregate | Stage 0 plans exist; Stage 1 activation remains a separate pending slice. |
| Match binding / closure capture | Existing semantics and Stage 0 audit coverage remain; standalone work does not activate their complete Stage 1 matrix. |

Receiver spelling/morphology activation and general `InvokeExpr` belong to
Stage 2, not a shortcut around these remaining Stage 1 items. Protocol cleanup,
obsolete-path deletion, TKI/interface-key changes and final release
qualification remain later work. Agree each next slice's exact boundary and
gate before implementing it.
