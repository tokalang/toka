#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend(void *coro_frame, uint64_t *out_task_id,
                                     uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_wait_registry_allocate(uint64_t task_id, uint64_t gen,
                                       uint16_t source_tag,
                                       uint32_t *out_wait_id,
                                       uint32_t *out_slot_gen);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern uint32_t toka_ready_queue_count(void);
extern int toka_task_subscribe_completion(void *tcb_ptr, uint32_t wait_id,
                                          uint32_t slot_gen);
extern int toka_task_unsubscribe_completion(void *tcb_ptr, uint32_t wait_id,
                                            uint32_t slot_gen);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

typedef struct {
    void *owner;
    void *worker;
    void *frame;
    uint64_t task_id;
    uint64_t generation;
    uint32_t wait_id;
    uint32_t slot_generation;
} PreparedParent;

typedef struct {
    FakePromise promise;
    void *tcb;
} ChildTask;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "completion subscription check failed: %s (%s:%d)\\n", \
                    #condition, __FILE__, __LINE__);                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

static PreparedParent make_prepared_parent(void) {
    PreparedParent parent = {0};
    parent.frame = calloc(1, 64);
    CHECK(parent.frame != NULL);
    parent.owner = toka_task_create(parent.frame, NULL);
    CHECK(parent.owner != NULL);
    CHECK(toka_task_start(parent.owner));
    CHECK(toka_task_pop_ready(&parent.task_id, &parent.generation,
                              &parent.worker));
    CHECK(parent.worker == parent.owner);
    CHECK(toka_task_prepare_suspend(parent.frame, &parent.task_id,
                                    &parent.generation));
    CHECK(toka_wait_registry_allocate(parent.task_id, parent.generation, 1,
                                      &parent.wait_id,
                                      &parent.slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 1);
    return parent;
}

static ChildTask make_child(void) {
    ChildTask child = {0};
    child.tcb = toka_task_create(NULL, &child.promise);
    CHECK(child.tcb != NULL);
    return child;
}

static void finish_woken_parent(PreparedParent *parent) {
    CHECK(toka_task_commit_suspend(parent->frame));
    void *resumed = NULL;
    uint64_t task_id = 0;
    uint64_t generation = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &resumed));
    CHECK(task_id == parent->task_id);
    CHECK(generation == parent->generation);
    CHECK(toka_ready_queue_count() == 0);
    toka_task_clear_current(resumed);
    toka_task_release(resumed);
    toka_task_release(parent->worker);
    toka_task_release(parent->owner);
    free(parent->frame);
}

static void finish_aborted_parent(PreparedParent *parent) {
    CHECK(toka_task_abort_suspend(parent->frame));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    toka_task_clear_current(parent->worker);
    toka_task_release(parent->worker);
    toka_task_release(parent->owner);
    free(parent->frame);
}

static void test_subscribe_before_terminal(void) {
    PreparedParent parent = make_prepared_parent();
    ChildTask child = make_child();
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    toka_task_complete(&child.promise);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&parent);
    toka_task_release(child.tcb);
}

static void test_terminal_before_subscribe(void) {
    PreparedParent normal_parent = make_prepared_parent();
    ChildTask normal_child = make_child();
    toka_task_complete(&normal_child.promise);
    CHECK(toka_task_subscribe_completion(normal_child.tcb,
                                         normal_parent.wait_id,
                                         normal_parent.slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&normal_parent);
    toka_task_release(normal_child.tcb);

    PreparedParent canceled_parent = make_prepared_parent();
    ChildTask canceled_child = make_child();
    toka_task_complete_canceled(&canceled_child.promise);
    CHECK(toka_task_subscribe_completion(canceled_child.tcb,
                                         canceled_parent.wait_id,
                                         canceled_parent.slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&canceled_parent);
    toka_task_release(canceled_child.tcb);
}

static void test_unsubscribe_wins_before_terminal(void) {
    PreparedParent parent = make_prepared_parent();
    ChildTask child = make_child();
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    CHECK(toka_task_unsubscribe_completion(child.tcb, parent.wait_id,
                                           parent.slot_generation));
    toka_task_complete(&child.promise);
    CHECK(toka_rt_live_wait_registry_count() == 1);
    finish_aborted_parent(&parent);
    toka_task_release(child.tcb);
}

typedef struct {
    ChildTask *child;
    uint32_t wait_id;
    uint32_t slot_generation;
    _Atomic int subscribe_result;
} SubscribeRace;

static void *subscribe_worker(void *arg) {
    SubscribeRace *race = (SubscribeRace *)arg;
    atomic_store(&race->subscribe_result,
                 toka_task_subscribe_completion(race->child->tcb,
                                                 race->wait_id,
                                                 race->slot_generation));
    return NULL;
}

static void *complete_worker(void *arg) {
    SubscribeRace *race = (SubscribeRace *)arg;
    toka_task_complete(&race->child->promise);
    return NULL;
}

static void test_subscribe_terminal_race(void) {
    for (int i = 0; i < 1000; ++i) {
        PreparedParent parent = make_prepared_parent();
        ChildTask child = make_child();
        SubscribeRace race = {
            .child = &child,
            .wait_id = parent.wait_id,
            .slot_generation = parent.slot_generation,
        };
        pthread_t subscribe_thread;
        pthread_t complete_thread;
        CHECK(pthread_create(&subscribe_thread, NULL, subscribe_worker,
                             &race) == 0);
        CHECK(pthread_create(&complete_thread, NULL, complete_worker,
                             &race) == 0);
        CHECK(pthread_join(subscribe_thread, NULL) == 0);
        CHECK(pthread_join(complete_thread, NULL) == 0);
        CHECK(atomic_load(&race.subscribe_result) == 1);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        finish_woken_parent(&parent);
        toka_task_release(child.tcb);
    }
}

int main(void) {
    test_subscribe_before_terminal();
    test_terminal_before_subscribe();
    test_unsubscribe_wins_before_terminal();
    test_subscribe_terminal_race();
    puts("async completion subscription ordering passed");
    return 0;
}
