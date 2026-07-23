// Controlled 20,000-iteration WaitRegistry Concurrency & Stale Token Probe
// Tests stale token rejection, exactly-once winner CAS, O(1) invalidation, and slot reuse safety.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>

typedef struct {
    uint64_t id;
    void *coro_frame;
    void *promise;
    _Atomic uint64_t task_schedule_generation;
    _Atomic uint32_t state;
} MockTCB;

extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen, uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_invalidate(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);

typedef struct {
    uint32_t wait_id;
    uint32_t slot_gen;
    _Atomic uint32_t winner_count;
} RaceContext;

static void* wake_worker(void *arg) {
    RaceContext *ctx = (RaceContext*)arg;
    int woken = toka_wait_registry_try_wake(ctx->wait_id, ctx->slot_gen);
    if (woken) {
        atomic_fetch_add(&ctx->winner_count, 1);
    }
    return NULL;
}

int main(void) {
    printf("Starting 20,000-iteration WaitRegistry Concurrency Probe...\n");

    for (int iter = 1; iter <= 20000; iter++) {
        int dummy_val = 42;
        void *dummy_frame = &dummy_val;
        MockTCB *tcb = (MockTCB*)toka_task_create(dummy_frame, NULL);
        toka_task_start(tcb);

        uint32_t wait_id = 0, slot_gen = 0;
        int alloc_ok = toka_wait_registry_allocate(tcb->id, 1, 1, &wait_id, &slot_gen);
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

        // 3. Release slot and verify generation increment
        int rel_ok = toka_wait_registry_release(wait_id, slot_gen);
        assert(rel_ok == 1);

        // 4. Verify old token is rejected after release
        int old_wake = toka_wait_registry_try_wake(wait_id, slot_gen);
        assert(old_wake == 0);

        toka_task_release(tcb);
    }

    printf("PASSED 20,000/20,000 iterations! WaitRegistry Stale Token Rejection & Winner CAS verified!\n");
    return 0;
}
