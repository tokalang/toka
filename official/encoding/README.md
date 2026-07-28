# `official/encoding` v1

`official/encoding` provides small, safe binary-to-text codecs. Its package
identity and public import path are `official/encoding`; the manifest short
name is `encoding`.

## v1 contract

The initial slice provides strict RFC 4648 hexadecimal and standard Base64
encoding:

```toka
import official/encoding::{hex_encode, hex_decode}

auto wire = hex_encode("Toka".as_bytes())
assert(wire.equals("546f6b61"))

auto raw = hex_decode("546f6b61").unwrap() // Vec<u8>
```

`hex_encode` accepts Toka's borrowed `bytes` view, so it does not copy its
input. `hex_decode` accepts text and returns an owned `Vec<u8>`; arbitrary
binary output is therefore never forced through `string`. Invalid input yields
`DecodeError`, including the relevant byte position.

Hex decoding accepts ASCII upper- and lowercase digits, requires an even input
length, and rejects all other bytes. URL parsing and percent/form encoding stay
in `stdx/net`; no raw pointer API is exposed by this package.

`base64_encode` and `base64_decode` use the standard `A-Z a-z 0-9 + /`
alphabet with required canonical `=` padding. URL-safe Base64, unpadded Base64,
and whitespace-tolerant MIME decoding are deliberately separate APIs rather
than silent parser modes.

## Qualification

```text
tokac -I ../../../lib -I lib tests/encoding_v1.tk -o /tmp/encoding_v1 && /tmp/encoding_v1
```
