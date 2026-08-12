#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend_token(
    void *coro_frame, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_gen);
extern int toka_task_commit_suspend(void *coro_frame);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_wait_registry_allocate_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_allocate_pair_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t first_source_tag, uint16_t second_source_tag,
    uint32_t *out_first_wait_id, uint32_t *out_first_slot_gen,
    uint32_t *out_second_wait_id, uint32_t *out_second_slot_gen);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
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
extern int toka_task_try_retain(void *tcb_ptr);
extern int toka_task_request_cancel(void *tcb_ptr);
extern uint32_t toka_rt_live_tcb_count(void);
extern int toka_task_await_prepare(void *child_promise_ptr,
                                   void *parent_tcb_ptr);
extern int toka_task_resolve_await(void *child_promise_ptr,
                                   void *parent_tcb_ptr);
extern void toka_task_finish_await_resolution(void *parent_tcb_ptr);
extern void toka_rt_test_pause_terminal_after_publish(void);
extern int toka_rt_test_terminal_publish_paused(void);
extern void toka_rt_test_resume_terminal_publish(void);

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
    uint64_t task_instance;
    uint64_t generation;
    uint32_t wait_id;
    uint32_t slot_generation;
} PreparedParent;

typedef struct {
    void *owner;
    void *worker;
    void *frame;
    uint64_t task_id;
    uint64_t task_instance;
    uint64_t generation;
    uint32_t first_wait_id;
    uint32_t first_slot_generation;
    uint32_t second_wait_id;
    uint32_t second_slot_generation;
} PreparedPairParent;

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
    CHECK(toka_task_prepare_suspend_token(
        parent.frame, &parent.task_id, &parent.task_instance,
        &parent.generation));
    CHECK(toka_wait_registry_allocate_token(
        parent.task_id, parent.task_instance, parent.generation, 1,
        &parent.wait_id, &parent.slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 1);
    // The coroutine has now reached its suspend boundary. Model the worker
    // loop returning from that resume before a later wake can pop it again.
    toka_task_clear_current(parent.worker);
    return parent;
}

static void make_child(ChildTask *child) {
    *child = (ChildTask){0};
    child->tcb = toka_task_create(NULL, &child->promise);
    CHECK(child->tcb != NULL);
}

static PreparedPairParent make_prepared_pair_parent(void) {
    PreparedPairParent parent = {0};
    parent.frame = calloc(1, 64);
    CHECK(parent.frame != NULL);
    parent.owner = toka_task_create(parent.frame, NULL);
    CHECK(parent.owner != NULL);
    CHECK(toka_task_start(parent.owner));
    CHECK(toka_task_pop_ready(&parent.task_id, &parent.generation,
                              &parent.worker));
    CHECK(parent.worker == parent.owner);
    CHECK(toka_task_prepare_suspend_token(
        parent.frame, &parent.task_id, &parent.task_instance,
        &parent.generation));
    CHECK(toka_wait_registry_allocate_pair_token(
        parent.task_id, parent.task_instance, parent.generation, 1, 2,
        &parent.first_wait_id, &parent.first_slot_generation,
        &parent.second_wait_id, &parent.second_slot_generation));
    CHECK(toka_task_commit_suspend(parent.frame));
    toka_task_clear_current(parent.worker);
    CHECK(toka_rt_live_wait_registry_count() == 2);
    return parent;
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
    toka_task_release(parent->worker);
    toka_task_release(parent->owner);
    free(parent->frame);
}

static void finish_woken_pair_parent(PreparedPairParent *parent,
                                     int release_outcome_slots) {
    void *resumed = NULL;
    uint64_t task_id = 0;
    uint64_t generation = 0;
    CHECK(toka_task_pop_ready(&task_id, &generation, &resumed));
    CHECK(resumed == parent->owner);
    CHECK(task_id == parent->task_id);
    CHECK(generation == parent->generation);
    toka_task_clear_current(resumed);
    if (release_outcome_slots) {
        CHECK(toka_wait_registry_release(parent->first_wait_id,
                                         parent->first_slot_generation));
        CHECK(toka_wait_registry_release(parent->second_wait_id,
                                         parent->second_slot_generation));
    }
    toka_task_release(resumed);
    toka_task_release(parent->worker);
    toka_task_release(parent->owner);
    free(parent->frame);
}

