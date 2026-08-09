#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend_token(
    void *coro_frame, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern void toka_task_pause_next_queue_publication_for_test(void);
extern void toka_wait_set_pause_next_commit_for_test(void);
extern int toka_wait_registry_allocate_pair_token(
    uint64_t task_id, uint64_t instance, uint64_t gen, uint16_t tag1,
    uint16_t tag2, uint32_t *out_id1, uint32_t *out_gen1,
    uint32_t *out_id2, uint32_t *out_gen2);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
extern int toka_task_try_schedule_token(uint64_t task_id,
                                        uint64_t instance_generation,
                                        uint64_t generation);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern uint32_t toka_ready_queue_count(void);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern void toka_task_publish_result_state(void *promise_ptr, uint8_t state);
extern int toka_task_request_cancel(void *tcb_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);
extern int toka_task_try_retain(void *tcb_ptr);
extern uint32_t toka_rt_test_get_frame_access_state(void *tcb_ptr);
extern void toka_rt_test_pause_terminal_after_publish(void);
extern int toka_rt_test_terminal_publish_paused(void);
extern void toka_rt_test_resume_terminal_publish(void);

enum {
    TOKA_RESULT_STATE_PENDING = 0,
    TOKA_RESULT_STATE_READYLIVE = 1,
    TOKA_RESULT_STATE_CANCELED = 3,
};

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

typedef struct {
    FakePromise promise;
} TerminalRace;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "terminal publisher check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void assert_terminal_pair(void *tcb, FakePromise *promise) {
    uint8_t result = atomic_load_explicit(&promise->result_state,
                                          memory_order_acquire);
    CHECK(toka_tcb_is_done(tcb));
    if (result == TOKA_RESULT_STATE_READYLIVE) {
        CHECK(!toka_tcb_is_canceled(tcb));
    } else {
        CHECK(result == TOKA_RESULT_STATE_CANCELED);
        CHECK(toka_tcb_is_canceled(tcb));
    }
}

static void *publish_normal(void *arg) {
    TerminalRace *race = (TerminalRace *)arg;
    toka_task_complete(&race->promise);
    return NULL;
}

static void *publish_canceled(void *arg) {
    TerminalRace *race = (TerminalRace *)arg;
    toka_task_complete_canceled(&race->promise);
    return NULL;
}

static void *publish_canceled_pinned(void *arg) {
    FakePromise *promise = (FakePromise *)arg;
    toka_task_complete_canceled(promise);
    return NULL;
}

static void publish_terminal(FakePromise *promise, int canceled) {
    if (canceled) {
        toka_task_complete_canceled(promise);
    } else {
        toka_task_complete(promise);
    }
}

static void test_terminal_publisher_holds_frame_pin(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    FakePromise promise = {0};
    void *tcb = toka_task_create(frame, &promise);
    CHECK(tcb != NULL);

    toka_rt_test_pause_terminal_after_publish();
    pthread_t publisher;
    CHECK(pthread_create(&publisher, NULL, publish_canceled_pinned,
                         &promise) == 0);
    while (!toka_rt_test_terminal_publish_paused()) {
    }

    CHECK(toka_tcb_is_done(tcb));
    CHECK(toka_tcb_is_canceled(tcb));
    CHECK(toka_rt_test_get_frame_access_state(tcb) == 1);
    // The publisher's checked retain keeps the TCB and frame alive after the
    // last external owner leaves; its pin is the remaining retirement guard.
    toka_task_release(tcb);
    CHECK(toka_rt_test_get_frame_access_state(tcb) == 1);

    toka_rt_test_resume_terminal_publish();
    CHECK(pthread_join(publisher, NULL) == 0);
    CHECK(toka_task_try_retain(tcb) == 0);
    free(frame);
}

static void test_terminal_uninstalls_active_wait_set(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    FakePromise promise = {0};
    void *owner = toka_task_create(frame, &promise);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    void *worker = NULL;
    uint64_t task_id = 0;
    uint64_t generation = 0;
    uint64_t instance = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &instance, &generation));

    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    uint32_t second_id = 0;
    uint32_t second_generation = 0;
    CHECK(toka_wait_registry_allocate_pair_token(
        task_id, instance, generation, 1, 2, &first_id, &first_generation,
        &second_id, &second_generation));
    CHECK(toka_task_commit_suspend(frame));
    toka_task_clear_current(worker);
    CHECK(toka_rt_live_wait_registry_count() == 2);

    toka_task_complete(&promise);
    CHECK(toka_tcb_is_done(owner));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
    CHECK(toka_ready_queue_count() == 0);

    toka_task_release(worker);
    toka_task_release(owner);
}

