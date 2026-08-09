#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern void toka_task_pause_next_queue_publication_for_test(void);
extern void toka_wait_set_pause_next_commit_for_test(void);
extern int toka_task_suspend_and_register(void *tcb_ptr);
extern int toka_task_prepare_suspend_token(
    void *coro_frame, uint64_t *out_id, uint64_t *out_instance,
    uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_task_request_cancel(void *tcb_ptr);
extern void toka_tcb_get_wait_token_with_instance(
    void *tcb_ptr, uint64_t *out_id, uint64_t *out_instance,
    uint64_t *out_gen);
extern int toka_task_try_schedule_token(uint64_t task_id,
                                        uint64_t instance, uint64_t gen);
extern int toka_wait_registry_allocate_pair_token(
    uint64_t task_id, uint64_t instance, uint64_t gen, uint16_t tag1,
    uint16_t tag2, uint32_t *out_id1, uint32_t *out_gen1,
    uint32_t *out_id2, uint32_t *out_gen2);
extern int toka_wait_registry_allocate_token(
    uint64_t task_id, uint64_t instance, uint64_t gen, uint16_t source_tag,
    uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_allocate_nway_token(
    uint64_t task_id, uint64_t instance, uint64_t gen, uint16_t tag_base,
    uint32_t count, uint32_t *out_ids, uint32_t *out_gens);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_is_winner(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern uint32_t toka_ready_queue_count(void);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "queue publication check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

typedef struct {
    uint64_t task_id;
    uint64_t task_instance;
    uint64_t generation;
    int result;
} HelpAttempt;

typedef struct {
    void *owner;
    void *worker;
    void *frame;
    uint64_t task_id;
    uint64_t task_instance;
    uint64_t generation;
} RunningTask;

typedef struct {
    void *task;
    uint32_t wait_id;
    uint32_t slot_generation;
    int wake_result;
    int cancel_result;
} WaitSetSelectionRace;

static void *help_publish_worker(void *arg) {
    HelpAttempt *attempt = (HelpAttempt *)arg;
    attempt->result = toka_task_try_schedule_token(
        attempt->task_id, attempt->task_instance, attempt->generation);
    return NULL;
}

static void *select_wait_source_worker(void *arg) {
    WaitSetSelectionRace *race = (WaitSetSelectionRace *)arg;
    race->wake_result = toka_wait_registry_try_wake(
        race->wait_id, race->slot_generation);
    return NULL;
}

static void *select_wait_cancel_worker(void *arg) {
    WaitSetSelectionRace *race = (WaitSetSelectionRace *)arg;
    race->cancel_result = toka_task_request_cancel(race->task);
    return NULL;
}

static void help_preempted_publisher_once(void *expected_owner, uint64_t task_id,
                                          uint64_t task_instance,
                                          uint64_t generation) {
    enum { HELPERS = 8 };
    CHECK(toka_ready_queue_count() == 0);

    pthread_t threads[HELPERS];
    HelpAttempt attempts[HELPERS] = {0};
    for (int i = 0; i < HELPERS; ++i) {
        attempts[i].task_id = task_id;
        attempts[i].task_instance = task_instance;
        attempts[i].generation = generation;
        CHECK(pthread_create(&threads[i], NULL, help_publish_worker,
                             &attempts[i]) == 0);
    }
    for (int i = 0; i < HELPERS; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(attempts[i].result == 1);
    }

    CHECK(toka_ready_queue_count() == 1);
    void *worker = NULL;
    uint64_t popped_id = 0;
    uint64_t popped_generation = 0;
    CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &worker));
    CHECK(worker == expected_owner);
    CHECK(popped_id == task_id);
    CHECK(popped_generation == generation);
    CHECK(toka_ready_queue_count() == 0);

    // A late helper cannot republish an epoch that a worker has claimed.
    CHECK(toka_task_try_schedule_token(task_id, task_instance, generation) == 0);
    CHECK(toka_ready_queue_count() == 0);
    toka_task_clear_current(worker);
    toka_task_release(worker);
}

static RunningTask make_running_task(void) {
    RunningTask task = {0};
    task.frame = calloc(1, 64);
    CHECK(task.frame != NULL);
    task.owner = toka_task_create(task.frame, NULL);
    CHECK(task.owner != NULL);
    CHECK(toka_task_start(task.owner));
    CHECK(toka_task_pop_ready(&task.task_id, &task.generation, &task.worker));
    CHECK(task.worker == task.owner);
    return task;
}

static void destroy_running_task(RunningTask *task) {
    toka_task_release(task->worker);
    toka_task_release(task->owner);
    free(task->frame);
}

static void test_created_queue_claim(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        void *frame = calloc(1, 64);
        CHECK(frame != NULL);
        void *owner = toka_task_create(frame, NULL);
        CHECK(owner != NULL);

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_start(owner));
        uint64_t task_id = 0;
        uint64_t task_instance = 0;
        uint64_t generation = 0;
        toka_tcb_get_wait_token_with_instance(
            owner, &task_id, &task_instance, &generation);
        CHECK(generation == 1);
        help_preempted_publisher_once(owner, task_id, task_instance, generation);

        toka_task_release(owner);
        free(frame);
    }
}

