#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern void toka_task_pause_next_queue_publication_for_test(void);
extern int toka_task_suspend_and_register(void *tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_id,
                                     uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern void toka_tcb_get_wait_token(void *tcb_ptr, uint64_t *out_id,
                                    uint64_t *out_gen);
extern int toka_task_try_schedule(uint64_t task_id, uint64_t gen);
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
    uint64_t generation;
    int result;
} HelpAttempt;

typedef struct {
    void *owner;
    void *worker;
    void *frame;
    uint64_t task_id;
    uint64_t generation;
} RunningTask;

static void *help_publish_worker(void *arg) {
    HelpAttempt *attempt = (HelpAttempt *)arg;
    attempt->result = toka_task_try_schedule(attempt->task_id,
                                              attempt->generation);
    return NULL;
}

static void help_preempted_publisher_once(void *expected_owner, uint64_t task_id,
                                          uint64_t generation) {
    enum { HELPERS = 8 };
    CHECK(toka_ready_queue_count() == 0);

    pthread_t threads[HELPERS];
    HelpAttempt attempts[HELPERS] = {0};
    for (int i = 0; i < HELPERS; ++i) {
        attempts[i].task_id = task_id;
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
    CHECK(toka_task_try_schedule(task_id, generation) == 0);
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
    toka_task_clear_current(task->worker);
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
        uint64_t generation = 0;
        toka_tcb_get_wait_token(owner, &task_id, &generation);
        CHECK(generation == 1);
        help_preempted_publisher_once(owner, task_id, generation);

        toka_task_release(owner);
        free(frame);
    }
}

static void test_suspended_queue_claim(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        CHECK(toka_task_suspend_and_register(task.worker));
        toka_tcb_get_wait_token(task.owner, &task.task_id, &task.generation);

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_try_schedule(task.task_id, task.generation));
        help_preempted_publisher_once(task.owner, task.task_id, task.generation);
        destroy_running_task(&task);
    }
}

static void test_pending_wake_commit_queue_claim(void) {
    enum { ROUNDS = 100 };
    for (int round = 0; round < ROUNDS; ++round) {
        RunningTask task = make_running_task();
        CHECK(toka_task_prepare_suspend(task.frame, &task.task_id,
                                        &task.generation));
        CHECK(toka_task_try_schedule(task.task_id, task.generation));

        toka_task_pause_next_queue_publication_for_test();
        CHECK(toka_task_commit_suspend(task.frame));
        help_preempted_publisher_once(task.owner, task.task_id, task.generation);
        destroy_running_task(&task);
    }
}

int main(void) {
    test_created_queue_claim();
    test_suspended_queue_claim();
    test_pending_wake_commit_queue_claim();
    puts("async queue publication helping passed");
    return 0;
}
