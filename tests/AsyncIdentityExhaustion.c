#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id,
                                     uint64_t *out_gen);
extern int toka_task_suspend_and_register(void *tcb_ptr);
extern int toka_task_abort_suspend(void *coro_frame);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern uint32_t toka_ready_queue_count(void);
extern int toka_rt_test_set_next_task_id(uint64_t next_id);
extern int toka_rt_test_set_schedule_generation(void *tcb_ptr,
                                                 uint64_t generation);

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "identity exhaustion check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static void test_schedule_generation_exhaustion_is_nontransitioning(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *owner = toka_task_create(frame, NULL);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_rt_test_set_schedule_generation(owner, UINT64_MAX));
    CHECK(!toka_task_prepare_suspend(frame, &task_id, &generation));
    CHECK(!toka_task_suspend_and_register(owner));
    CHECK(toka_ready_queue_count() == 0);
    // The failed preparation leaves Running intact rather than stranding the
    // task in a partially installed suspend state.
    CHECK(toka_rt_test_set_schedule_generation(owner, 1));
    CHECK(toka_task_prepare_suspend(frame, &task_id, &generation));
    CHECK(toka_task_abort_suspend(frame));

    toka_task_clear_current(worker);
    toka_task_release(worker);
    toka_task_release(owner);
    free(frame);
}

static void test_task_identity_exhaustion_installs_nothing(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    CHECK(toka_rt_test_set_next_task_id(UINT64_MAX));
    CHECK(toka_task_create(frame, NULL) == NULL);
    CHECK(toka_ready_queue_count() == 0);
    free(frame);
}

int main(void) {
    test_schedule_generation_exhaustion_is_nontransitioning();
    test_task_identity_exhaustion_installs_nothing();
    puts("async identity exhaustion passed");
    return 0;
}