static void test_suspended_queue_claim(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        CHECK(toka_task_suspend_and_register(task.worker));
        // __builtin_coro_suspend returns to the worker before a wake can run
        // this task again, so release this execution-frame pin first.
        toka_task_clear_current(task.worker);
        toka_tcb_get_wait_token_with_instance(
            task.owner, &task.task_id, &task.task_instance, &task.generation);

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_try_schedule_token(
            task.task_id, task.task_instance, task.generation));
        help_preempted_publisher_once(
            task.owner, task.task_id, task.task_instance, task.generation);
        destroy_running_task(&task);
    }
}

static void test_pending_wake_commit_queue_claim(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_task_try_schedule_token(
            task.task_id, task.task_instance, task.generation));

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        help_preempted_publisher_once(
            task.owner, task.task_id, task.task_instance, task.generation);
        destroy_running_task(&task);
    }
}

static void test_wait_set_cancel_after_logical_uninstall(void) {
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
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        CHECK(toka_rt_live_wait_registry_count() == 2);

        // Cancellation first unlinks the complete WaitSet, then the original
        // publisher is preempted before it can insert the selected ticket.
        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_request_cancel(task.owner));
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
        help_preempted_publisher_once(
            task.owner, task.task_id, task.task_instance, task.generation);
        destroy_running_task(&task);
    }
}

static void test_wait_set_source_winner_after_logical_uninstall(void) {
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
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        CHECK(toka_rt_live_wait_registry_count() == 2);

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 2);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_wait_registry_is_winner(first_id, first_generation));
        CHECK(!toka_wait_registry_is_winner(second_id, second_generation));
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 3);
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 4);

        help_preempted_publisher_once(
            task.owner, task.task_id, task.task_instance, task.generation);
        CHECK(toka_wait_registry_release(first_id, first_generation));
        CHECK(toka_wait_registry_release(second_id, second_generation));
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
        destroy_running_task(&task);
    }
}

static void test_wait_set_source_winner_before_commit(void) {
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

        // A source can select the group before the coroutine has committed
        // its suspension. The later commit owns the corresponding ticket.
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 2);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(!toka_wait_registry_is_winner(first_id, first_generation));
        CHECK(toka_wait_registry_is_winner(second_id, second_generation));

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        help_preempted_publisher_once(
            task.owner, task.task_id, task.task_instance, task.generation);
        CHECK(toka_wait_registry_release(first_id, first_generation));
        CHECK(toka_wait_registry_release(second_id, second_generation));
        destroy_running_task(&task);
    }
}

