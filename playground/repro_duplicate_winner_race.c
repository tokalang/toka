// AR-P4 Duplicate Winner Idempotency Probe
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdatomic.h>

extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen, void **out_tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id, uint64_t *out_schedule_gen);
extern int toka_wait_registry_allocate_pair(uint64_t task_id, uint64_t gen, uint16_t tag1, uint16_t tag2,
                                             uint32_t *out_id1, uint32_t *out_gen1,
                                             uint32_t *out_id2, uint32_t *out_gen2);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_is_winner(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);

int main(void) {
    printf("Starting AR-P4 Duplicate Winner Idempotency Probe...\n");

    int dummy_val = 42;
    void *dummy_frame = &dummy_val;
    void *tcb = toka_task_create(dummy_frame, NULL);
    assert(tcb != NULL);
    toka_task_start(tcb);

    uint64_t popped_id = 0, popped_gen = 0;
    void *popped_tcb = NULL;
    int popped = toka_task_pop_ready(&popped_id, &popped_gen, &popped_tcb);
    assert(popped == 1);

    uint64_t tid = 0, gen = 0;
    int prep_ok = toka_task_prepare_suspend(dummy_frame, &tid, &gen);
    assert(prep_ok && "prepare_suspend failed");

    uint32_t id1 = 0, gen1 = 0, id2 = 0, gen2 = 0;
    // Allocate pair
    int alloc_ok = toka_wait_registry_allocate_pair(tid, gen, 2, 1, &id1, &gen1, &id2, &gen2);
    assert(alloc_ok && "allocate_pair failed");

    // 1. First wake on Token 1
    int wake1 = toka_wait_registry_try_wake(id1, gen1);
    assert(wake1 == 1 && "First wake on Token 1 must succeed");

    int is_win1_before = toka_wait_registry_is_winner(id1, gen1);
    assert(is_win1_before == 1 && "Token 1 must be winner after first wake");

    // 2. Duplicate wake on Token 1 (same winner event delivered again)
    int wake1_dup = toka_wait_registry_try_wake(id1, gen1);
    assert(wake1_dup == 0 && "Duplicate wake must return 0 without scheduling again");

    int is_win1_after = toka_wait_registry_is_winner(id1, gen1);
    printf("winner before duplicate=%d, after duplicate=%d\n", is_win1_before, is_win1_after);
    assert(is_win1_after == 1 && "Token 1 MUST REMAIN WINNER after duplicate delivery!");

    // 3. Late wake on losing Token 2
    int wake2 = toka_wait_registry_try_wake(id2, gen2);
    assert(wake2 == 0 && "Losing token 2 wake must return 0");

    int is_win2 = toka_wait_registry_is_winner(id2, gen2);
    assert(is_win2 == 0 && "Token 2 must not be winner");

    toka_wait_registry_release(id1, gen1);
    toka_wait_registry_release(id2, gen2);

    printf("PASSED! AR-P4 Duplicate Winner Idempotency verified!\n");
    return 0;
}
