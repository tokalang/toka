# `stdx/version/semver` v1

`stdx/version/semver` provides an owned, strict Semantic Versioning 2.0.0 value type.
`SemVer::parse` accepts `major.minor.patch`, optional pre-release identifiers,
and optional build metadata. `compare` implements SemVer precedence: numeric
pre-release identifiers sort numerically and before text identifiers; build
metadata does not affect precedence.

`SemVerError` carries the first invalid byte position and an owned explanatory
message. Core numbers and numeric pre-release identifiers are bounded to
non-negative `i64`; overflow is rejected rather than wrapped.

Version ranges, dependency solving, loose/coercive parsing, and package
registry policy are deliberately outside this value-type API.