static void test_wait_set_member_release_uninstalls_complete_group(void) {
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
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        CHECK(toka_rt_live_wait_registry_count() == 2);

        // A member handle cannot dismantle one side of a live WaitSet and
        // leave the other event-eligible. It uninstalls the complete group
        // and schedules the suspended parent exactly once.
        CHECK(toka_wait_registry_release(first_id, first_generation));
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
        CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
        CHECK(!toka_wait_registry_release(second_id, second_generation));
        CHECK(toka_ready_queue_count() == 1);

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);

        // The old group is already inactive, so this resumed task may install
        // and roll back a fresh suspension immediately.
        uint64_t next_task_id = 0;
        uint64_t next_instance = 0;
        uint64_t next_generation = 0;
        uint32_t next_wait_id = 0;
        uint32_t next_slot_generation = 0;
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &next_task_id, &next_instance, &next_generation));
        CHECK(toka_wait_registry_allocate_token(
            next_task_id, next_instance, next_generation, 9, &next_wait_id,
            &next_slot_generation));
        CHECK(toka_task_abort_suspend(task.frame));
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_wait_registry_try_wake(next_wait_id,
                                           next_slot_generation) == 0);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);
        destroy_running_task(&task);
    }
}

static void test_nway_wait_set_third_source_wins_once(void) {
    enum { ROUNDS = 100, MEMBERS = 3 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t ids[MEMBERS] = {0};
        uint32_t generations[MEMBERS] = {0};
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_nway_token(
            task.task_id, task.task_instance, task.generation, 1, MEMBERS,
            ids, generations));
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        CHECK(toka_rt_live_wait_registry_count() == MEMBERS);

        CHECK(toka_wait_registry_try_wake(ids[2], generations[2]) == 2);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(!toka_wait_registry_is_winner(ids[0], generations[0]));
        CHECK(!toka_wait_registry_is_winner(ids[1], generations[1]));
        CHECK(toka_wait_registry_is_winner(ids[2], generations[2]));
        CHECK(toka_wait_registry_try_wake(ids[0], generations[0]) == 4);
        CHECK(toka_wait_registry_try_wake(ids[1], generations[1]) == 4);
        CHECK(toka_wait_registry_try_wake(ids[2], generations[2]) == 3);

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);

        // The old physical outcome records still exist, but their committed
        // descriptor no longer owns the active-set link. A resumed parent may
        // therefore install and roll back a fresh wait immediately.
        uint64_t next_task_id = 0;
        uint64_t next_instance = 0;
        uint64_t next_generation = 0;
        uint32_t next_wait_id = 0;
        uint32_t next_slot_generation = 0;
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &next_task_id, &next_instance, &next_generation));
        CHECK(toka_wait_registry_allocate_token(
            next_task_id, next_instance, next_generation, 9, &next_wait_id,
            &next_slot_generation));
        CHECK(toka_task_abort_suspend(task.frame));
        CHECK(toka_wait_registry_try_wake(next_wait_id,
                                           next_slot_generation) == 0);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);

        for (uint32_t i = 0; i < MEMBERS; ++i) {
            CHECK(toka_wait_registry_release(ids[i], generations[i]));
            CHECK(toka_wait_registry_try_wake(ids[i], generations[i]) == 0);
        }
        destroy_running_task(&task);
    }
}

