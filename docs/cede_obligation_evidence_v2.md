# Cede Obligation Evidence v2

`tokac --cede-obligations=v2` emits the RC9 signature-driven ownership facts.
It is separately versioned from the frozen v1 explicit-spelling contract.

Caller records add three orthogonal fields:

- `spelling`: `implicit` or `explicit`;
- `transfer`: `CopyValue`, `CopyIdentity`, `TransferShared`, `MoveOwned`,
  `ConsumeTemporary`, or fail-closed `Indeterminate`;
- `source`: `KeepLive`, `InvalidatePlace`, `NoSourcePlace`, or
  `Indeterminate`.

Callee-consumption and return-transfer records retain their v1 meaning and use
`null` for these caller-only fields. Diagnostics and process exit status remain
authoritative. Consumers must reject unknown versions. The normative schema is
[`toka.cede-obligation-evidence.v2.schema.json`](../schemas/toka.cede-obligation-evidence.v2.schema.json).

V1 remains available through `--cede-obligations=json` as a frozen RC8 legacy
replay profile and is not changed by this protocol.
