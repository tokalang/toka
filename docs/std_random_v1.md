# `std/random` v1

`std/random` is the standard library's OS-backed cryptographically secure
randomness boundary. `secure_random_bytes(length)` allocates an owned buffer;
`fill_secure_random_bytes(buffer)` fills and returns an existing owned buffer.
Both APIs return `Result` and never silently substitute a deterministic PRNG.

The runtime ABI and temporary raw buffer address are confined below the public
API. `stdx/rand` remains a separate deterministic PRNG library and must not be
used for tokens, UUIDs, WebSocket masking keys, or other security-sensitive
material.

`std/net::net_random_bytes` remains as a compatibility forwarder; new code
should import `std/random` directly.
