#define _POSIX_C_SOURCE 200809L
#include "toka_thread_handoff_v1.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#define TOKA_THREAD_POSIX 1
#else
#define TOKA_THREAD_POSIX 0
#endif

/* Fault substitution is compile-time-only and absent from production builds. */
#ifdef TOKA_THREAD_HANDOFF_TESTING
#include "thread_handoff_v1_test_hooks.h"
#define RT_ALLOC toka_test_alloc
#define RT_FREE toka_test_free
#define RT_ALIGNED toka_test_aligned
#define RT_CREATE toka_test_create
#define RT_JOIN toka_test_join
#define RT_DETACH toka_test_detach
#define RT_MUTEX_INIT toka_test_mutex_init
#define RT_LOCK toka_test_mutex_lock
#define RT_UNLOCK toka_test_mutex_unlock
#define RT_EVENT(e) toka_test_event(e)
#else
#define RT_ALLOC malloc
#define RT_FREE free
#define RT_ALIGNED posix_memalign
#define RT_CREATE pthread_create
#define RT_JOIN pthread_join
#define RT_DETACH pthread_detach
#define RT_MUTEX_INIT(m, a) pthread_mutex_init(m, a)
#define RT_LOCK pthread_mutex_lock
#define RT_UNLOCK pthread_mutex_unlock
#define RT_EVENT(e) ((void)0)
#endif

static void fatal(void) {
    /* No diagnostic I/O, stdio flush, atexit or user signal handler. Even a
     * full stderr pipe must not turn an unrecoverable failure into a wait. */
    _Exit(TOKA_THREAD_FATAL_EXIT_V1);
}

static void require(int condition) { if (!condition) fatal(); }

static void validate_result(const TokaThreadResultOpsV1 *ops) {
    require(ops && ops->version == TOKA_THREAD_HANDOFF_VERSION_V1 &&
            ops->struct_size == sizeof(*ops) && ops->abi_key &&
            strcmp(ops->abi_key, TOKA_THREAD_HANDOFF_ABI_V1) == 0 &&
            ops->type_key && ops->type_key[0] && ops->value_alignment &&
            (ops->value_alignment & (ops->value_alignment - 1)) == 0 &&
            ops->move_out && ops->drop_live);
}

static void match_result(const TokaThreadResultOpsV1 *actual,
                         const TokaThreadResultOpsV1 *expected) {
    validate_result(actual);
    validate_result(expected);
    require(strcmp(actual->type_key, expected->type_key) == 0 &&
            actual->value_size == expected->value_size &&
            actual->value_alignment == expected->value_alignment);
    /* Equivalent compiler descriptors may have distinct module-local thunks.
     * The producer descriptor remains authoritative for execution; expected
     * proves the requested complete type/ABI, not an executable address. */
}

static void validate_env(const TokaThreadEnvOpsV1 *ops) {
    require(ops && ops->version == TOKA_THREAD_HANDOFF_VERSION_V1 &&
            ops->struct_size == sizeof(*ops) && ops->abi_key &&
            strcmp(ops->abi_key, TOKA_THREAD_HANDOFF_ABI_V1) == 0 &&
            ops->contract_key && ops->contract_key[0] &&
            ops->run_once && ops->drop_unstarted);
    validate_result(ops->result_ops);
}

enum PreparedState { PreparedReady, PreparedStarting };
struct TokaThreadPrepared {
    const TokaThreadEnvOpsV1 *ops;
    void *packet;
    enum PreparedState state;
};
struct TokaThreadResultLease {
    const TokaThreadResultOpsV1 *ops;
    void *storage;
};

void toka_thread_require_compiler_0_9_9_18_v1(void) {}

int32_t toka_thread_prepare_v1(const TokaThreadEnvOpsV1 *ops, void *packet,
                               TokaThreadPrepared **out) {
    require(out && !*out && packet);
    validate_env(ops);
    TokaThreadPrepared *prepared = RT_ALLOC(sizeof(*prepared));
    if (!prepared) return TOKA_THREAD_ALLOCATION_V1;
    prepared->ops = ops;
    prepared->packet = packet;
    prepared->state = PreparedReady;
    *out = prepared;
    return TOKA_THREAD_OK_V1;
}

void toka_thread_dispose_prepared_v1(TokaThreadPrepared **inout) {
    require(inout != NULL);
    if (!*inout) return;
    TokaThreadPrepared *prepared = *inout;
    require(prepared->state == PreparedReady);
    validate_env(prepared->ops);
    require(prepared->packet != NULL);
    *inout = NULL;
    prepared->ops->drop_unstarted(prepared->packet);
    RT_FREE(prepared);
}

static void free_empty_lease(TokaThreadResultLease *lease) {
    RT_FREE(lease->storage);
    RT_FREE(lease);
}