static void test_old_outcomes_cannot_touch_a_new_wait_set(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t old_first_id = 0;
        uint32_t old_first_generation = 0;
        uint32_t old_second_id = 0;
        uint32_t old_second_generation = 0;
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_pair_token(
            task.task_id, task.task_instance, task.generation, 1, 2,
            &old_first_id, &old_first_generation, &old_second_id,
            &old_second_generation));
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);

        CHECK(toka_wait_registry_try_wake(
            old_first_id, old_first_generation) == 2);
        void *first_resume = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation,
                                  &first_resume));
        CHECK(first_resume == task.owner);

        uint64_t new_task_id = 0;
        uint64_t new_instance = 0;
        uint64_t new_generation = 0;
        uint32_t new_first_id = 0;
        uint32_t new_first_generation = 0;
        uint32_t new_second_id = 0;
        uint32_t new_second_generation = 0;
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &new_task_id, &new_instance, &new_generation));
        CHECK(toka_wait_registry_allocate_pair_token(
            new_task_id, new_instance, new_generation, 3, 4, &new_first_id,
            &new_first_generation, &new_second_id, &new_second_generation));
        CHECK(toka_rt_live_wait_registry_count() == 2);

        // The old physical records retain a completed descriptor only. Their
        // late source probes and releases must not observe or dismantle the
        // new active WaitSet on the same parent TCB.
        CHECK(toka_wait_registry_try_wake(
            old_first_id, old_first_generation) == 3);
        CHECK(toka_wait_registry_try_wake(
            old_second_id, old_second_generation) == 4);
        CHECK(toka_wait_registry_release(old_first_id, old_first_generation));
        CHECK(toka_rt_live_wait_registry_count() == 2);
        CHECK(toka_wait_registry_release(old_second_id,
                                         old_second_generation));
        CHECK(toka_rt_live_wait_registry_count() == 2);
        CHECK(toka_ready_queue_count() == 0);

        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(first_resume);
        toka_task_release(first_resume);
        CHECK(toka_wait_registry_try_wake(
            new_second_id, new_second_generation) == 2);
        CHECK(toka_ready_queue_count() == 1);

        void *second_resume = NULL;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation,
                                  &second_resume));
        CHECK(second_resume == task.owner);
        CHECK(popped_id == new_task_id);
        CHECK(popped_generation == new_generation);
        CHECK(toka_wait_registry_is_winner(new_second_id,
                                            new_second_generation));
        CHECK(!toka_wait_registry_is_winner(new_first_id,
                                             new_first_generation));
        CHECK(toka_wait_registry_release(new_first_id, new_first_generation));
        CHECK(toka_wait_registry_release(new_second_id,
                                         new_second_generation));
        toka_task_clear_current(second_resume);
        toka_task_release(second_resume);
        destroy_running_task(&task);
    }
}

static void test_wait_set_pending_winner_is_helpable(void) {
    enum { ROUNDS = 100, MEMBERS = 3 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t ids[MEMBERS] = {0};
        uint32_t generations[MEMBERS] = {0};
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_nway_token(
            task.task_id, task.task_instance, task.generation, 1, MEMBERS,
            ids, generations));
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);

        // The original publisher has made the whole group logically inactive
        // and selected slot 2, but is preempted before WonPending commits.
        toka_wait_set_pause_next_commit_for_test();
        CHECK(toka_wait_registry_try_wake(ids[2], generations[2]) == 2);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_ready_queue_count() == 0);

        // A losing source sees its own inactive physical slot, completes the
        // selected descriptor, and publishes the already-selected parent.
        CHECK(toka_wait_registry_try_wake(ids[0], generations[0]) == 4);
        CHECK(toka_ready_queue_count() == 1);
        CHECK(toka_wait_registry_is_winner(ids[2], generations[2]));
        CHECK(!toka_wait_registry_is_winner(ids[0], generations[0]));

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);

        for (uint32_t i = 0; i < MEMBERS; ++i) {
            CHECK(toka_wait_registry_release(ids[i], generations[i]));
            CHECK(toka_wait_registry_try_wake(ids[i], generations[i]) == 0);
        }
        destroy_running_task(&task);
    }
}

static void test_wait_set_pending_cancel_commits_selected_descriptor(void) {
    enum { ROUNDS = 100, MEMBERS = 3 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t ids[MEMBERS] = {0};
        uint32_t generations[MEMBERS] = {0};
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_nway_token(
            task.task_id, task.task_instance, task.generation, 1, MEMBERS,
            ids, generations));
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);

        toka_wait_set_pause_next_commit_for_test();
        CHECK(toka_wait_registry_try_wake(ids[1], generations[1]) == 2);
        CHECK(toka_ready_queue_count() == 0);

        // A task-level cancellation request must help the already-selected
        // source; it cannot publish a second or descriptor-less wake.
        CHECK(toka_task_request_cancel(task.owner));
        CHECK(toka_ready_queue_count() == 1);
        CHECK(toka_wait_registry_is_winner(ids[1], generations[1]));
        CHECK(!toka_wait_registry_is_winner(ids[0], generations[0]));
        CHECK(!toka_wait_registry_is_winner(ids[2], generations[2]));

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);

        for (uint32_t i = 0; i < MEMBERS; ++i) {
            CHECK(toka_wait_registry_release(ids[i], generations[i]));
        }
        destroy_running_task(&task);
    }
}

