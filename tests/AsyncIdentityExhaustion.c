#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen,
                               void **out_tcb_ptr);
extern int toka_task_prepare_suspend_token(
    void *coro_frame, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_gen);
extern int toka_task_suspend_and_register(void *tcb_ptr);
extern int toka_task_abort_suspend(void *coro_frame);
extern int toka_task_await_prepare(void *child_promise_ptr,
                                   void *parent_tcb_ptr);
extern int toka_task_register_cancel_child(void *parent_frame,
                                           void *child_tcb_ptr);
extern int toka_task_try_retain(void *tcb_ptr);
extern int toka_task_request_cancel(void *tcb_ptr);
extern int toka_task_try_schedule_token(uint64_t task_id,
                                        uint64_t instance_generation,
                                        uint64_t generation);
extern void *toka_task_get_current_coro_frame(void);
extern void toka_tcb_get_wait_token_with_instance(
    void *tcb_ptr, uint64_t *out_task_id, uint64_t *out_instance_generation,
    uint64_t *out_generation);
extern int toka_wait_registry_allocate_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t source_tag, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_wait_registry_allocate_nway_token(
    uint64_t task_id, uint64_t instance_generation, uint64_t gen,
    uint16_t tag_base, uint32_t count, uint32_t *out_ids,
    uint32_t *out_slot_generations);
extern int toka_wait_registry_release(uint32_t wait_id, uint32_t slot_gen);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern void *toka_tcb_get_promise(void *tcb_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);
extern uint32_t toka_ready_queue_count(void);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern int toka_rt_test_set_next_task_id(uint64_t next_id);
extern int toka_rt_test_set_schedule_generation(void *tcb_ptr,
                                                 uint64_t generation);
extern int toka_rt_test_set_tcb_ref_count(void *tcb_ptr, uint32_t ref_count);
extern uint32_t toka_rt_test_get_frame_access_state(void *tcb_ptr);
extern int toka_rt_test_set_wait_slot_generation(uint32_t wait_id,
                                                  uint32_t generation);
extern int toka_rt_test_set_next_wait_set_id(uint64_t next_id);

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
    uint64_t task_instance = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_rt_test_set_schedule_generation(owner, UINT64_MAX));
    CHECK(!toka_task_prepare_suspend_token(
        frame, &task_id, &task_instance, &generation));
    CHECK(!toka_task_suspend_and_register(owner));
    CHECK(toka_ready_queue_count() == 0);
    // The failed preparation leaves Running intact rather than stranding the
    // task in a partially installed suspend state.
    CHECK(toka_rt_test_set_schedule_generation(owner, 1));
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &task_instance, &generation));
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

static void test_reused_numeric_id_rejects_stale_instance_token(void) {
    const uint64_t reused_id = UINT64_C(0x5A5A0001);
    CHECK(toka_rt_test_set_next_task_id(reused_id));
    void *first = toka_task_create(NULL, NULL);
    CHECK(first != NULL);

    uint64_t first_id = 0;
    uint64_t first_instance = 0;
    uint64_t first_generation = 0;
    toka_tcb_get_wait_token_with_instance(
        first, &first_id, &first_instance, &first_generation);
    CHECK(first_id == reused_id);
    CHECK(first_instance != 0);
    toka_task_release(first);

    // Test-only numeric-slot reuse models a future recycled registry slot.
    // The fresh TCB must not make the stale task token authoritative again.
    CHECK(toka_rt_test_set_next_task_id(reused_id));
    void *second = toka_task_create(NULL, NULL);
    CHECK(second != NULL);
    uint64_t second_id = 0;
    uint64_t second_instance = 0;
    uint64_t second_generation = 0;
    toka_tcb_get_wait_token_with_instance(
        second, &second_id, &second_instance, &second_generation);
    CHECK(second_id == first_id);
    CHECK(second_instance > first_instance);
    CHECK(toka_task_try_schedule_token(first_id, first_instance, 1) == 0);
    CHECK(toka_wait_registry_allocate_token(
              first_id, first_instance, 1, 1, NULL, NULL) == 0);

    CHECK(toka_task_start(second));
    CHECK(toka_task_try_schedule_token(
              second_id, second_instance, 1) == 1);
    uint64_t popped_id = 0;
    uint64_t popped_generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&popped_id, &popped_generation, &worker));
    CHECK(worker == second);
    toka_task_clear_current(worker);
    toka_task_release(worker);
    toka_task_release(second);
}

