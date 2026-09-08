#ifndef TOKA_THREAD_HANDOFF_V1_H
#define TOKA_THREAD_HANDOFF_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared private ABI. Compiler adapters and runtime must use this exact key.
 * Public compiler activation must use interface 0.9.9-18 (or a later explicitly
 * qualified revision), never silently combine this protocol with -17 TKI. */
#define TOKA_THREAD_HANDOFF_VERSION_V1 UINT32_C(1)
#define TOKA_THREAD_HANDOFF_ABI_V1 "toka-thread-handoff-v1/compiler-0.9.9-18"
#define TOKA_THREAD_FATAL_EXIT_V1 134

enum TokaThreadStatusV1 {
    TOKA_THREAD_OK_V1 = 0,
    TOKA_THREAD_ALLOCATION_V1 = 1,
    TOKA_THREAD_CREATE_V1 = 2,
    TOKA_THREAD_JOIN_V1 = 3,
    TOKA_THREAD_DETACH_V1 = 4,
    TOKA_THREAD_CLOSED_V1 = 5,
    TOKA_THREAD_BUSY_V1 = 6,
    TOKA_THREAD_SETUP_V1 = 7
};

typedef struct TokaThreadControl TokaThreadControl;
typedef struct TokaThreadPrepared TokaThreadPrepared;
typedef struct TokaThreadResultLease TokaThreadResultLease;

/* Static-lifetime, immutable compiler-produced descriptors. Complete semantic
 * identity, target ABI and interface identity belong in type_key/contract_key;
 * a short type name or digest alone is not identity proof. Runtime validates
 * the carrier, not the compiler's ownership/lifetime derivation. */
typedef struct TokaThreadResultOpsV1 {
    uint32_t version;
    uint32_t struct_size;
    const char *abi_key;
    const char *type_key;
    size_t value_size;
    size_t value_alignment;
    /* Destructive relocation of a live, complete T into uninitialized storage.
     * No allocation, user code, unwind, retain or recoverable failure. */
    void (*move_out)(void *source, void *destination);
    /* Drop exactly one live T, but do not free source storage. May call user
     * code; runtime must never hold its state mutex here. */
    void (*drop_live)(void *source);
} TokaThreadResultOpsV1;

typedef struct TokaThreadEnvOpsV1 {
    uint32_t version;
    uint32_t struct_size;
    const char *abi_key;
    const char *contract_key;
    const TokaThreadResultOpsV1 *result_ops;
    /* On normal return: packet consumed/freed; one complete result constructed.
     * Callback invokes the real typed callable, not a cast Toka/C signature. */
    void (*run_once)(void *packet, void *result_storage);
    /* Consume/free an unstarted packet and its still-live environment. */
    void (*drop_unstarted)(void *packet);
} TokaThreadEnvOpsV1;

/* Versioned strong symbol is also a link-time old-runtime rejection boundary. */
void toka_thread_require_compiler_0_9_9_18_v1(void);

/* All output slots must be nonnull and initially NULL. Opaque wrappers are
 * runtime-owned allocations: callers never sizeof/copy/free them themselves.
 * prepare succeeds: packet responsibility moves into *out.
 * prepare allocation failure: packet remains the caller's responsibility.
 * Invalid descriptors/ABI are fatal, before callbacks or transfer. */
int32_t toka_thread_prepare_v1(const TokaThreadEnvOpsV1 *ops, void *packet,
                               TokaThreadPrepared **out);
void toka_thread_dispose_prepared_v1(TokaThreadPrepared **inout);

/* start success: prepared becomes NULL, *out owns H. Failure: prepared still
 * owns unstarted packet, out remains NULL; caller must dispose prepared.
 * Worker may finish before start returns. No access after dropping W. */
int32_t toka_thread_start_v1(TokaThreadPrepared **inout, TokaThreadControl **out,
                            int32_t *native_code);

/* Error preserves H and leaves result out empty. Success consumes H, clears
 * inout, and transfers an independent live result lease without allocation. */
int32_t toka_thread_join_v1(TokaThreadControl **inout,
                           const TokaThreadResultOpsV1 *expected,
                           TokaThreadResultLease **out, int32_t *native_code);
int32_t toka_thread_detach_v1(TokaThreadControl **inout, int32_t *native_code);

/* Empty is a no-op. Detach failure during implicit drop: outside the state
 * lock, no allocation or I/O, immediate TOKA_THREAD_FATAL_EXIT_V1 termination;
 * no promise that process termination runs all Drops. */
void toka_thread_drop_handle_v1(TokaThreadControl **inout);

/* Take validates expected type/alignment before relocation. Complete legal
 * leases cannot fail recoverably; no user code during move_out. Destination
 * uninitialized/accessible storage is a compiler-validated precondition. Both
 * take and drop clear *inout and free opaque lease plus result allocation. */
void toka_thread_take_result_v1(TokaThreadResultLease **inout,
                                const TokaThreadResultOpsV1 *expected,
                                void *destination);
void toka_thread_drop_result_v1(TokaThreadResultLease **inout);

#ifdef __cplusplus
}
#endif
#endif