static void test_wait_set_pending_commit_suspend_helps_descriptor(void) {
    enum { ROUNDS = 100, MEMBERS = 3 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        uint32_t ids[MEMBERS] = {0};
        uint32_t generations[MEMBERS] = {0};
        CHECK(toka_task_prepare_suspend_token(
            task.frame, &task.task_id, &task.task_instance, &task.generation));
        CHECK(toka_wait_registry_allocate_nway_token(
            task.task_id, task.task_instance, task.generation, 1, MEMBERS,
            ids, generations));

        // A source can win before the coroutine publishes its suspension. The
        // commit path must first finish that immutable descriptor, then use
        // the one PreparingWithPendingWake queue ticket.
        toka_wait_set_pause_next_commit_for_test();
        CHECK(toka_wait_registry_try_wake(ids[2], generations[2]) == 2);
        CHECK(toka_ready_queue_count() == 0);
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);
        CHECK(toka_ready_queue_count() == 1);
        CHECK(toka_wait_registry_is_winner(ids[2], generations[2]));

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);

        for (uint32_t i = 0; i < MEMBERS; ++i) {
            CHECK(toka_wait_registry_release(ids[i], generations[i]));
        }
        destroy_running_task(&task);
    }
}

static void test_wait_set_source_cancel_race_has_one_selection(void) {
    enum { ROUNDS = 200 };
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
        CHECK(toka_task_commit_suspend(task.frame));
        toka_task_clear_current(task.worker);

        WaitSetSelectionRace race = {
            .task = task.owner,
            .wait_id = first_id,
            .slot_generation = first_generation,
        };
        pthread_t source_thread;
        pthread_t cancel_thread;
        CHECK(pthread_create(&source_thread, NULL, select_wait_source_worker,
                             &race) == 0);
        CHECK(pthread_create(&cancel_thread, NULL, select_wait_cancel_worker,
                             &race) == 0);
        CHECK(pthread_join(source_thread, NULL) == 0);
        CHECK(pthread_join(cancel_thread, NULL) == 0);
        CHECK(race.cancel_result == 1);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_ready_queue_count() == 1);

        if (race.wake_result == 2) {
            // The source owns an irreversible completed descriptor; a later
            // cancellation request cannot rewrite its outcome.
            CHECK(toka_wait_registry_is_winner(first_id, first_generation));
            CHECK(!toka_wait_registry_is_winner(second_id, second_generation));
            CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 3);
            CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 4);
        } else {
            CHECK(race.wake_result == 0);
            CHECK(toka_wait_registry_try_wake(first_id, first_generation) == 0);
            CHECK(toka_wait_registry_try_wake(second_id, second_generation) == 0);
        }

        void *resumed = NULL;
        uint64_t popped_id = 0;
        uint64_t popped_generation = 0;
        CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &resumed));
        CHECK(resumed == task.owner);
        CHECK(popped_id == task.task_id);
        CHECK(popped_generation == task.generation);
        CHECK(toka_ready_queue_count() == 0);
        toka_task_clear_current(resumed);
        toka_task_release(resumed);

        if (race.wake_result == 2) {
            CHECK(toka_wait_registry_release(first_id, first_generation));
            CHECK(toka_wait_registry_release(second_id, second_generation));
        }
        destroy_running_task(&task);
    }
}

int main(void) {
    test_created_queue_claim();
    test_suspended_queue_claim();
    test_pending_wake_commit_queue_claim();
    test_wait_set_cancel_after_logical_uninstall();
    test_wait_set_source_winner_after_logical_uninstall();
    test_wait_set_source_winner_before_commit();
    test_wait_set_member_release_uninstalls_complete_group();
    test_nway_wait_set_third_source_wins_once();
    test_old_outcomes_cannot_touch_a_new_wait_set();
    test_wait_set_pending_winner_is_helpable();
    test_wait_set_pending_cancel_commits_selected_descriptor();
    test_wait_set_pending_commit_suspend_helps_descriptor();
    test_wait_set_source_cancel_race_has_one_selection();
    puts("async queue publication helping passed");
    return 0;
}