void toka_thread_drop_result_v1(TokaThreadResultLease **inout) {
    require(inout != NULL);
    if (!*inout) return;
    TokaThreadResultLease *lease = *inout;
    validate_result(lease->ops);
    require(lease->storage != NULL);
    *inout = NULL;
    lease->ops->drop_live(lease->storage);
    free_empty_lease(lease);
}

void toka_thread_take_result_v1(TokaThreadResultLease **inout,
                                const TokaThreadResultOpsV1 *expected,
                                void *destination) {
    require(inout && *inout && destination);
    TokaThreadResultLease *lease = *inout;
    match_result(lease->ops, expected);
    require(lease->storage &&
            (uintptr_t)destination % expected->value_alignment == 0 &&
            destination != lease->storage);
    uintptr_t source_address = (uintptr_t)lease->storage;
    uintptr_t destination_address = (uintptr_t)destination;
    size_t extent = expected->value_size ? expected->value_size : 1;
    uintptr_t distance = source_address > destination_address
        ? source_address - destination_address : destination_address - source_address;
    require(distance >= extent);
    *inout = NULL;
    lease->ops->move_out(lease->storage, destination);
    free_empty_lease(lease);
}

#if TOKA_THREAD_POSIX
enum Native { Starting, Open, Joining, Detaching, Joined, Detached };
enum Result { Pending, Ready, Taken, DiscardClaimed, Disposed };
struct TokaThreadControl {
    pthread_mutex_t mutex;
    pthread_t thread;
    enum Native native;
    enum Result result;
    unsigned references;
    const TokaThreadEnvOpsV1 *env_ops;
    void *packet;
    TokaThreadResultLease *lease;
};

static void lock(TokaThreadControl *control) {
    require(RT_LOCK(&control->mutex) == 0);
}
static void unlock(TokaThreadControl *control) {
    require(RT_UNLOCK(&control->mutex) == 0);
}

/* Caller must own H or W. On return control may have been destroyed. */
static void release(TokaThreadControl *control) {
    lock(control);
    require(control->references > 0);
    int destroy = --control->references == 0;
    if (destroy) require(!control->packet && !control->lease &&
                         (control->native == Joined || control->native == Detached) &&
                         (control->result == Taken || control->result == Disposed));
    unlock(control);
    if (destroy) {
        require(pthread_mutex_destroy(&control->mutex) == 0);
        RT_FREE(control);
        RT_EVENT(TOKA_TEST_CONTROL_FREED);
    }
}

static void *trampoline(void *argument) {
    TokaThreadControl *control = argument;
    lock(control);
    void *packet = control->packet;
    const TokaThreadEnvOpsV1 *ops = control->env_ops;
    void *storage = control->lease->storage;
    require(packet && control->result == Pending);
    control->packet = NULL;
    unlock(control);
    ops->run_once(packet, storage);

    lock(control);
    require(control->result == Pending);
    TokaThreadResultLease *discard = NULL;
    if (control->native == Detached) {
        control->result = DiscardClaimed;
        discard = control->lease;
        control->lease = NULL;
    } else control->result = Ready;
    unlock(control);
    if (discard) {
        toka_thread_drop_result_v1(&discard);
        lock(control);
        control->result = Disposed;
        unlock(control);
    }
    RT_EVENT(TOKA_TEST_WORKER_PUBLISHED);
    /* W release is the final control-block operation. No hook or member read
     * follows it; returning NULL does not borrow from the control block. */
    release(control);
    return NULL;
}

int32_t toka_thread_start_v1(TokaThreadPrepared **inout, TokaThreadControl **out,
                            int32_t *native_code) {
    require(inout && out && !*out && native_code);
    *native_code = 0;
    if (!*inout) return TOKA_THREAD_CLOSED_V1;
    TokaThreadPrepared *prepared = *inout;
    require(prepared->state == PreparedReady);
    validate_env(prepared->ops);
    require(prepared->packet != NULL);
    TokaThreadControl *control = RT_ALLOC(sizeof(*control));
    if (!control) return TOKA_THREAD_ALLOCATION_V1;
    TokaThreadResultLease *lease = RT_ALLOC(sizeof(*lease));
    if (!lease) { RT_FREE(control); return TOKA_THREAD_ALLOCATION_V1; }
    lease->ops = prepared->ops->result_ops;
    size_t alignment = lease->ops->value_alignment;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    lease->storage = NULL;
    if (RT_ALIGNED(&lease->storage, alignment,
                   lease->ops->value_size ? lease->ops->value_size : 1) != 0) {
        RT_FREE(lease);
        RT_FREE(control);
        return TOKA_THREAD_ALLOCATION_V1;
    }
    int error = RT_MUTEX_INIT(&control->mutex, NULL);
    if (error) {
        free_empty_lease(lease);
        RT_FREE(control);
        *native_code = error;
        return TOKA_THREAD_SETUP_V1;
    }
    control->native = Starting;
    control->result = Pending;
    control->references = 2;
    control->env_ops = prepared->ops;
    control->packet = prepared->packet;
    control->lease = lease;
    pthread_t thread;
    prepared->state = PreparedStarting;
    error = RT_CREATE(&thread, NULL, trampoline, control);
    if (error) {
        /* POSIX guarantees no worker exists on this branch. The prepared
         * wrapper still owns the packet; neither H nor reserved W escapes. */
        require(pthread_mutex_destroy(&control->mutex) == 0);
        free_empty_lease(lease);
        RT_FREE(control);
        prepared->state = PreparedReady;
        *native_code = error;
        return TOKA_THREAD_CREATE_V1;
    }
    lock(control);
    require(control->native == Starting);
    control->thread = thread;
    control->native = Open;
    unlock(control);
    *inout = NULL;
    RT_FREE(prepared); /* Do not inspect packet: worker may already free it. */
    *out = control;
    return TOKA_THREAD_OK_V1;
}

