// AR-P5 Redline Qualification: 20,000-Iteration Multithreaded Cancellation & Lifecycle Stress Test
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

// Declarations matching lib/sys/toka_rt.c
extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_request_cancel(void *tcb_ptr);
extern void toka_task_detach(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern void toka_task_retain(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen, uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id, uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern int toka_task_pop_ready(uint64_t *out_id, uint64_t *out_gen, void **out_tcb_ptr);
extern uint32_t toka_task_active_detached_count(void);
extern uint32_t toka_rt_live_tcb_count(void);
extern uint32_t toka_rt_live_wait_registry_count(void);

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

#define ITERATIONS 20000

typedef struct {
    void *tcb;
    void *dummy_frame;
} ThreadContext;

static _Atomic uint32_t g_hit_prepare_suspend = 0;
static _Atomic uint32_t g_hit_commit_suspend = 0;
static _Atomic uint32_t g_hit_cancel_running = 0;

static void* worker_thread_cancel(void *arg) {
    ThreadContext *ctx = (ThreadContext*)arg;
    if (toka_task_request_cancel(ctx->tcb)) {
        atomic_fetch_add(&g_hit_cancel_running, 1);
    }
    return NULL;
}

static void* worker_thread_suspend(void *arg) {
    ThreadContext *ctx = (ThreadContext*)arg;
    uint64_t tid = 0, gen = 0;
    if (toka_task_prepare_suspend(ctx->dummy_frame, &tid, &gen)) {
        atomic_fetch_add(&g_hit_prepare_suspend, 1);
        uint32_t wid = 0, wgen = 0;
        if (toka_wait_registry_allocate(tid, gen, 1, &wid, &wgen)) {
            if (toka_task_commit_suspend(ctx->dummy_frame)) {
                atomic_fetch_add(&g_hit_commit_suspend, 1);
            }
            toka_wait_registry_try_wake(wid, wgen);
            toka_wait_registry_release(wid, wgen);
        }
    }
    return NULL;
}

int main(void) {
    printf("--- Starting 20,000-Iteration AR-P5 Cancellation Redline Stress Test ---\n");
    fflush(stdout);

    for (int i = 0; i < ITERATIONS; ++i) {
        FakePromise promise = {0};
        void **dummy_frame = (void**)calloc(8, sizeof(void*));
        assert(dummy_frame != NULL);
        dummy_frame[1] = NULL; // Safe coroutine frame destructor

        void *tcb = toka_task_create(dummy_frame, &promise);
        assert(tcb != NULL);

        // 1. Transition CREATED -> QUEUED
        toka_task_start(tcb);

        // 2. Transition QUEUED -> RUNNING by popping from ready queue
        uint64_t r_id = 0, r_gen = 0;
        void *r_tcb = NULL;
        if (toka_task_pop_ready(&r_id, &r_gen, &r_tcb)) {
            assert(r_tcb == tcb);
            toka_task_release(r_tcb); // Release ready-queue ownership transferred to r_tcb
        }

        ThreadContext ctx = { .tcb = tcb, .dummy_frame = dummy_frame };

        // 3. Spawning concurrent threads in RUNNING state
        pthread_t t_cancel, t_suspend;
        pthread_create(&t_cancel, NULL, worker_thread_cancel, &ctx);
        pthread_create(&t_suspend, NULL, worker_thread_suspend, &ctx);

        pthread_join(t_cancel, NULL);
        pthread_join(t_suspend, NULL);

        // Drain remaining C ready queue items
        while (toka_task_pop_ready(&r_id, &r_gen, &r_tcb)) {
            if (r_tcb) {
                toka_task_release(r_tcb);
            }
        }

        // Complete task & drop owner handle via detach
        toka_task_complete(&promise);
        toka_task_detach(tcb);

        if ((i + 1) % 5000 == 0) {
            printf("[Stress] Completed %d / %d iterations cleanly...\n", i + 1, ITERATIONS);
            fflush(stdout);
        }
    }

    assert(toka_rt_live_tcb_count() == 0);
    assert(toka_rt_live_wait_registry_count() == 0);
    assert(toka_task_active_detached_count() == 0);
    assert(atomic_load(&g_hit_prepare_suspend) > 0);
    assert(atomic_load(&g_hit_commit_suspend) > 0);
    assert(atomic_load(&g_hit_cancel_running) > 0);

    printf("--- 20,000-Iteration Redline Stress Test PASSED Cleanly (Live TCBs=%u, Live Registrations=%u, Hits: prepare=%u, commit=%u, cancel_running=%u)! ---\n",
           toka_rt_live_tcb_count(), toka_rt_live_wait_registry_count(),
           atomic_load(&g_hit_prepare_suspend), atomic_load(&g_hit_commit_suspend), atomic_load(&g_hit_cancel_running));
    fflush(stdout);
    return 0;
}
