# Telemetry pipeline tooling pilot

This deterministic reference project exercises a long import chain, public
shapes, documented functions, symbol indexing, and incremental analysis. Its
checked-in `src` tree contains more than 5,000 lines across 21 Toka modules.

Regenerate it with:

```sh
python3 tests/tooling/pilot_project/generate.py
```

`tools/scripts/test_tooling_scale.py` copies the project to a temporary
workspace, performs a clean compiler check, and drives `tokalsp` through a
100-edit fixed-seed soak. The generated source is deliberately regular so a
performance change is comparable across revisions, but every declaration is
valid and participates in a telemetry scoring domain rather than being filler.