static void test_subscribe_before_terminal(void) {
    PreparedParent parent = make_prepared_parent();
    ChildTask child;
    make_child(&child);
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    toka_task_complete(&child.promise);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&parent);
    toka_task_release(child.tcb);
}

static void test_subscription_retain_survives_handle_release(void) {
    PreparedParent parent = make_prepared_parent();
    ChildTask child;
    make_child(&child);
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    // The subscription owns the remaining checked child reference. Its
    // terminal publisher must therefore remain valid after the external
    // owner releases the handle reference.
    toka_task_release(child.tcb);
    toka_task_complete(&child.promise);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&parent);
}

static void test_terminal_before_subscribe(void) {
    PreparedParent normal_parent = make_prepared_parent();
    ChildTask normal_child;
    make_child(&normal_child);
    toka_task_complete(&normal_child.promise);
    CHECK(toka_task_subscribe_completion(normal_child.tcb,
                                         normal_parent.wait_id,
                                         normal_parent.slot_generation));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    finish_woken_parent(&normal_parent);
    toka_task_release(normal_child.tcb);

    PreparedParent canceled_parent = make_prepared_parent();
    ChildTask canceled_child;
    make_child(&canceled_child);
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
    ChildTask child;
    make_child(&child);
    CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                         parent.slot_generation));
    CHECK(toka_task_unsubscribe_completion(child.tcb, parent.wait_id,
                                           parent.slot_generation));
    toka_task_complete(&child.promise);
    CHECK(toka_rt_live_wait_registry_count() == 1);
    finish_aborted_parent(&parent);
    toka_task_release(child.tcb);
}

static void test_other_group_winner_unsubscribes_child(void) {
    PreparedPairParent parent = make_prepared_pair_parent();
    ChildTask child;
    make_child(&child);
    CHECK(toka_task_subscribe_completion(child.tcb, parent.first_wait_id,
                                         parent.first_slot_generation));

    // The descriptor is currently the only child retain. Selecting the other
    // member must unlink it before the parent runs; it may not keep a
    // nonterminal child alive until a later, unrelated terminal publication.
    toka_task_release(child.tcb);
    CHECK(toka_wait_registry_try_wake(parent.second_wait_id,
                                      parent.second_slot_generation) == 2);
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(!toka_task_try_retain(child.tcb));
    finish_woken_pair_parent(&parent, 1);
}

