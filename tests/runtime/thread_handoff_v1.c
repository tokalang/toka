#define _POSIX_C_SOURCE 200809L
#include "toka_thread_handoff_v1.h"
#include "thread_handoff_v1_test_hooks.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int worker_open, published, freed_controls;
static int wait_before_create_return, fail_allocation, fail_setup, fail_create;
static int fail_join, fail_detach, complete_during_native, reenter_native;
static TokaThreadControl **reenter_handle;
static TokaThreadPrepared **dispose_during_create;
static void *constructed_storage;
static atomic_int allocations, frees, attempts, invokes, env_drops, result_drops, moves;
static pthread_t creator, destructor_thread;
static _Thread_local int runtime_lock_depth;

static void wait_for(int *field, int target) {
    assert(pthread_mutex_lock(&gate) == 0);
    while (*field < target) assert(pthread_cond_wait(&condition, &gate) == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
}
static void open_worker(void) {
    assert(pthread_mutex_lock(&gate) == 0);
    worker_open = 1;
    assert(pthread_cond_broadcast(&condition) == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
}
void toka_test_event(int event) {
    assert(pthread_mutex_lock(&gate) == 0);
    if (event == TOKA_TEST_WORKER_PUBLISHED) ++published;
    else if (event == TOKA_TEST_CONTROL_FREED) ++freed_controls;
    else abort();
    assert(pthread_cond_broadcast(&condition) == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
}
void *toka_test_alloc(size_t size) {
    assert(runtime_lock_depth == 0);
    int attempt = atomic_fetch_add(&attempts, 1) + 1;
    if (attempt == fail_allocation) return NULL;
    void *pointer = malloc(size);
    if (pointer) atomic_fetch_add(&allocations, 1);
    return pointer;
}
void toka_test_free(void *pointer) {
    assert(runtime_lock_depth == 0);
    if (pointer) atomic_fetch_add(&frees, 1);
    free(pointer);
}
int toka_test_aligned(void **out, size_t alignment, size_t size) {
    assert(runtime_lock_depth == 0);
    int attempt = atomic_fetch_add(&attempts, 1) + 1;
    if (attempt == fail_allocation) return ENOMEM;
    int result = posix_memalign(out, alignment, size);
    if (!result) atomic_fetch_add(&allocations, 1);
    return result;
}
int toka_test_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    return fail_setup ? ENOMEM : pthread_mutex_init(mutex, attr);
}
int toka_test_mutex_lock(pthread_mutex_t *mutex) {
    int result = pthread_mutex_lock(mutex);
    if (!result) ++runtime_lock_depth;
    return result;
}
int toka_test_mutex_unlock(pthread_mutex_t *mutex) {
    assert(runtime_lock_depth == 1);
    --runtime_lock_depth;
    return pthread_mutex_unlock(mutex);
}
int toka_test_create(pthread_t *thread, const pthread_attr_t *attr,
                     void *(*entry)(void *), void *context) {
    assert(runtime_lock_depth == 0);
    if (dispose_during_create) toka_thread_dispose_prepared_v1(dispose_during_create);
    if (fail_create) return EAGAIN;
    int result = pthread_create(thread, attr, entry, context);
    if (!result && wait_before_create_return) wait_for(&published, 1);
    return result;
}
static void native_window(void) {
    assert(runtime_lock_depth == 0);
    if (reenter_native) {
        int32_t code = 77;
        assert(toka_thread_detach_v1(reenter_handle, &code) == TOKA_THREAD_BUSY_V1);
        assert(code == 0 && *reenter_handle);
    }
    if (complete_during_native) { open_worker(); wait_for(&published, 1); }
}
int toka_test_join(pthread_t thread, void **result) {
    native_window();
    return fail_join ? EDEADLK : pthread_join(thread, result);
}
int toka_test_detach(pthread_t thread) {
    native_window();
    return fail_detach ? EINVAL : pthread_detach(thread);
}

typedef struct { int value; int *resource; } Value;
typedef struct { int value; } Packet;
static void drop_value(void *storage) {
    assert(runtime_lock_depth == 0);
    Value *value = storage;
    assert(value->resource && *value->resource == value->value);
    free(value->resource);
    value->resource = NULL;
    destructor_thread = pthread_self();
    atomic_fetch_add(&result_drops, 1);
    /* Reenter empty API while user cleanup runs; no runtime lock is needed. */
    TokaThreadControl *empty = NULL;
    int32_t code;
    assert(toka_thread_detach_v1(&empty, &code) == TOKA_THREAD_CLOSED_V1);
}
static void move_value(void *source, void *destination) {
    assert(runtime_lock_depth == 0);
    memcpy(destination, source, sizeof(Value));
    ((Value *)source)->resource = NULL;
    atomic_fetch_add(&moves, 1);
}
static void run(void *pointer, void *storage) {
    assert(runtime_lock_depth == 0);
    wait_for(&worker_open, 1);
    Packet *packet = pointer;
    Value value = {packet->value, malloc(sizeof(int))};
    assert(value.resource);
    *value.resource = value.value;
    memcpy(storage, &value, sizeof(value));
    constructed_storage = storage;
    free(packet);
    atomic_fetch_add(&invokes, 1);
    atomic_fetch_add(&env_drops, 1);
}
static void unstarted(void *pointer) {
    assert(runtime_lock_depth == 0);
    free(pointer);
    atomic_fetch_add(&env_drops, 1);
}
static const TokaThreadResultOpsV1 value_ops = {
    1, sizeof(TokaThreadResultOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
    "test/module::Value/i32+owned-int/target-native/interface-18", sizeof(Value),
    _Alignof(Value), move_value, drop_value
};
static const TokaThreadEnvOpsV1 env_ops = {
    1, sizeof(TokaThreadEnvOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
    "test/module::run/owned-packet/to-Value/interface-18", &value_ops, run, unstarted
};

static void reset(void) {
    assert(atomic_load(&allocations) == atomic_load(&frees));
    worker_open = published = freed_controls = 0;
    wait_before_create_return = fail_allocation = fail_setup = fail_create = 0;
    fail_join = fail_detach = complete_during_native = reenter_native = 0;
    dispose_during_create = NULL;
    constructed_storage = NULL;
    atomic_store(&attempts, 0);
    atomic_store(&allocations, 0);
    atomic_store(&frees, 0);
    atomic_store(&invokes, 0);
    atomic_store(&env_drops, 0);
    atomic_store(&result_drops, 0);
    atomic_store(&moves, 0);
    creator = pthread_self();
}
static TokaThreadPrepared *prepare(void) {
    Packet *packet = malloc(sizeof(*packet));
    assert(packet); packet->value = 42;
    TokaThreadPrepared *prepared = NULL;
    int result = toka_thread_prepare_v1(&env_ops, packet, &prepared);
    if (result) { assert(result == TOKA_THREAD_ALLOCATION_V1); unstarted(packet); }
    return prepared;
}
static TokaThreadControl *start(void) {
    TokaThreadPrepared *prepared = prepare();
    assert(prepared);
    TokaThreadControl *handle = NULL;
    int32_t code = 99;
    assert(toka_thread_start_v1(&prepared, &handle, &code) == TOKA_THREAD_OK_V1);
    assert(!prepared && handle && !code);
    return handle;
}
static void join_take(TokaThreadControl **handle) {
    TokaThreadResultLease *lease = NULL;
    int32_t code = 9;
    assert(toka_thread_join_v1(handle, &value_ops, &lease, &code) == TOKA_THREAD_OK_V1);
    assert(!*handle && lease && !code);
    assert(freed_controls == 1); /* Lease survives destruction of control. */
    int previous_attempts = atomic_load(&attempts);
    Value value = {0, NULL};
    toka_thread_take_result_v1(&lease, &value_ops, &value);
    assert(!lease && value.value == 42 && atomic_load(&moves) == 1);
    assert(atomic_load(&attempts) == previous_attempts);
    drop_value(&value);
    assert(atomic_load(&env_drops) == 1 && atomic_load(&result_drops) == 1);
    assert(pthread_equal(creator, destructor_thread));
}

static void allocation_failures(void) {
    for (int at = 1; at <= 4; ++at) {
        reset(); fail_allocation = at;
        TokaThreadPrepared *prepared = prepare();
        if (at > 1) {
            assert(prepared);
            TokaThreadControl *handle = NULL;
            int32_t code = 1;
            assert(toka_thread_start_v1(&prepared, &handle, &code) == TOKA_THREAD_ALLOCATION_V1);
            assert(prepared && !handle && !code);
            toka_thread_dispose_prepared_v1(&prepared);
            assert(!prepared);
        } else assert(!prepared);
        assert(atomic_load(&env_drops) == 1 && atomic_load(&invokes) == 0);
        assert(atomic_load(&result_drops) == 0);
    }
    for (int setup = 0; setup <= 1; ++setup) {
        reset(); fail_setup = setup; fail_create = !setup;
        TokaThreadPrepared *prepared = prepare();
        TokaThreadControl *handle = NULL;
        int32_t code;
        assert(toka_thread_start_v1(&prepared, &handle, &code) ==
               (setup ? TOKA_THREAD_SETUP_V1 : TOKA_THREAD_CREATE_V1));
        assert(code == (setup ? ENOMEM : EAGAIN));
        assert(prepared && !handle && !atomic_load(&invokes));
        toka_thread_dispose_prepared_v1(&prepared);
        assert(!prepared && atomic_load(&env_drops) == 1);
    }
}

static void success_paths(void) {
    reset(); worker_open = 1; wait_before_create_return = 1;
    TokaThreadControl *handle = start();
    assert(published == 1);
    join_take(&handle);
    TokaThreadResultLease *empty = NULL;
    int32_t code;
    assert(toka_thread_join_v1(&handle, &value_ops, &empty, &code) == TOKA_THREAD_CLOSED_V1);
    assert(toka_thread_detach_v1(&handle, &code) == TOKA_THREAD_CLOSED_V1);
    toka_thread_drop_handle_v1(&handle);
    toka_thread_drop_result_v1(&empty);

    reset(); worker_open = 1;
    handle = start();
    TokaThreadResultLease *lease = NULL;
    assert(toka_thread_join_v1(&handle, &value_ops, &lease, &code) == TOKA_THREAD_OK_V1);
    assert(!handle && lease && freed_controls == 1);
    toka_thread_drop_result_v1(&lease);
    assert(!lease && atomic_load(&result_drops) == 1 && !atomic_load(&moves));

    for (int late = 0; late <= 1; ++late) {
        for (int implicit = 0; implicit <= 1; ++implicit) {
            reset(); worker_open = late; wait_before_create_return = late;
            handle = start();
            if (implicit) toka_thread_drop_handle_v1(&handle);
            else assert(toka_thread_detach_v1(&handle, &code) == TOKA_THREAD_OK_V1);
            assert(!handle);
            if (!late) { assert(!atomic_load(&invokes)); open_worker(); }
            wait_for(&freed_controls, 1);
            assert(atomic_load(&env_drops) == 1 && atomic_load(&result_drops) == 1);
            assert(!!pthread_equal(creator, destructor_thread) == late);
        }
    }
}

static void native_failures(void) {
    for (int is_join = 0; is_join <= 1; ++is_join) {
        for (int complete = 0; complete <= 1; ++complete) {
          for (int finish_detached = 0; finish_detached <= 1; ++finish_detached) {
            reset();
            TokaThreadControl *handle = start();
            TokaThreadControl *original = handle;
            TokaThreadResultLease *lease = NULL;
            complete_during_native = complete;
            reenter_native = 1; reenter_handle = &handle;
            fail_join = is_join; fail_detach = !is_join;
            int32_t code;
            int status = is_join ? toka_thread_join_v1(&handle, &value_ops, &lease, &code)
                                 : toka_thread_detach_v1(&handle, &code);
            assert(status == (is_join ? TOKA_THREAD_JOIN_V1 : TOKA_THREAD_DETACH_V1));
            assert(code == (is_join ? EDEADLK : EINVAL));
            assert(handle == original && !lease && !atomic_load(&result_drops));
            fail_join = fail_detach = reenter_native = complete_during_native = 0;
            if (finish_detached) {
                assert(toka_thread_detach_v1(&handle, &code) == TOKA_THREAD_OK_V1);
                assert(!handle);
                open_worker();
                wait_for(&freed_controls, 1);
                assert(atomic_load(&env_drops) == 1 && atomic_load(&result_drops) == 1);
            } else {
                open_worker();
                join_take(&handle);
            }
          }
        }
    }
    for (int is_join = 0; is_join <= 1; ++is_join) {
        reset();
        TokaThreadControl *original = start();
        TokaThreadControl *handle = original;
        original = NULL; /* Exactly H moves; this does not retain. */
        toka_thread_drop_handle_v1(&original);
        complete_during_native = reenter_native = 1;
        reenter_handle = &handle;
        if (is_join) join_take(&handle);
        else {
            int32_t code;
            assert(toka_thread_detach_v1(&handle, &code) == TOKA_THREAD_OK_V1);
            assert(!handle);
            wait_for(&freed_controls, 1);
            assert(atomic_load(&env_drops) == 1 && atomic_load(&result_drops) == 1);
        }
    }
}

static void zero_run(void *packet, void *storage) {
    assert(runtime_lock_depth == 0 && storage);
    free(packet);
    atomic_fetch_add(&env_drops, 1);
}
static void zero_move(void *source, void *destination) {
    assert(runtime_lock_depth == 0 && source && destination);
    atomic_fetch_add(&moves, 1);
}
static void zero_drop(void *source) {
    assert(runtime_lock_depth == 0 && source);
    atomic_fetch_add(&result_drops, 1);
}
static void aligned_run(void *packet, void *storage) {
    assert(runtime_lock_depth == 0 && (uintptr_t)storage % 64 == 0);
    memset(storage, 0x5a, 128);
    free(packet);
    atomic_fetch_add(&env_drops, 1);
}
static void aligned_drop(void *storage) {
    assert(runtime_lock_depth == 0 && (uintptr_t)storage % 64 == 0);
    for (size_t i = 0; i < 128; ++i) assert(((unsigned char *)storage)[i] == 0x5a);
    atomic_fetch_add(&result_drops, 1);
}
static void aligned_move(void *source, void *destination) {
    assert(runtime_lock_depth == 0 && (uintptr_t)source % 64 == 0 &&
           (uintptr_t)destination % 64 == 0);
    memcpy(destination, source, 128);
    atomic_fetch_add(&moves, 1);
}
typedef struct { atomic_int references; int value; } Shared;
static void shared_run(void *packet, void *storage) {
    assert(runtime_lock_depth == 0);
    memcpy(storage, packet, sizeof(Shared *));
    free(packet);
    atomic_fetch_add(&env_drops, 1);
}
static void shared_drop(void *storage) {
    assert(runtime_lock_depth == 0);
    Shared *shared = *(Shared **)storage;
    assert(shared->value == 42);
    if (atomic_fetch_sub(&shared->references, 1) == 1) free(shared);
    atomic_fetch_add(&result_drops, 1);
}
static void shared_move(void *source, void *destination) {
    assert(runtime_lock_depth == 0);
    memcpy(destination, source, sizeof(Shared *));
    *(Shared **)source = NULL;
    atomic_fetch_add(&moves, 1);
}
static const TokaThreadResultOpsV1 layout_results[] = {
    {1, sizeof(TokaThreadResultOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/zero/interface-18", 0, 1, zero_move, zero_drop},
    {1, sizeof(TokaThreadResultOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/aligned128/interface-18", 128, 64, aligned_move, aligned_drop},
    {1, sizeof(TokaThreadResultOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/shared/interface-18", sizeof(Shared *), _Alignof(Shared *), shared_move, shared_drop}
};
static const TokaThreadEnvOpsV1 layout_envs[] = {
    {1, sizeof(TokaThreadEnvOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/zero-env/interface-18", &layout_results[0], zero_run, unstarted},
    {1, sizeof(TokaThreadEnvOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/aligned-env/interface-18", &layout_results[1], aligned_run, unstarted},
    {1, sizeof(TokaThreadEnvOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
     "test/shared-env/interface-18", &layout_results[2], shared_run, unstarted}
};
static void layout_cases(void) {
    for (int kind = 0; kind < 3; ++kind) {
        for (int take = 0; take <= 1; ++take) {
            reset();
            void *packet = malloc(sizeof(Shared *));
            assert(packet);
            Shared *shared = NULL;
            if (kind == 2) {
                shared = malloc(sizeof(*shared));
                assert(shared);
                atomic_init(&shared->references, 2); shared->value = 42;
                *(Shared **)packet = shared;
            }
            TokaThreadPrepared *prepared = NULL;
            TokaThreadControl *handle = NULL;
            TokaThreadResultLease *lease = NULL;
            int32_t code;
            assert(toka_thread_prepare_v1(&layout_envs[kind], packet, &prepared) == 0);
            assert(toka_thread_start_v1(&prepared, &handle, &code) == 0);
            assert(toka_thread_join_v1(&handle, &layout_results[kind], &lease, &code) == 0);
            assert(!handle && lease && freed_controls == 1);
            if (take) {
                void *destination = NULL;
                assert(posix_memalign(&destination, 64, 128) == 0);
                int before = atomic_load(&attempts);
                toka_thread_take_result_v1(&lease, &layout_results[kind], destination);
                assert(atomic_load(&attempts) == before);
                layout_results[kind].drop_live(destination);
                free(destination);
            } else toka_thread_drop_result_v1(&lease);
            assert(!lease && atomic_load(&result_drops) == 1 && atomic_load(&env_drops) == 1);
            if (shared) { assert(atomic_load(&shared->references) == 1); free(shared); }
        }
    }
}

static void fatal_case(int which) {
    reset();
    struct rlimit limit = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &limit) == 0);
    TokaThreadResultOpsV1 bad = value_ops;
    TokaThreadEnvOpsV1 bad_env = env_ops;
    if (which < 8) {
        switch (which) {
        case 0: bad.version = 0; break;
        case 1: bad.struct_size = 0; break;
        case 2: bad.abi_key = "old-runtime"; break;
        case 3: bad.type_key = ""; break;
        case 4: bad.value_alignment = 3; break;
        case 5: bad.move_out = NULL; break;
        case 6: bad.drop_live = NULL; break;
        case 7: bad_env.contract_key = ""; break;
        }
        bad_env.result_ops = &bad;
        TokaThreadPrepared *prepared = NULL;
        Packet packet = {42};
        (void)toka_thread_prepare_v1(&bad_env, &packet, &prepared);
    } else if (which == 12) {
        TokaThreadPrepared *prepared = prepare();
        TokaThreadControl *handle = NULL;
        int32_t code;
        dispose_during_create = &prepared;
        (void)toka_thread_start_v1(&prepared, &handle, &code);
    } else {
        TokaThreadControl *handle = start();
        if (which == 8) { fail_detach = 1; toka_thread_drop_handle_v1(&handle); }
        else {
            TokaThreadResultLease *lease = NULL;
            int32_t code;
            if (which == 9) {
                bad.type_key = "different-type";
                (void)toka_thread_join_v1(&handle, &bad, &lease, &code);
            } else {
                open_worker();
                assert(toka_thread_join_v1(&handle, &value_ops, &lease, &code) == 0);
                Value destination;
                if (which == 10) bad.value_size += 1;
                if (which == 11) bad.value_alignment *= 2;
                void *target = which == 13
                    ? (void *)((unsigned char *)constructed_storage + _Alignof(Value))
                    : (void *)&destination;
                toka_thread_take_result_v1(&lease, &bad, target);
            }
        }
    }
    _exit(99);
}

int main(void) {
    alarm(30); /* Deadlock watchdog, never used to schedule a passing test. */
    toka_thread_require_compiler_0_9_9_18_v1();
    allocation_failures();
    success_paths();
    native_failures();
    layout_cases();
    reset();
    for (int which = 0; which < 14; ++which) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) fatal_case(which);
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
    puts("thread handoff v1: 28 lifetime/layout/failure schedules + 14 fatal descriptor/drop cases passed");
    return 0;
}