static void test_checked_retain_rejects_overflow_zero_and_stale_pointer(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *owner = toka_task_create(frame, NULL);
    CHECK(owner != NULL);

    uint64_t task_id = 0;
    uint64_t task_instance = 0;
    uint64_t generation = 0;
    toka_tcb_get_wait_token_with_instance(
        owner, &task_id, &task_instance, &generation);
    CHECK(task_id != 0);
    CHECK(toka_rt_test_set_tcb_ref_count(owner, UINT32_MAX));
    CHECK(toka_task_try_retain(owner) == 0);
    CHECK(toka_task_try_schedule_token(task_id, task_instance, generation) == 0);
    CHECK(toka_wait_registry_allocate_token(
              task_id, task_instance, generation, 1, NULL, NULL) == 0);
    CHECK(toka_rt_live_wait_registry_count() == 0);

    CHECK(toka_rt_test_set_tcb_ref_count(owner, 0));
    CHECK(toka_task_try_retain(owner) == 0);
    CHECK(toka_rt_test_set_tcb_ref_count(owner, 1));
    CHECK(toka_task_try_retain(owner));
    toka_task_release(owner);
    toka_task_release(owner);
    CHECK(toka_task_try_retain(owner) == 0);
    // Runtime entry points that receive a task pointer must also reject the
    // stale identity before dereferencing it. A failed operation is not a
    // surrogate access path around the checked-retain protocol.
    CHECK(toka_task_start(owner) == 0);
    CHECK(toka_task_request_cancel(owner) == 0);
    CHECK(toka_tcb_get_promise(owner) == NULL);
    CHECK(toka_tcb_is_done(owner));
    CHECK(!toka_tcb_is_canceled(owner));
    uint64_t stale_id = UINT64_C(99);
    uint64_t stale_instance = UINT64_C(99);
    uint64_t stale_generation = UINT64_C(99);
    toka_tcb_get_wait_token_with_instance(
        owner, &stale_id, &stale_instance, &stale_generation
    );
    CHECK(stale_id == 0);
    CHECK(stale_instance == 0);
    CHECK(stale_generation == 0);
    free(frame);
}

static void test_worker_frame_access_is_pinned_and_current_only(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *owner = toka_task_create(frame, NULL);
    CHECK(owner != NULL);
    CHECK(toka_task_get_current_coro_frame() == NULL);
    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t task_instance = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_rt_test_get_frame_access_state(worker) == 1);
    CHECK(toka_task_get_current_coro_frame() == frame);

    toka_task_clear_current(worker);
    CHECK(toka_rt_test_get_frame_access_state(owner) == 0);
    CHECK(toka_task_get_current_coro_frame() == NULL);
    toka_task_release(worker);
    toka_task_release(owner);
    free(frame);
}

static void test_nested_worker_context_restores_outer_frame_pin(void) {
    void *outer_frame = calloc(1, 64);
    void *inner_frame = calloc(1, 64);
    CHECK(outer_frame != NULL);
    CHECK(inner_frame != NULL);
    void *outer_owner = toka_task_create(outer_frame, NULL);
    void *inner_owner = toka_task_create(inner_frame, NULL);
    CHECK(outer_owner != NULL);
    CHECK(inner_owner != NULL);
    CHECK(toka_task_start(outer_owner));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *outer_worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &outer_worker));
    CHECK(outer_worker == outer_owner);
    CHECK(toka_task_get_current_coro_frame() == outer_frame);

    // A task may run the default executor recursively. Clearing that nested
    // task must restore the outer worker rather than losing its frame pin.
    CHECK(toka_task_start(inner_owner));
    void *inner_worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &inner_worker));
    CHECK(inner_worker == inner_owner);
    CHECK(toka_task_get_current_coro_frame() == inner_frame);
    toka_task_clear_current(inner_worker);
    CHECK(toka_task_get_current_coro_frame() == outer_frame);
    toka_task_clear_current(outer_worker);
    CHECK(toka_task_get_current_coro_frame() == NULL);

    toka_task_release(inner_worker);
    toka_task_release(inner_owner);
    toka_task_release(outer_worker);
    toka_task_release(outer_owner);
    free(inner_frame);
    free(outer_frame);
}

