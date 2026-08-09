#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern void toka_tcb_get_wait_token_with_instance(
    void *tcb_ptr, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_generation);
extern int toka_task_prepare_suspend_token(
    void *coro_frame, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_gen);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_task_try_schedule_token(uint64_t task_id,
                                        uint64_t instance_generation,
                                        uint64_t gen);
extern void toka_wait_set_pause_next_commit_for_test(void);
extern void toka_wait_set_fail_next_create_for_test(void);
extern int toka_wait_registry_allocate_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_allocate_pair_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t tag1, uint16_t tag2, uint32_t *out_id1, uint32_t *out_gen1,
    uint32_t *out_id2, uint32_t *out_gen2);
extern int toka_wait_registry_allocate_nway_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t tag_base, uint32_t count, uint32_t *out_ids,
    uint32_t *out_slot_generations);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_is_winner(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
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
    uint64_t task_id;
    uint64_t task_instance;
    uint64_t generation;
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
    task.task_id = task_id;
    task.generation = generation;
    toka_tcb_get_wait_token_with_instance(
        task.owner, &task.task_id, &task.task_instance, &task.generation);
    return task;
}

static void destroy_running_task(RunningTask *task) {
    toka_task_clear_current(task->worker);
    toka_task_release(task->worker);
    toka_task_release(task->owner);
    free(task->frame);
}

static void assert_repreparable(RunningTask *task) {
    CHECK(toka_task_prepare_suspend_token(
        task->frame, &task->task_id, &task->task_instance, &task->generation));
    CHECK(toka_task_abort_suspend(task->frame));
}

static void test_singleton_registration_rollback(void) {
    RunningTask task = make_running_task();
    uint32_t wait_id = 0;
    uint32_t slot_generation = 0;

    CHECK(toka_task_prepare_suspend_token(
        task.frame, &task.task_id, &task.task_instance, &task.generation));
    CHECK(toka_wait_registry_allocate_token(
        task.task_id, task.task_instance, task.generation, 1, &wait_id,
        &slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 1);
    uint32_t pair_id1 = 0;
    uint32_t pair_gen1 = 0;
    uint32_t pair_id2 = 0;
    uint32_t pair_gen2 = 0;
    // A second registration for the same preparation must fail before it
    // overwrites the first active WaitSet link.
    CHECK(!toka_wait_registry_allocate_pair_token(
        task.task_id, task.task_instance, task.generation, 2, 3, &pair_id1,
        &pair_gen1, &pair_id2, &pair_gen2));
    CHECK(toka_rt_live_wait_registry_count() == 1);
    CHECK(toka_task_abort_suspend(task.frame));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_wait_registry_try_wake(wait_id, slot_generation) == 0);
    assert_repreparable(&task);
    destroy_running_task(&task);
}

static void test_pending_pair_registration_rollback(void) {
    RunningTask task = make_running_task();
    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    uint32_t second_id = 0;
    uint32_t second_generation = 0;

    CHECK(toka_task_prepare_suspend_token(
        task.frame, &task.task_id, &task.task_instance, &task.generation));
    CHECK(toka_wait_registry_allocate_pair_token(
        task.task_id, task.task_instance, task.generation, 1, 2, &first_id,
        &first_generation, &second_id, &second_generation));
    CHECK(toka_rt_live_wait_registry_count() == 2);
    uint32_t extra_id = 0;
    uint32_t extra_generation = 0;
    CHECK(!toka_wait_registry_allocate_token(
        task.task_id, task.task_instance, task.generation, 3, &extra_id,
        &extra_generation));
    CHECK(toka_rt_live_wait_registry_count() == 2);

    // A wake can win while registration is still being prepared. Rollback
    // must still invalidate the pair and return to Running without queueing a
    // never-committed suspension attempt.
    CHECK(toka_task_try_schedule_token(
        task.task_id, task.task_instance, task.generation));
    CHECK(toka_task_abort_suspend(task.frame));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
    CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
    assert_repreparable(&task);
    destroy_running_task(&task);
}

static void test_wait_set_create_failure_preserves_preparation(void) {
    RunningTask task = make_running_task();
    uint32_t first_id = UINT32_MAX;
    uint32_t first_generation = UINT32_MAX;
    uint32_t second_id = UINT32_MAX;
    uint32_t second_generation = UINT32_MAX;

    CHECK(toka_task_prepare_suspend_token(
        task.frame, &task.task_id, &task.task_instance, &task.generation));
    toka_wait_set_fail_next_create_for_test();
    CHECK(!toka_wait_registry_allocate_pair_token(
        task.task_id, task.task_instance, task.generation, 1, 2, &first_id,
        &first_generation, &second_id, &second_generation));

    // Descriptor creation fails before the pair is installed: no slot, wake,
    // or output token becomes visible. The caller can roll back its existing
    // preparation and retry normally.
    CHECK(first_id == UINT32_MAX);
    CHECK(first_generation == UINT32_MAX);
    CHECK(second_id == UINT32_MAX);
    CHECK(second_generation == UINT32_MAX);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_task_abort_suspend(task.frame));
    assert_repreparable(&task);
    destroy_running_task(&task);
}

static void test_nway_create_failure_preserves_outputs(void) {
    enum { MEMBERS = 3 };
    RunningTask task = make_running_task();
    uint32_t ids[MEMBERS] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t generations[MEMBERS] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    CHECK(toka_task_prepare_suspend_token(
        task.frame, &task.task_id, &task.task_instance, &task.generation));
    toka_wait_set_fail_next_create_for_test();
    CHECK(!toka_wait_registry_allocate_nway_token(
        task.task_id, task.task_instance, task.generation, 1, MEMBERS, ids,
        generations));

    // N-way discovery is private until the descriptor exists. A failed
    // installation cannot hand the caller plausible-but-stale slot numbers.
    for (uint32_t i = 0; i < MEMBERS; ++i) {
        CHECK(ids[i] == UINT32_MAX);
        CHECK(generations[i] == UINT32_MAX);
    }
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_task_abort_suspend(task.frame));
    assert_repreparable(&task);
    destroy_running_task(&task);
}

static void test_selected_pair_rollback_preserves_winner(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t first_id = 0;
        uint32_t first_generation = 0;
        uint32_t second_id = 0;
        uint32_t second_generation = 0;

        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_pair_token(
            task.task_id, task.task_instance, task.generation, 1, 2, &first_id,
            &first_generation, &second_id, &second_generation));

        // The source has selected the group but is preempted before it can
        // commit. Abort must complete that immutable descriptor, restore the
        // current task locally, and retain the outcome for the caller.
        toka_wait_set_pause_next_commit_for_test();
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 2);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_ready_queue_count() == 0);
        CHECK(toka_task_abort_suspend(task.frame));
        CHECK(toka_ready_queue_count() == 0);
        CHECK(!toka_wait_registry_is_winner(first_id, first_generation));
        CHECK(toka_wait_registry_is_winner(second_id, second_generation));

        CHECK(toka_wait_registry_release(first_id, first_generation));
        CHECK(toka_wait_registry_release(second_id, second_generation));
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
        assert_repreparable(&task);
        destroy_running_task(&task);
    }
}

int main(void) {
    test_singleton_registration_rollback();
    test_pending_pair_registration_rollback();
    test_wait_set_create_failure_preserves_preparation();
    test_nway_create_failure_preserves_outputs();
    test_selected_pair_rollback_preserves_winner();
    puts("async suspend rollback registration cleanup passed");
    return 0;
}
