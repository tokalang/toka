#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create_with_result_drop_and_cold_cleanup(
    void *coro_frame, void *promise, void (*result_drop_fn)(void *));
extern int toka_task_request_cancel(void *tcb_ptr);
extern void toka_task_drop_handle(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);
extern uint32_t toka_rt_live_tcb_count(void);

enum {
    TOKA_RESULT_STATE_CANCELED = 3,
};

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

typedef struct {
    _Atomic uint32_t cleanup_count;
    _Atomic uint32_t terminal_seen_during_cleanup;
} CleanupProbe;

typedef struct {
    void (*resume)(void *);
    void (*destroy)(void *);
    FakePromise promise;
    CleanupProbe *probe;
} ColdFrame;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "cold cleanup check failed: %s (%s:%d)\\n",  \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void cold_destroy(void *frame_ptr) {
    ColdFrame *frame = (ColdFrame *)frame_ptr;
    if (toka_tcb_is_done(frame->promise.self_tcb)) {
        atomic_fetch_add(&frame->probe->terminal_seen_during_cleanup, 1);
    }
    atomic_fetch_add(&frame->probe->cleanup_count, 1);
}

static ColdFrame *new_cold_frame(CleanupProbe *probe) {
    ColdFrame *frame = (ColdFrame *)calloc(1, sizeof(ColdFrame));
    CHECK(frame != NULL);
    frame->destroy = cold_destroy;
    frame->probe = probe;
    return frame;
}

int main(void) {
    CleanupProbe canceled_probe = {0};
    ColdFrame *canceled_frame = new_cold_frame(&canceled_probe);
    void *canceled_tcb = toka_task_create_with_result_drop_and_cold_cleanup(
        canceled_frame, &canceled_frame->promise, NULL);
    CHECK(canceled_tcb != NULL);
    CHECK(toka_task_request_cancel(canceled_tcb));
    CHECK(atomic_load(&canceled_probe.cleanup_count) == 1);
    CHECK(atomic_load(&canceled_probe.terminal_seen_during_cleanup) == 0);
    CHECK(toka_tcb_is_done(canceled_tcb));
    CHECK(toka_tcb_is_canceled(canceled_tcb));
    CHECK(atomic_load(&canceled_frame->promise.result_state) ==
          TOKA_RESULT_STATE_CANCELED);
    toka_task_release(canceled_tcb);

    CleanupProbe dropped_probe = {0};
    ColdFrame *dropped_frame = new_cold_frame(&dropped_probe);
    void *dropped_tcb = toka_task_create_with_result_drop_and_cold_cleanup(
        dropped_frame, &dropped_frame->promise, NULL);
    CHECK(dropped_tcb != NULL);
    toka_task_drop_handle(dropped_tcb);
    CHECK(atomic_load(&dropped_probe.cleanup_count) == 1);
    CHECK(atomic_load(&dropped_probe.terminal_seen_during_cleanup) == 0);
    CHECK(toka_rt_live_tcb_count() == 0);

    puts("async cold cancellation cleanup ordering passed");
    return 0;
}