static void test_wait_slot_exhaustion_retires_the_slot(void) {
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *owner = toka_task_create(frame, NULL);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t task_instance = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);

    uint32_t first_id = 0;
    uint32_t first_generation = 0;
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &task_instance, &generation));
    CHECK(toka_wait_registry_allocate_token(
        task_id, task_instance, generation, 1, &first_id, &first_generation));
    CHECK(toka_rt_test_set_wait_slot_generation(first_id, UINT32_MAX));
    CHECK(toka_wait_registry_release(first_id, UINT32_MAX));
    CHECK(toka_wait_registry_try_wake(first_id, UINT32_MAX) == 0);
    CHECK(toka_task_abort_suspend(frame));

    uint32_t second_id = 0;
    uint32_t second_generation = 0;
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &task_instance, &generation));
    CHECK(toka_wait_registry_allocate_token(
        task_id, task_instance, generation, 1, &second_id, &second_generation));
    CHECK(second_id != first_id);
    CHECK(toka_wait_registry_release(second_id, second_generation));
    CHECK(toka_task_abort_suspend(frame));

    toka_task_clear_current(worker);
    toka_task_release(worker);
    toka_task_release(owner);
    free(frame);
}

static void test_wait_set_identity_exhaustion_installs_nothing(void) {
    enum { MEMBERS = 3 };
    void *frame = calloc(1, 64);
    CHECK(frame != NULL);
    void *owner = toka_task_create(frame, NULL);
    CHECK(owner != NULL);
    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t task_instance = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);
    CHECK(toka_task_prepare_suspend_token(
        frame, &task_id, &task_instance, &generation));

    uint32_t ids[MEMBERS] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t slot_generations[MEMBERS] = {
        UINT32_MAX, UINT32_MAX, UINT32_MAX};
    CHECK(toka_rt_test_set_next_wait_set_id(UINT64_MAX));
    CHECK(!toka_wait_registry_allocate_nway_token(
        task_id, task_instance, generation, 1, MEMBERS, ids,
        slot_generations));
    for (uint32_t i = 0; i < MEMBERS; ++i) {
        CHECK(ids[i] == UINT32_MAX);
        CHECK(slot_generations[i] == UINT32_MAX);
    }
    CHECK(toka_rt_live_wait_registry_count() == 0);
    CHECK(toka_ready_queue_count() == 0);
    CHECK(toka_task_abort_suspend(frame));

    toka_task_clear_current(worker);
    toka_task_release(worker);
    toka_task_release(owner);
    free(frame);
}

static void test_await_prepare_rejects_stale_participants(void) {
    void *parent_frame = calloc(1, 64);
    CHECK(parent_frame != NULL);
    void *parent = toka_task_create(parent_frame, NULL);
    CHECK(parent != NULL);
    CHECK(toka_task_start(parent));
    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));

    // Create and retire the stale child after the parent is live. A raw
    // address is not an identity token, so a later allocator reuse would be a
    // different live task rather than a meaningful stale-pointer probe.
    FakePromise stale_child_promise = {0};
    void *stale_child = toka_task_create(NULL, &stale_child_promise);
    CHECK(stale_child != NULL);
    toka_task_release(stale_child);
    CHECK(!toka_task_register_cancel_child(parent_frame, stale_child));
    CHECK(!toka_task_await_prepare(&stale_child_promise, parent));
    toka_task_clear_current(worker);
    toka_task_release(worker);
    toka_task_release(parent);
    free(parent_frame);

    FakePromise live_child_promise = {0};
    void *live_child = toka_task_create(NULL, &live_child_promise);
    CHECK(live_child != NULL);
    void *stale_parent = toka_task_create(NULL, NULL);
    CHECK(stale_parent != NULL);
    toka_task_release(stale_parent);
    CHECK(!toka_task_await_prepare(&live_child_promise, stale_parent));
    toka_task_release(live_child);
}

int main(void) {
    test_schedule_generation_exhaustion_is_nontransitioning();
    test_checked_retain_rejects_overflow_zero_and_stale_pointer();
    test_worker_frame_access_is_pinned_and_current_only();
    test_nested_worker_context_restores_outer_frame_pin();
    test_wait_slot_exhaustion_retires_the_slot();
    test_wait_set_identity_exhaustion_installs_nothing();
    test_await_prepare_rejects_stale_participants();
    test_reused_numeric_id_rejects_stale_instance_token();
    test_task_identity_exhaustion_installs_nothing();
    puts("async identity exhaustion passed");
    return 0;
}
