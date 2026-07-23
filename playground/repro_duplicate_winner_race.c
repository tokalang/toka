// AR-P4 Duplicate Winner Dispatcher Probe
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

enum {
    TOKA_WAKE_STALE = 0,
    TOKA_WAKE_SINGLETON_WON = 1,
    TOKA_WAKE_PAIR_WON = 2,
    TOKA_WAKE_PAIR_DUPLICATE = 3,
    TOKA_WAKE_PAIR_LOST = 4
};

int main(void) {
    printf("Starting AR-P4 Duplicate Winner Dispatcher Integration Probe...\n");

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
    int alloc_ok = toka_wait_registry_allocate_pair(tid, gen, 2, 1, &id1, &gen1, &id2, &gen2);
    assert(alloc_ok && "allocate_pair failed");

    // 1. First wake on Token 1
    int outcome1 = toka_wait_registry_try_wake(id1, gen1);
    assert(outcome1 == TOKA_WAKE_PAIR_WON && "First wake on Token 1 must return TOKA_WAKE_PAIR_WON");

    // 2. Duplicate wake on Token 1 before coroutine resumes (Simulate dispatcher handling)
    int outcome_dup = toka_wait_registry_try_wake(id1, gen1);
    assert(outcome_dup == TOKA_WAKE_PAIR_DUPLICATE && "Duplicate wake must return TOKA_WAKE_PAIR_DUPLICATE");

    // Dispatcher logic check: ONLY release if outcome == TOKA_WAKE_PAIR_LOST (4)
    if (outcome_dup == TOKA_WAKE_PAIR_LOST) {
        toka_wait_registry_release(id1, gen1);
    }

    // 3. Coroutine resumes and queries is_winner
    int is_win1_after_dup = toka_wait_registry_is_winner(id1, gen1);
    printf("winner status after duplicate delivery in dispatcher=%d\n", is_win1_after_dup);
    assert(is_win1_after_dup == 1 && "Token 1 MUST REMAIN WINNER after dispatcher processes duplicate event!");

    // 4. Late wake on losing Token 2 (Simulate dispatcher handling)
    int outcome2 = toka_wait_registry_try_wake(id2, gen2);
    assert(outcome2 == TOKA_WAKE_PAIR_LOST && "Losing token 2 must return TOKA_WAKE_PAIR_LOST");
    if (outcome2 == TOKA_WAKE_PAIR_LOST) {
        toka_wait_registry_release(id2, gen2);
    }

    toka_wait_registry_release(id1, gen1);

    printf("PASSED! AR-P4 Duplicate Winner Dispatcher Integration verified!\n");
    return 0;
}