int32_t toka_thread_join_v1(TokaThreadControl **inout,
                           const TokaThreadResultOpsV1 *expected,
                           TokaThreadResultLease **out, int32_t *native_code) {
    require(inout && out && !*out && native_code);
    *native_code = 0;
    if (!*inout) return TOKA_THREAD_CLOSED_V1;
    TokaThreadControl *control = *inout;
    match_result(control->env_ops->result_ops, expected);
    lock(control);
    if (control->native != Open) { unlock(control); return TOKA_THREAD_BUSY_V1; }
    control->native = Joining;
    pthread_t thread = control->thread;
    unlock(control);
    int error = RT_JOIN(thread, NULL);
    lock(control);
    require(control->native == Joining);
    if (error) {
        control->native = Open;
        unlock(control);
        *native_code = error;
        return TOKA_THREAD_JOIN_V1;
    }
    require(control->result == Ready && !control->packet && control->lease);
    control->native = Joined;
    control->result = Taken;
    TokaThreadResultLease *lease = control->lease;
    control->lease = NULL;
    unlock(control);
    *inout = NULL;
    *out = lease;
    release(control);
    return TOKA_THREAD_OK_V1;
}

int32_t toka_thread_detach_v1(TokaThreadControl **inout, int32_t *native_code) {
    require(inout && native_code);
    *native_code = 0;
    if (!*inout) return TOKA_THREAD_CLOSED_V1;
    TokaThreadControl *control = *inout;
    lock(control);
    if (control->native != Open) { unlock(control); return TOKA_THREAD_BUSY_V1; }
    control->native = Detaching;
    pthread_t thread = control->thread;
    unlock(control);
    int error = RT_DETACH(thread);
    lock(control);
    require(control->native == Detaching);
    if (error) {
        control->native = Open;
        unlock(control);
        *native_code = error;
        return TOKA_THREAD_DETACH_V1;
    }
    control->native = Detached;
    TokaThreadResultLease *discard = NULL;
    if (control->result == Ready) {
        control->result = DiscardClaimed;
        discard = control->lease;
        control->lease = NULL;
    } else require(control->result == Pending);
    unlock(control);
    *inout = NULL;
    if (discard) {
        toka_thread_drop_result_v1(&discard);
        lock(control);
        control->result = Disposed;
        unlock(control);
    }
    release(control);
    return TOKA_THREAD_OK_V1;
}
#else
/* No pthread_t stand-in or fake success on unsupported targets. */
int32_t toka_thread_start_v1(TokaThreadPrepared **inout, TokaThreadControl **out,
                            int32_t *native_code) {
    require(inout && out && !*out && native_code);
    *native_code = 0;
    if (!*inout) return TOKA_THREAD_CLOSED_V1;
    require((*inout)->state == PreparedReady);
#ifdef ENOTSUP
    *native_code = ENOTSUP;
#else
    *native_code = EINVAL;
#endif
    return TOKA_THREAD_SETUP_V1;
}
int32_t toka_thread_join_v1(TokaThreadControl **inout,
                           const TokaThreadResultOpsV1 *expected,
                           TokaThreadResultLease **out, int32_t *native_code) {
    (void)expected;
    require(inout && !*inout && out && !*out && native_code);
    *native_code = 0;
    return TOKA_THREAD_CLOSED_V1;
}
int32_t toka_thread_detach_v1(TokaThreadControl **inout, int32_t *native_code) {
    require(inout && !*inout && native_code);
    *native_code = 0;
    return TOKA_THREAD_CLOSED_V1;
}
#endif

void toka_thread_drop_handle_v1(TokaThreadControl **inout) {
    require(inout != NULL);
    if (!*inout) return;
    int32_t native_code;
    if (toka_thread_detach_v1(inout, &native_code) != TOKA_THREAD_OK_V1) fatal();
}
