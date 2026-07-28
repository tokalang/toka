# `stdx/uuid` v1

`stdx/uuid` provides a small RFC 9562 UUID value type. Its stable v1 surface
is `Uuid::parse`, `Uuid::to_string`, `Uuid::nil`, `Uuid::is_nil`,
`Uuid::version`, and `Uuid::new_v4`.

`new_v4` obtains 16 bytes only through `std/net::net_random_bytes`, the
OS-randomness adapter. `stdx/rand::Random` is intentionally not used because
it is PCG32 and is not a cryptographic entropy source. Parsing and formatting
operate on safe value arrays and strings; this extension introduces no raw
pointer or FFI API.

This is a Tier 3 `stdx` extension. UUID versions other than v4 generation,
name-based UUIDs, database adapters, and serialization traits remain outside
v1.
