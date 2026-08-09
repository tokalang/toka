#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create_with_result_drop(
    void *coro_frame, void *promise, void (*result_drop_fn)(void *));
extern void toka_task_release(void *tcb_ptr);
extern void toka_task_detach(void *tcb_ptr);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern int toka_task_take_result(void *promise_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);
extern int __toka_task_take_result_access(void *promise_ptr,
                                          void **out_value_ptr,
                                          void **out_access_guard);
extern void __toka_task_release_result_access(void *access_guard);
extern int toka_task_try_retain(void *tcb_ptr);
extern uint32_t toka_rt_test_get_frame_access_state(void *tcb_ptr);
extern void *toka_task_scope_create(void);
extern int toka_task_scope_try_enroll(void *scope_ptr, void *tcb_ptr);
extern int toka_task_scope_begin_close(void *scope_ptr);
extern int toka_task_scope_finish_close(void *scope_ptr);
extern void toka_task_scope_release(void *scope_ptr);
extern void toka_rt_test_pause_terminal_after_result_commit(void);
extern int toka_rt_test_terminal_result_commit_paused(void);
extern void toka_rt_test_resume_terminal_result_commit(void);

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

static void test_stale_promise_header_fails_closed(void) {
    ResultProbe probe = {0};
    void *tcb = toka_task_create_with_result_drop(
        NULL, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);
    // The caller still owns the stack-resident promise bytes, but the TCB is
    // gone. A compatibility result claim must validate its non-null header
    // pointer through the task registry rather than dereferencing freed TCB
    // storage or falling back to the promise-only ABI.
    toka_task_release(tcb);
    CHECK(toka_task_take_result(&probe.promise) == 0);
}

static void test_result_access_guard_keeps_frame_alive(void) {
    ResultProbe probe = {0};
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *tcb = toka_task_create_with_result_drop(
        frame, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);
    toka_task_complete(&probe.promise);

    void *value_ptr = NULL;
    void *access_guard = NULL;
    CHECK(__toka_task_take_result_access(&probe.promise, &value_ptr,
                                         &access_guard) == 1);
    CHECK(value_ptr == &probe.drop_count);
    CHECK(access_guard == tcb);

    // The external owner may leave immediately after the claim; the access
    // guard still owns the frame pin required for the generated typed load.
    toka_task_release(tcb);
    CHECK(toka_rt_test_get_frame_access_state(access_guard) == 1);
    CHECK(atomic_load((_Atomic uint32_t *)value_ptr) == 0);

    __toka_task_release_result_access(access_guard);
    CHECK(toka_task_try_retain(tcb) == 0);
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

static void *publish_normal_terminal(void *arg) {
    toka_task_complete(arg);
    return NULL;
}

typedef struct {
    void *tcb;
    FakePromise *promise;
} DetachedTerminalRace;

static void *detach_running_task(void *arg) {
    DetachedTerminalRace *race = (DetachedTerminalRace *)arg;
    toka_task_detach(race->tcb);
    return NULL;
}

static void *complete_running_task(void *arg) {
    DetachedTerminalRace *race = (DetachedTerminalRace *)arg;
    toka_task_complete(race->promise);
    return NULL;
}

static void test_detach_after_result_commit_before_terminal(void) {
    ResultProbe probe = {0};
    void *tcb = toka_task_create_with_result_drop(
        NULL, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);

    // Detach starts cold tasks, which would deliberately leave an unrelated
    // ready-queue reference. Model the real running-task handoff instead.
    CHECK(toka_task_start(tcb));
    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == tcb);

    // Freeze the unique handoff interval after public ReadyLive is committed
    // but before Completed makes it claimable. Detach transfers the result
    // owner but cannot drop the payload yet; the terminal publisher must do
    // that once it publishes normal completion.
    toka_rt_test_pause_terminal_after_result_commit();
    pthread_t publisher;
    CHECK(pthread_create(&publisher, NULL, publish_normal_terminal,
                         &probe.promise) == 0);
    while (!toka_rt_test_terminal_result_commit_paused()) {
    }

    CHECK(atomic_load(&probe.promise.result_state) ==
          TOKA_RESULT_STATE_READYLIVE);
    toka_task_detach(tcb);
    CHECK(atomic_load(&probe.drop_count) == 0);

    toka_rt_test_resume_terminal_result_commit();
    CHECK(pthread_join(publisher, NULL) == 0);
    check_drop_once(&probe);
    toka_task_clear_current(worker);
    toka_task_release(worker);
    CHECK(toka_task_try_retain(tcb) == 0);
}

static void test_detached_canceled_terminal_releases_owner(void) {
    ResultProbe probe = {0};
    void *tcb = toka_task_create_with_result_drop(
        NULL, &probe.promise, drop_probe_result);
    CHECK(tcb != NULL);
    CHECK(toka_task_start(tcb));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == tcb);

    toka_task_detach(tcb);
    toka_task_complete_canceled(&probe.promise);
    CHECK(toka_tcb_is_canceled(tcb));
    CHECK(atomic_load(&probe.promise.result_state) ==
          TOKA_RESULT_STATE_CANCELED);
    CHECK(atomic_load(&probe.drop_count) == 0);

    toka_task_clear_current(worker);
    toka_task_release(worker);
    CHECK(toka_task_try_retain(tcb) == 0);
}

static void test_concurrent_detach_and_terminal_drain(void) {
    for (int i = 0; i < 2000; ++i) {
        ResultProbe probe = {0};
        void *tcb = toka_task_create_with_result_drop(
            NULL, &probe.promise, drop_probe_result);
        CHECK(tcb != NULL);
        CHECK(toka_task_start(tcb));

        uint64_t task_id = 0;
        uint64_t generation = 0;
        void *worker = NULL;
        CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
        CHECK(worker == tcb);

        DetachedTerminalRace race = {
            .tcb = tcb,
            .promise = &probe.promise,
        };
        pthread_t detacher;
        pthread_t publisher;
        CHECK(pthread_create(&detacher, NULL, detach_running_task, &race) == 0);
        CHECK(pthread_create(&publisher, NULL, complete_running_task, &race) == 0);
        CHECK(pthread_join(detacher, NULL) == 0);
        CHECK(pthread_join(publisher, NULL) == 0);

        check_drop_once(&probe);
        toka_task_clear_current(worker);
        toka_task_release(worker);
        CHECK(toka_task_try_retain(tcb) == 0);
    }
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
    test_stale_promise_header_fails_closed();
    test_result_access_guard_keeps_frame_alive();
    test_detached_drain_from_both_sides();
    test_detach_after_result_commit_before_terminal();
    test_detached_canceled_terminal_releases_owner();
    test_concurrent_detach_and_terminal_drain();
    test_scope_drain_uses_the_same_claim();
    puts("async result disposition claim ordering passed");
    return 0;
}
