#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create_with_result_drop(
    void *coro_frame, void *promise, void (*result_drop_fn)(void *));
extern void toka_task_release(void *tcb_ptr);
extern void toka_task_detach(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern int toka_task_take_result(void *promise_ptr);
extern void *toka_task_scope_create(void);
extern int toka_task_scope_try_enroll(void *scope_ptr, void *tcb_ptr);
extern int toka_task_scope_begin_close(void *scope_ptr);
extern int toka_task_scope_finish_close(void *scope_ptr);
extern void toka_task_scope_release(void *scope_ptr);

enum {
    TOKA_RESULT_STATE_READYLIVE = 1,
    TOKA_RESULT_STATE_TAKEN = 2,
    TOKA_RESULT_STATE_CANCELED = 3,
};

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

typedef struct {
    FakePromise promise;
    _Atomic uint32_t drop_count;
    _Atomic uint8_t state_during_drop;
    _Atomic int reentrant_take_result;
} ResultProbe;

_Static_assert(offsetof(ResultProbe, drop_count) == sizeof(FakePromise),
               "test result payload must follow the promise header");

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "result disposition check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void drop_probe_result(void *payload_ptr) {
    ResultProbe *probe = (ResultProbe *)((char *)payload_ptr -
        offsetof(ResultProbe, drop_count));
    atomic_fetch_add(&probe->drop_count, 1);
    atomic_store(&probe->state_during_drop,
                 atomic_load_explicit(&probe->promise.result_state,
                                      memory_order_acquire));

    // This must neither deadlock nor steal the typed payload. The runtime has
    // already claimed Dropping privately, but has not yet published Taken.
    atomic_store(&probe->reentrant_take_result,
                 toka_task_take_result(&probe->promise));
}

static void check_drop_once(const ResultProbe *probe) {
    CHECK(atomic_load(&probe->drop_count) == 1);
    CHECK(atomic_load(&probe->state_during_drop) ==
          TOKA_RESULT_STATE_READYLIVE);
    CHECK(atomic_load(&probe->reentrant_take_result) == 0);
    CHECK(atomic_load(&probe->promise.result_state) ==
          TOKA_RESULT_STATE_TAKEN);
}

static void test_take_requires_normal_terminal(void) {
    ResultProbe premature = {0};
    void *premature_tcb = toka_task_create_with_result_drop(
        NULL, &premature.promise, drop_probe_result);
    CHECK(premature_tcb != NULL);

    // A raw ReadyLive observation before the corresponding Completed state is
    // deliberately not sufficient to transfer payload ownership.
    atomic_store(&premature.promise.result_state, TOKA_RESULT_STATE_READYLIVE);
    CHECK(toka_task_take_result(&premature.promise) == 0);
    CHECK(atomic_load(&premature.promise.result_state) ==
          TOKA_RESULT_STATE_READYLIVE);
    toka_task_release(premature_tcb);

    ResultProbe canceled = {0};
    void *canceled_tcb = toka_task_create_with_result_drop(
        NULL, &canceled.promise, drop_probe_result);
    CHECK(canceled_tcb != NULL);
    toka_task_complete_canceled(&canceled.promise);
    CHECK(toka_task_take_result(&canceled.promise) == -1);
    CHECK(atomic_load(&canceled.drop_count) == 0);
    CHECK(atomic_load(&canceled.promise.result_state) ==
          TOKA_RESULT_STATE_CANCELED);
    toka_task_release(canceled_tcb);
}

static void test_consumer_transfer_uses_private_claim(void) {
    ResultProbe probe = {0};
    void *tcb = toka_task_create_with_result_drop(
        NULL, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);
    toka_task_complete(&probe.promise);
    CHECK(toka_task_take_result(&probe.promise) == 1);
    CHECK(atomic_load(&probe.promise.result_state) ==
          TOKA_RESULT_STATE_TAKEN);
    CHECK(atomic_load(&probe.drop_count) == 0);
    CHECK(toka_task_take_result(&probe.promise) == 0);
    toka_task_release(tcb);
}

static void test_detached_drain_from_both_sides(void) {
    ResultProbe detach_before_complete = {0};
    void *before_tcb = toka_task_create_with_result_drop(
        NULL, &detach_before_complete.promise, drop_probe_result);
    CHECK(before_tcb != NULL);
    toka_task_detach(before_tcb);
    toka_task_complete(&detach_before_complete.promise);
    check_drop_once(&detach_before_complete);

    ResultProbe complete_before_detach = {0};
    void *after_tcb = toka_task_create_with_result_drop(
        NULL, &complete_before_detach.promise, drop_probe_result);
    CHECK(after_tcb != NULL);
    toka_task_complete(&complete_before_detach.promise);
    toka_task_detach(after_tcb);
    check_drop_once(&complete_before_detach);
}

static void test_scope_drain_uses_the_same_claim(void) {
    ResultProbe probe = {0};
    void *scope = toka_task_scope_create();
    CHECK(scope != NULL);
    void *tcb = toka_task_create_with_result_drop(
        NULL, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);
    CHECK(toka_task_scope_try_enroll(scope, tcb) == 1);
    toka_task_complete(&probe.promise);
    CHECK(toka_task_scope_begin_close(scope));
    CHECK(toka_task_scope_finish_close(scope));
    check_drop_once(&probe);
    toka_task_scope_release(scope);
}

int main(void) {
    test_take_requires_normal_terminal();
    test_consumer_transfer_uses_private_claim();
    test_detached_drain_from_both_sides();
    test_scope_drain_uses_the_same_claim();
    puts("async result disposition claim ordering passed");
    return 0;
}
