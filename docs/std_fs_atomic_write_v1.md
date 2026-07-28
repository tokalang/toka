# `std/fs::write_string_atomic` v1

`write_string_atomic(path, content)` replaces a target through a uniquely
created sibling temporary file. On the current Tier 1 POSIX targets (macOS and
Linux), a successful call uses same-filesystem `rename`, so readers observe
either the previous target or the complete replacement, never a partially
written target. When replacing an existing regular file, its POSIX permission
bits are preserved; a newly created target receives the secure temporary-file
default permissions.

This is deliberately an **atomic-visibility** contract, not a durable commit
protocol: it does not promise survival of sudden power loss because neither the
file nor containing directory is synced. A future durability API must expose
its stronger cost and failure semantics explicitly.

The API returns `Err` where the platform adapter is unavailable (currently
Windows and WASI), rather than emulating atomic replacement with an unsafe
delete-and-rewrite sequence.
