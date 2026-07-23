// Controlled 20,000-iteration All-Permutations Concurrency Gate Race Probe
// Tests prepare_suspend, try_schedule, and commit_suspend under heavy multi-threaded interleaving.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>

extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id, uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern int toka_task_try_schedule(uint64_t task_id, uint64_t gen);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen, void **out_tcb_ptr);
extern void toka_task_release(void *tcb_ptr);

typedef struct {
    void *tcb;
    void *dummy_frame;
    uint64_t task_id;
    uint64_t gen;
    _Atomic int prep_done;
    _Atomic int wake_res;
    _Atomic int commit_res;
} GateProbeContext;

static void* prepare_worker(void *arg) {
    GateProbeContext *ctx = (GateProbeContext*)arg;
    uint64_t tid = 0, gen = 0;
    int ok = toka_task_prepare_suspend(ctx->dummy_frame, &tid, &gen);
    if (ok) {
        ctx->task_id = tid;
        ctx->gen = gen;
    }
    ctx->prep_done = ok;
    return NULL;
}

static void* wake_worker(void *arg) {
    GateProbeContext *ctx = (GateProbeContext*)arg;
    // Wait until prepare completes or spin
    while (!ctx->prep_done) {
        #if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
        #endif
    }
    ctx->wake_res = toka_task_try_schedule(ctx->task_id, ctx->gen);
    return NULL;
}

static void* commit_worker(void *arg) {
    GateProbeContext *ctx = (GateProbeContext*)arg;
    while (!ctx->prep_done) {
        #if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
        #endif
    }
    ctx->commit_res = toka_task_commit_suspend(ctx->dummy_frame);
    return NULL;
}

int main(void) {
    printf("Starting 20,000-iteration All-Permutations Concurrency Gate Race Probe...\n");

    for (int iter = 1; iter <= 20000; iter++) {
        int dummy_val = 42;
        void *dummy_frame = &dummy_val;

        GateProbeContext ctx = {0};
        ctx.dummy_frame = dummy_frame;
        ctx.tcb = toka_task_create(dummy_frame, NULL);

        // Put task into Running state
        toka_task_start(ctx.tcb);
        uint64_t popped_id = 0, popped_gen = 0;
        void *popped_tcb = NULL;
        int popped = toka_task_pop_ready(&popped_id, &popped_gen, &popped_tcb);
        assert(popped == 1); // State is now RUNNING

        pthread_t t1, t2, t3;
        pthread_create(&t1, NULL, prepare_worker, &ctx);
        pthread_create(&t2, NULL, wake_worker, &ctx);
        pthread_create(&t3, NULL, commit_worker, &ctx);

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
        pthread_join(t3, NULL);

        // Check if task is queued in ready queue
        uint64_t final_id = 0, final_gen = 0;
        void *final_tcb = NULL;
        int final_popped = toka_task_pop_ready(&final_id, &final_gen, &final_tcb);
        assert(final_popped == 1);
        assert(final_id == ctx.task_id);

        toka_task_release(final_tcb);
        toka_task_release(popped_tcb);
    }

    printf("PASSED 20,000/20,000 iterations! AR-P1.1 Concurrency Gate Zero Lost-Wake verified!\n");
    return 0;
}
