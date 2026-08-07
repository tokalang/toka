#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern void toka_task_release(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern void toka_task_publish_result_state(void *promise_ptr, uint8_t state);
extern int toka_task_request_cancel(void *tcb_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);

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

int main(void) {
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
