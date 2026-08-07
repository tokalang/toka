#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id,
                                     uint64_t *out_gen);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_task_try_schedule(uint64_t task_id, uint64_t gen);
extern int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen,
                                       uint16_t source_tag,
                                       uint32_t *out_wait_id,
                                       uint32_t *out_slot_gen);
extern int toka_wait_registry_allocate_pair(uint64_t task_id, uint64_t gen,
                                            uint16_t tag1, uint16_t tag2,
                                            uint32_t *out_id1,
                                            uint32_t *out_gen1,
                                            uint32_t *out_id2,
                                            uint32_t *out_gen2);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern uint32_t toka_ready_queue_count(void);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "suspend rollback check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

typedef struct {
    void *owner;
    void *worker;
    void *frame;
} RunningTask;

static RunningTask make_running_task(void) {
    RunningTask task = {0};
    task.frame = calloc(1, 64);
    CHECK(task.frame != NULL);
    task.owner = toka_task_create(task.frame, NULL);
    CHECK(task.owner != NULL);
    CHECK(toka_task_start(task.owner));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &task.worker));
    CHECK(task.worker == task.owner);
    return task;
}

static void destroy_running_task(RunningTask *task) {
    toka_task_clear_current(task->worker);
    toka_task_release(task->worker);
    toka_task_release(task->owner);
    free(task->frame);
}

static void assert_repreparable(void *frame) {
    uint64_t task_id = 0;
    uint64_t generation = 0;
    CHECK(toka_task_prepare_suspend(frame, &task_id, &generation));
    CHECK(toka_task_abort_suspend(frame));
}

static void test_singleton_registration_rollback(void) {
    RunningTask task = make_running_task();
    uint64_t task_id = 0;
    uint64_t generation = 0;
    uint32_t wait_id = 0;
    uint32_t slot_generation = 0;

    CHECK(toka_task_prepare_suspend(task.frame, &task_id, &generation));
    CHECK(toka_wait_registry_allocate(task_id, generation, 1, &wait_id,
                                      &slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 1);
    CHECK(toka_task_abort_suspend(task.frame));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_wait_registry_try_wake(wait_id, slot_generation) == 0);
    assert_repreparable(task.frame);
    destroy_running_task(&task);
}

static void test_pending_pair_registration_rollback(void) {
    RunningTask task = make_running_task();
    uint64_t task_id = 0;
    uint64_t generation = 0;
    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    uint32_t second_id = 0;
    uint32_t second_generation = 0;

    CHECK(toka_task_prepare_suspend(task.frame, &task_id, &generation));
    CHECK(toka_wait_registry_allocate_pair(
        task_id, generation, 1, 2, &first_id, &first_generation, &second_id,
        &second_generation));
    CHECK(toka_rt_live_wait_registry_count() == 2);

    // A wake can win while registration is still being prepared. Rollback
    // must still invalidate the pair and return to Running without queueing a
    // never-committed suspension attempt.
    CHECK(toka_task_try_schedule(task_id, generation));
    CHECK(toka_task_abort_suspend(task.frame));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
    assert_repreparable(task.frame);
    destroy_running_task(&task);
}

int main(void) {
    test_singleton_registration_rollback();
    test_pending_pair_registration_rollback();
    puts("async suspend rollback registration cleanup passed");
    return 0;
}
