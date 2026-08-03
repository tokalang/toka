# TKI Replay Semantic Tests

These tests verify that source and source-less `.tki` paths preserve the same
Toka 1.0 semantic facts. Each case first compiles every consumer against the
provider source, then hides the provider `.tk` and repeats the same decisions
against only the generated interface plus object file. A case marked
`SOURCELESS_RECHECKED_BODY` retains and recompiles its provider body instead;
this is the Level-A path for Outcome Contracts and does not link the provider
object.

Case layout:

```text
cases/<rule-id>_<name>/
  lib.tk
  pass_*.tk
  fail_*.tk
```

`fail_*.tk` files should contain one or more `EXPECT_ERROR: E....` comments.
The runner checks that compilation fails and that each expected diagnostic code
appears in the compiler output.

Provider files may contain `EXPECT_TKI: text` comments. The runner requires
each fixed-string fragment to appear in the generated interface. Use these for
semantic markers whose downstream effect does not yet have a discriminating
consumer test.

Run with:

```bash
tools/scripts/test_semantic_replay.sh
```