static void test_parent_cancel_unsubscribes_child(void) {
    PreparedPairParent parent = make_prepared_pair_parent();
    ChildTask child;
    make_child(&child);
    CHECK(toka_task_subscribe_completion(child.tcb, parent.first_wait_id,
                                         parent.first_slot_generation));

    toka_task_release(child.tcb);
    CHECK(toka_task_request_cancel(parent.owner));
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(!toka_task_try_retain(child.tcb));
    finish_woken_pair_parent(&parent, 0);
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
        ChildTask child;
        make_child(&child);
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

typedef struct {
    ChildTask *child;
    uint32_t wait_id;
    uint32_t slot_generation;
    _Atomic int unsubscribe_result;
} UnsubscribeRace;

typedef struct {
    ChildTask *child;
    uint32_t other_wait_id;
    uint32_t other_slot_generation;
    _Atomic int wake_result;
} GroupTerminalRace;

static void *unsubscribe_worker(void *arg) {
    UnsubscribeRace *race = (UnsubscribeRace *)arg;
    atomic_store(&race->unsubscribe_result,
                 toka_task_unsubscribe_completion(race->child->tcb,
                                                   race->wait_id,
                                                   race->slot_generation));
    return NULL;
}

static void test_terminal_unsubscribe_race(void) {
    for (int i = 0; i < 1000; ++i) {
        PreparedParent parent = make_prepared_parent();
        ChildTask child;
        make_child(&child);
        CHECK(toka_task_subscribe_completion(child.tcb, parent.wait_id,
                                             parent.slot_generation));
        UnsubscribeRace race = {
            .child = &child,
            .wait_id = parent.wait_id,
            .slot_generation = parent.slot_generation,
        };
        pthread_t unsubscribe_thread;
        pthread_t complete_thread;
        CHECK(pthread_create(&unsubscribe_thread, NULL, unsubscribe_worker,
                             &race) == 0);
        CHECK(pthread_create(&complete_thread, NULL, complete_worker,
                             &race) == 0);
        CHECK(pthread_join(unsubscribe_thread, NULL) == 0);
        CHECK(pthread_join(complete_thread, NULL) == 0);
        if (atomic_load(&race.unsubscribe_result)) {
            CHECK(toka_rt_live_wait_registry_count() == 1);
            finish_aborted_parent(&parent);
        } else {
            CHECK(toka_rt_live_wait_registry_count() == 0);
            finish_woken_parent(&parent);
        }
        toka_task_release(child.tcb);
    }
}

static void *complete_group_child_worker(void *arg) {
    GroupTerminalRace *race = (GroupTerminalRace *)arg;
    toka_task_complete(&race->child->promise);
    return NULL;
}

static void *wake_other_group_source_worker(void *arg) {
    GroupTerminalRace *race = (GroupTerminalRace *)arg;
    atomic_store(&race->wake_result,
                 toka_wait_registry_try_wake(race->other_wait_id,
                                             race->other_slot_generation));
    return NULL;
}

static void test_child_terminal_vs_other_group_winner(void) {
    for (int i = 0; i < 1000; ++i) {
        PreparedPairParent parent = make_prepared_pair_parent();
        ChildTask child;
        make_child(&child);
        CHECK(toka_task_subscribe_completion(child.tcb,
                                             parent.first_wait_id,
                                             parent.first_slot_generation));
        GroupTerminalRace race = {
            .child = &child,
            .other_wait_id = parent.second_wait_id,
            .other_slot_generation = parent.second_slot_generation,
        };
        pthread_t terminal_thread;
        pthread_t source_thread;
        CHECK(pthread_create(&terminal_thread, NULL,
                             complete_group_child_worker, &race) == 0);
        CHECK(pthread_create(&source_thread, NULL,
                             wake_other_group_source_worker, &race) == 0);
        CHECK(pthread_join(terminal_thread, NULL) == 0);
        CHECK(pthread_join(source_thread, NULL) == 0);

        // One source owns the group. The losing source has no second wake or
        // orphaned completion descriptor to leave behind.
        CHECK(atomic_load(&race.wake_result) == 2 ||
              atomic_load(&race.wake_result) == 4);
        CHECK(toka_rt_live_wait_registry_count() == 0);
        finish_woken_pair_parent(&parent, 1);
        toka_task_release(child.tcb);
    }
}

typedef struct {
    PreparedPairParent *parent;
    ChildTask *child;
} ParentCancelTerminalRace;

static void *cancel_parent_worker(void *arg) {
    ParentCancelTerminalRace *race = (ParentCancelTerminalRace *)arg;
    toka_task_request_cancel(race->parent->owner);
    return NULL;
}

static void test_parent_cancel_vs_child_terminal(void) {
    for (int i = 0; i < 1000; ++i) {
        PreparedPairParent parent = make_prepared_pair_parent();
        ChildTask child;
        make_child(&child);
        CHECK(toka_task_subscribe_completion(child.tcb, parent.first_wait_id,
                                             parent.first_slot_generation));
        ParentCancelTerminalRace race = {
            .parent = &parent,
            .child = &child,
        };
        pthread_t cancel_thread;
        pthread_t terminal_thread;
        CHECK(pthread_create(&cancel_thread, NULL, cancel_parent_worker,
                             &race) == 0);
        CHECK(pthread_create(&terminal_thread, NULL,
                             complete_group_child_worker, &race) == 0);
        CHECK(pthread_join(cancel_thread, NULL) == 0);
        CHECK(pthread_join(terminal_thread, NULL) == 0);

        // Parent cancellation and child terminal publication may race, but
        // they share the same parent WaitSet: exactly one ready ticket is
        // observable and the descriptor-held child reference is released.
        CHECK(toka_rt_live_wait_registry_count() == 0);
        CHECK(toka_ready_queue_count() == 1);

        void *resumed = NULL;
        uint64_t task_id = 0;
        uint64_t generation = 0;
        CHECK(toka_task_pop_ready(&task_id, &generation, &resumed));
        CHECK(resumed == parent.owner);
        CHECK(task_id == parent.task_id);
        CHECK(generation == parent.generation);
        toka_task_clear_current(resumed);

        // A child-terminal winner retains the two inactive outcome slots for
        // explicit release; a cancellation winner already reclaimed both.
        int first_released = toka_wait_registry_release(
            parent.first_wait_id, parent.first_slot_generation
        );
        int second_released = toka_wait_registry_release(
            parent.second_wait_id, parent.second_slot_generation
        );
        CHECK(first_released == second_released);

        toka_task_release(resumed);
        toka_task_release(parent.worker);
        toka_task_release(parent.owner);
        toka_task_release(child.tcb);
        free(parent.frame);
        CHECK(toka_rt_live_tcb_count() == 0);
    }
}

typedef struct {
    FakePromise *promise;
} AwaitTerminalRace;

static void *complete_awaited_child_worker(void *arg) {
    AwaitTerminalRace *race = (AwaitTerminalRace *)arg;
    toka_task_complete(race->promise);
    return NULL;
}

static void test_parent_cancel_waits_for_await_terminal(void) {
    void *parent_frame = calloc(1, 64);
    CHECK(parent_frame != NULL);
    void *parent = toka_task_create(parent_frame, NULL);
    CHECK(parent != NULL);
    CHECK(toka_task_start(parent));

    uint64_t parent_id = 0;
    uint64_t parent_generation = 0;
    void *parent_worker = NULL;
    CHECK(toka_task_pop_ready(&parent_id, &parent_generation, &parent_worker));
    CHECK(parent_worker == parent);

    FakePromise child_promise = {0};
    void *child = toka_task_create(NULL, &child_promise);
    CHECK(child != NULL);
    CHECK(toka_task_await_prepare(&child_promise, parent));
    toka_task_clear_current(parent_worker);

    toka_rt_test_pause_terminal_after_publish();
    AwaitTerminalRace race = { .promise = &child_promise };
    pthread_t terminal_thread;
    CHECK(pthread_create(&terminal_thread, NULL,
                         complete_awaited_child_worker, &race) == 0);
    for (int i = 0; i < 1000000 &&
                    !toka_rt_test_terminal_publish_paused();
         ++i) {
        sched_yield();
    }
    CHECK(toka_rt_test_terminal_publish_paused());

    // The child has committed normal terminal publication but its direct
    // await continuation has not yet run. Parent cancellation must defer its
    // own ready ticket until that continuation closes the await link.
    CHECK(toka_task_request_cancel(parent));
    CHECK(toka_task_resolve_await(&child_promise, parent) == -1);
    CHECK(toka_ready_queue_count() == 0);

    toka_rt_test_resume_terminal_publish();
    CHECK(pthread_join(terminal_thread, NULL) == 0);
    CHECK(toka_ready_queue_count() == 1);

    void *resumed_parent = NULL;
    uint64_t resumed_id = 0;
    uint64_t resumed_generation = 0;
    CHECK(toka_task_pop_ready(&resumed_id, &resumed_generation,
                              &resumed_parent));
    CHECK(resumed_parent == parent);
    CHECK(resumed_id == parent_id);
    CHECK(resumed_generation == parent_generation + 1);
    toka_task_clear_current(resumed_parent);
    CHECK(toka_task_resolve_await(&child_promise, parent) == -1);
    toka_task_finish_await_resolution(parent);

    toka_task_release(resumed_parent);
    toka_task_release(parent_worker);
    toka_task_release(parent);
    toka_task_release(child);
    free(parent_frame);
    CHECK(toka_rt_live_tcb_count() == 0);
}

static void test_normal_direct_await_claims_once(void) {
    void *parent_frame = calloc(1, 64);
    CHECK(parent_frame != NULL);
    void *parent = toka_task_create(parent_frame, NULL);
    CHECK(parent != NULL);
    CHECK(toka_task_start(parent));

    uint64_t parent_id = 0;
    uint64_t parent_generation = 0;
    void *parent_worker = NULL;
    CHECK(toka_task_pop_ready(&parent_id, &parent_generation, &parent_worker));
    CHECK(parent_worker == parent);

    FakePromise child_promise = {0};
    void *child = toka_task_create(NULL, &child_promise);
    CHECK(child != NULL);
    CHECK(toka_task_await_prepare(&child_promise, parent));
    toka_task_clear_current(parent_worker);
    toka_task_complete(&child_promise);
    CHECK(toka_ready_queue_count() == 1);

    CHECK(toka_task_resolve_await(&child_promise, parent) == 1);
    CHECK(toka_task_resolve_await(&child_promise, parent) == 1);
    CHECK(toka_task_request_cancel(parent));
    CHECK(toka_task_resolve_await(&child_promise, parent) == 1);
    CHECK(toka_ready_queue_count() == 1);
    toka_task_finish_await_resolution(parent);
    CHECK(toka_task_resolve_await(&child_promise, parent) == 0);

    void *resumed_parent = NULL;
    uint64_t resumed_id = 0;
    uint64_t resumed_generation = 0;
    CHECK(toka_task_pop_ready(&resumed_id, &resumed_generation,
                              &resumed_parent));
    CHECK(resumed_parent == parent);
    CHECK(resumed_id == parent_id);
    CHECK(resumed_generation == parent_generation + 1);
    toka_task_clear_current(resumed_parent);

    toka_task_release(resumed_parent);
    toka_task_release(parent_worker);
    toka_task_release(parent);
    toka_task_release(child);
    free(parent_frame);
    CHECK(toka_rt_live_tcb_count() == 0);
}

typedef struct {
    FakePromise *promise;
    void *parent;
} DirectAwaitCancelRace;

static void *complete_direct_await_child_worker(void *arg) {
    DirectAwaitCancelRace *race = (DirectAwaitCancelRace *)arg;
    toka_task_complete(race->promise);
    return NULL;
}

static void *cancel_direct_await_parent_worker(void *arg) {
    DirectAwaitCancelRace *race = (DirectAwaitCancelRace *)arg;
    toka_task_request_cancel(race->parent);
    return NULL;
}

static void test_direct_await_cancel_wins_before_normal_claim(void) {
    for (int i = 0; i < 1000; ++i) {
        void *parent_frame = calloc(1, 64);
        CHECK(parent_frame != NULL);
        void *parent = toka_task_create(parent_frame, NULL);
        CHECK(parent != NULL);
        CHECK(toka_task_start(parent));

        uint64_t parent_id = 0;
        uint64_t parent_generation = 0;
        void *parent_worker = NULL;
        CHECK(toka_task_pop_ready(&parent_id, &parent_generation,
                                  &parent_worker));
        CHECK(parent_worker == parent);

        FakePromise child_promise = {0};
        void *child = toka_task_create(NULL, &child_promise);
        CHECK(child != NULL);
        CHECK(toka_task_await_prepare(&child_promise, parent));
        toka_task_clear_current(parent_worker);

        DirectAwaitCancelRace race = {
            .promise = &child_promise,
            .parent = parent,
        };
        pthread_t terminal_thread;
        pthread_t cancel_thread;
        CHECK(pthread_create(&terminal_thread, NULL,
                             complete_direct_await_child_worker, &race) == 0);
        CHECK(pthread_create(&cancel_thread, NULL,
                             cancel_direct_await_parent_worker, &race) == 0);
        CHECK(pthread_join(terminal_thread, NULL) == 0);
        CHECK(pthread_join(cancel_thread, NULL) == 0);

        CHECK(toka_task_resolve_await(&child_promise, parent) == -1);
        CHECK(toka_ready_queue_count() == 1);

        void *resumed_parent = NULL;
        uint64_t resumed_id = 0;
        uint64_t resumed_generation = 0;
        CHECK(toka_task_pop_ready(&resumed_id, &resumed_generation,
                                  &resumed_parent));
        CHECK(resumed_parent == parent);
        CHECK(resumed_id == parent_id);
        CHECK(resumed_generation == parent_generation + 1);
        toka_task_clear_current(resumed_parent);

        toka_task_finish_await_resolution(parent);
        toka_task_release(resumed_parent);
        toka_task_release(parent_worker);
        toka_task_release(parent);
        toka_task_release(child);
        free(parent_frame);
        CHECK(toka_rt_live_tcb_count() == 0);
    }
}

int main(void) {
    test_subscribe_before_terminal();
    test_subscription_retain_survives_handle_release();
    test_terminal_before_subscribe();
    test_unsubscribe_wins_before_terminal();
    test_other_group_winner_unsubscribes_child();
    test_parent_cancel_unsubscribes_child();
    test_subscribe_terminal_race();
    test_terminal_unsubscribe_race();
    test_child_terminal_vs_other_group_winner();
    test_parent_cancel_vs_child_terminal();
    test_parent_cancel_waits_for_await_terminal();
    test_normal_direct_await_claims_once();
    test_direct_await_cancel_wins_before_normal_claim();
    puts("async completion subscription ordering passed");
    return 0;
}
