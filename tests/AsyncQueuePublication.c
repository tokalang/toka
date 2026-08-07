#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start_unpublished_for_test(void *tcb_ptr);
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

static void *help_publish_worker(void *arg) {
    HelpAttempt *attempt = (HelpAttempt *)arg;
    attempt->result = toka_task_try_schedule(attempt->task_id,
                                              attempt->generation);
    return NULL;
}

static void test_preempted_queue_publisher_is_helped_once(void) {
    enum { HELPERS = 8, ROUNDS = 100 };

    for (int round = 0; round < ROUNDS; ++round) {
        void *frame = calloc(1, 64);
        CHECK(frame != NULL);
        void *owner = toka_task_create(frame, NULL);
        CHECK(owner != NULL);

        // The test-only starter models an initial scheduler that was
        // preempted immediately after Created -> Queued, before publication.
        CHECK(toka_task_start_unpublished_for_test(owner));
        uint64_t task_id = 0;
        uint64_t generation = 0;
        toka_tcb_get_wait_token(owner, &task_id, &generation);
        CHECK(generation == 1);
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
        CHECK(worker == owner);
        CHECK(popped_id == task_id);
        CHECK(popped_generation == generation);
        CHECK(toka_ready_queue_count() == 0);

        // A late helper cannot republish an epoch that a worker has claimed.
        CHECK(toka_task_try_schedule(task_id, generation) == 0);
        CHECK(toka_ready_queue_count() == 0);

        toka_task_clear_current(worker);
        toka_task_release(worker);
        toka_task_release(owner);
        free(frame);
    }
}

int main(void) {
    test_preempted_queue_publisher_is_helped_once();
    puts("async queue publication helping passed");
    return 0;
}
