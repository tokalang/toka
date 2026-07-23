// Controlled 20,000-iteration WaitRegistry Race Probe
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdatomic.h>

extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen, void **out_tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id, uint64_t *out_schedule_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern void toka_task_release(void *tcb_ptr);

extern int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen, uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);

typedef struct {
    uint32_t wait_id;
    uint32_t slot_gen;
    _Atomic uint32_t winner_count;
} RaceContext;

static void* wake_worker(void *arg) {
    RaceContext *ctx = (RaceContext*)arg;
    int outcome = toka_wait_registry_try_wake(ctx->wait_id, ctx->slot_gen);
    if (outcome == 1 || outcome == 2) {
        atomic_fetch_add(&ctx->winner_count, 1);
    }
    return NULL;
}

int main(void) {
    printf("Starting 20,000-iteration WaitRegistry Concurrency Probe...\n");

    for (int iter = 1; iter <= 20000; iter++) {
        void *dummy_frame = malloc(128);
        void *tcb = toka_task_create(dummy_frame, NULL);
        toka_task_start(tcb);

        uint64_t popped_id = 0, popped_gen = 0;
        void *popped_tcb = NULL;
        int popped = toka_task_pop_ready(&popped_id, &popped_gen, &popped_tcb);
        assert(popped == 1); // State is now RUNNING

        uint64_t tid = 0, gen = 0;
        int prep_ok = toka_task_prepare_suspend(dummy_frame, &tid, &gen);
        assert(prep_ok == 1);
        toka_task_commit_suspend(dummy_frame);

        uint32_t wait_id = 0, slot_gen = 0;
        int alloc_ok = toka_wait_registry_allocate(tid, gen, 1, &wait_id, &slot_gen);
        assert(alloc_ok == 1);

        // 1. Verify stale token with wrong generation is rejected immediately
        int stale_res = toka_wait_registry_try_wake(wait_id, slot_gen + 999);
        assert(stale_res == 0); // Must be rejected!

        // 2. Race 4 threads calling try_wake concurrently
        RaceContext ctx = { .wait_id = wait_id, .slot_gen = slot_gen, .winner_count = 0 };
        pthread_t threads[4];
        for (int i = 0; i < 4; i++) {
            pthread_create(&threads[i], NULL, wake_worker, &ctx);
        }
        for (int i = 0; i < 4; i++) {
            pthread_join(threads[i], NULL);
        }

        // At most 1 winner across all 4 threads
        assert(ctx.winner_count <= 1);

        if (ctx.winner_count == 1) {
            // Singleton auto-released on wake success; subsequent release returns 0
            int rel_after_wake = toka_wait_registry_release(wait_id, slot_gen);
            assert(rel_after_wake == 0);
        } else {
            int rel_ok = toka_wait_registry_release(wait_id, slot_gen);
            assert(rel_ok == 1);
        }

        // 4. Verify old token is rejected after release
        int old_wake = toka_wait_registry_try_wake(wait_id, slot_gen);
        assert(old_wake == 0);

        // Pop the scheduled task from ready queue to clean up ref_count
        uint64_t final_id = 0, final_gen = 0;
        void *final_tcb = NULL;
        int final_popped = toka_task_pop_ready(&final_id, &final_gen, &final_tcb);
        if (final_popped) {
            toka_task_release(final_tcb);
        }
        toka_task_release(popped_tcb);
        free(dummy_frame);
    }

    printf("PASSED 20,000/20,000 iterations! WaitRegistry Stale Token Rejection & Winner CAS verified!\n");
    return 0;
}
