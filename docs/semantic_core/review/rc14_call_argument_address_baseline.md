# RC14 concrete-payload call-address baseline

This is the pre-fix receipt for the published Toka `v1.0.0-rc.13` macOS ARM64
SDK at candidate `6fb3337fa4f9956392326a7783c9dcdecabd7e5b`. The RC14
source branch was still at `d4909ff70401a0e568c42c17b1db6f1983782f7f`
when these probes were run. No compiler source was changed for this receipt.

The checked-in `tests/semantics/rc14_call_argument_address/payload_capture_matrix.tk`
compiled successfully and exited **3**, its unique-root payload-read failure.
The separate direct-value, nested unique-field, and reference controls exited
0; a shared-root payload read exited 7 in its isolated probe. A normal method
argument with the same unique-root source exited 0. The read-only payload-write
negative reported `E04571` and emitted no executable.

The generated LLVM IR shows the physical-layer mismatch before any callee
mutation. The caller has a unique handle slot and a shared carrier slot:

```llvm
%unique = alloca ptr
%shared = alloca { ptr, ptr }
%7 = call i32 @read_unique(ptr %unique)
%15 = call i32 @read_shared(ptr %shared)
```

Both callees expect an address of the concrete `Plain` payload. Passing the
caller slot/carrier address instead makes the callee interpret handle bytes as
payload fields. The intended repair is to select the source's validated
payload view for a concrete payload formal, without changing handle-identity,
consuming unique, or abstract whole-value transport contracts.