static void test_terminal_reaps_selected_wait_set_outcomes(int canceled) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    FakePromise promise = {0};
    void *owner = toka_task_create(frame, &promise);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    void *worker = NULL;
    uint64_t task_id = 0;
    uint64_t generation = 0;
    uint64_t instance = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &instance, &generation));

    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    uint32_t second_id = 0;
    uint32_t second_generation = 0;
    CHECK(toka_wait_registry_allocate_pair_token(
        task_id, instance, generation, 1, 2, &first_id, &first_generation,
        &second_id, &second_generation));
    CHECK(toka_task_commit_suspend(frame));
    toka_task_clear_current(worker);

    // A source owns the winner but its normal commit path is preempted. A
    // terminal publisher must finish that descriptor and reclaim the outcome
    // slots itself; no future user release can be required from a dead task.
    toka_wait_set_pause_next_commit_for_test();
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 2);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    publish_terminal(&promise, canceled);
    CHECK(toka_tcb_is_done(owner));
    CHECK(toka_tcb_is_canceled(owner) == canceled);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
    CHECK(!toka_wait_registry_release(first_id, first_generation));
    CHECK(!toka_wait_registry_release(second_id, second_generation));

    toka_task_release(worker);
    toka_task_release(owner);
}

static void test_terminal_blocks_preempted_queue_publication(int canceled) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    FakePromise promise = {0};
    void *owner = toka_task_create(frame, &promise);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    void *worker = NULL;
    uint64_t task_id = 0;
    uint64_t generation = 0;
    uint64_t instance = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &instance, &generation));

    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    uint32_t second_id = 0;
    uint32_t second_generation = 0;
    CHECK(toka_wait_registry_allocate_pair_token(
        task_id, instance, generation, 1, 2, &first_id, &first_generation,
        &second_id, &second_generation));
    CHECK(toka_task_commit_suspend(frame));
    toka_task_clear_current(worker);

    // The selected descriptor has claimed Queued but the original publisher
    // is preempted before physical queue insertion. Terminal publication must
    // make a late helper observe a stale ticket rather than resume a dead task.
    toka_task_pause_next_queue_publication_for_test();
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 2);
    CHECK(toka_ready_queue_count() == 0);
    publish_terminal(&promise, canceled);
    CHECK(toka_tcb_is_done(owner));
    CHECK(toka_tcb_is_canceled(owner) == canceled);
    CHECK(toka_task_try_schedule_token(task_id, instance, generation) == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);

    toka_task_release(worker);
    toka_task_release(owner);
}

int main(void) {
    test_terminal_publisher_holds_frame_pin();
    test_terminal_uninstalls_active_wait_set();
    test_terminal_reaps_selected_wait_set_outcomes(0);
    test_terminal_reaps_selected_wait_set_outcomes(1);
    test_terminal_blocks_preempted_queue_publication(0);
    test_terminal_blocks_preempted_queue_publication(1);

    FakePromise normal_then_cancel = {0};
    void *normal_tcb = toka_task_create(NULL, &normal_then_cancel);
    CHECK(normal_tcb != NULL);
    toka_task_complete(&normal_then_cancel);
    toka_task_complete_canceled(&normal_then_cancel);
    assert_terminal_pair(normal_tcb, &normal_then_cancel);
    CHECK(atomic_load(&normal_then_cancel.result_state) ==
          TOKA_RESULT_STATE_READYLIVE);
    toka_task_release(normal_tcb);

    FakePromise cancel_then_normal = {0};
    void *canceled_tcb = toka_task_create(NULL, &cancel_then_normal);
    CHECK(canceled_tcb != NULL);
    toka_task_complete_canceled(&cancel_then_normal);
    toka_task_complete(&cancel_then_normal);
    assert_terminal_pair(canceled_tcb, &cancel_then_normal);
    CHECK(atomic_load(&cancel_then_normal.result_state) ==
          TOKA_RESULT_STATE_CANCELED);
    toka_task_release(canceled_tcb);

    FakePromise legacy_publish = {0};
    void *legacy_tcb = toka_task_create(NULL, &legacy_publish);
    CHECK(legacy_tcb != NULL);
    toka_task_publish_result_state(&legacy_publish,
                                   TOKA_RESULT_STATE_READYLIVE);
    toka_task_complete_canceled(&legacy_publish);
    assert_terminal_pair(legacy_tcb, &legacy_publish);
    CHECK(atomic_load(&legacy_publish.result_state) ==
          TOKA_RESULT_STATE_READYLIVE);
    toka_task_release(legacy_tcb);

    FakePromise cold_cancel = {0};
    void *cold_tcb = toka_task_create(NULL, &cold_cancel);
    CHECK(cold_tcb != NULL);
    CHECK(toka_task_request_cancel(cold_tcb));
    toka_task_complete(&cold_cancel);
    assert_terminal_pair(cold_tcb, &cold_cancel);
    CHECK(atomic_load(&cold_cancel.result_state) ==
          TOKA_RESULT_STATE_CANCELED);
    toka_task_release(cold_tcb);

    for (int i = 0; i < 4000; ++i) {
        TerminalRace race = {0};
        void *tcb = toka_task_create(NULL, &race.promise);
        CHECK(tcb != NULL);

        pthread_t normal_thread;
        pthread_t canceled_thread;
        CHECK(pthread_create(&normal_thread, NULL, publish_normal, &race) == 0);
        CHECK(pthread_create(&canceled_thread, NULL, publish_canceled, &race) == 0);
        CHECK(pthread_join(normal_thread, NULL) == 0);
        CHECK(pthread_join(canceled_thread, NULL) == 0);

        assert_terminal_pair(tcb, &race.promise);
        toka_task_release(tcb);
    }

    puts("async terminal publisher arbitration passed");
    return 0;
}
