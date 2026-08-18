#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define close _close
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/event.h>
#endif

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,       \
                    __LINE__, #cond);                                        \
            abort();                                                         \
        }                                                                    \
    } while (0)

typedef struct {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
} FakePromise;

extern void *toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern int toka_task_request_cancel(void *tcb_ptr);
extern int toka_task_is_current_canceled(void *tcb_ptr);
extern void toka_task_complete_canceled(void *promise_ptr);
extern int toka_tcb_is_done(void *tcb_ptr);
extern int toka_tcb_is_canceled(void *tcb_ptr);
extern int toka_task_pop_ready(uint64_t *out_task_id, uint64_t *out_gen, void **out_tcb_ptr);
extern int toka_task_prepare_suspend_token(void *frame_ptr, uint64_t *out_task_id, uint64_t *out_task_instance, uint64_t *out_gen);
extern int toka_wait_registry_allocate_token(uint64_t task_id, uint64_t task_instance, uint64_t generation, uint32_t op_type, uint32_t *out_wait_id, uint32_t *out_slot_gen);
extern int toka_task_commit_suspend(void *frame_ptr);
extern void toka_task_clear_current(void *tcb_ptr);
extern void toka_task_release(void *tcb_ptr);
extern int toka_wait_registry_try_wake(uint32_t wait_id, uint32_t slot_gen);
extern int toka_reactor_add_read(int rfd, int fd, uint64_t event_key);
extern int toka_reactor_del_read(int rfd, int fd, uint64_t event_key);
extern int toka_reactor_wait(int rfd, int timeout_ms, uint64_t *ready_keys, int max_keys);
extern uint32_t toka_rt_live_tcb_count(void);
extern uint32_t toka_rt_live_wait_registry_count(void);
extern int toka_rt_test_reactor_is_fd_registered(int fd);

typedef struct MockIoCoroFrame {
    void (*resume_fn)(void *);
    void (*destroy_fn)(void *);
    void *tcb;
    FakePromise promise;
    int fd;
    uint64_t event_key;
    uint32_t wait_id;
    uint32_t slot_gen;
    int rfd;
    int cancel_observed;
    int cleaned_up;
} MockIoCoroFrame;

static void mock_io_coro_destroy(void *frame_ptr) {
    free(frame_ptr);
}

static void mock_io_coro_resume(void *frame_ptr) {
    MockIoCoroFrame *frame = (MockIoCoroFrame *)frame_ptr;
    if (toka_task_is_current_canceled(frame->tcb)) {
        frame->cancel_observed = 1;
        // Coroutine cancellation cleanup: unregister reactor key on cancel
        if (frame->event_key != 0 && frame->rfd >= 0) {
            toka_reactor_del_read(frame->rfd, frame->fd, frame->event_key);
        }
        // Publish completed-canceled terminal outcome from coroutine cancel path
        toka_task_complete_canceled(&frame->promise);
        frame->cleaned_up = 1;
    }
}

static void test_task_cancel_timer_logical_invalidation(void) {
    uint32_t base_tcb = toka_rt_live_tcb_count();
    uint32_t base_waits = toka_rt_live_wait_registry_count();

    MockIoCoroFrame *frame = calloc(1, sizeof(MockIoCoroFrame));
    CHECK(frame != NULL);
    frame->resume_fn = mock_io_coro_resume;
    frame->destroy_fn = mock_io_coro_destroy;
    frame->rfd = -1;
    frame->fd = -1;

    void *owner = toka_task_create(frame, &frame->promise);
    CHECK(owner != NULL);
    frame->tcb = owner;

    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);

    uint64_t task_instance = 0;
    CHECK(toka_task_prepare_suspend_token(frame, &task_id, &task_instance, &generation));

    uint32_t wait_id = 0;
    uint32_t slot_gen = 0;
    CHECK(toka_wait_registry_allocate_token(task_id, task_instance, generation, 1, &wait_id, &slot_gen));
    frame->wait_id = wait_id;
    frame->slot_gen = slot_gen;

    CHECK(toka_task_commit_suspend(frame));
    toka_task_clear_current(worker);
    toka_task_release(worker);

    CHECK(toka_rt_live_wait_registry_count() == base_waits + 1);

    // Cancel the suspended timer task
    CHECK(toka_task_request_cancel(owner) == 1);

    // Pop the ready worker and resume it through coroutine path
    void *popped_worker = NULL;
    uint64_t pop_id = 0;
    uint64_t pop_gen = 0;
    CHECK(toka_task_pop_ready(&pop_id, &pop_gen, &popped_worker));
    CHECK(popped_worker == owner);

    mock_io_coro_resume(frame);
    CHECK(frame->cancel_observed == 1);
    CHECK(frame->cleaned_up == 1);
    CHECK(toka_tcb_is_done(owner));
    CHECK(toka_tcb_is_canceled(owner));

    toka_task_clear_current(popped_worker);
    toka_task_release(popped_worker);

    // Token was invalidated and released by wake; a late timer expiry try_wake must return 0
    CHECK(toka_wait_registry_try_wake(wait_id, slot_gen) == 0);
    CHECK(toka_rt_live_wait_registry_count() == base_waits);

    toka_task_release(owner);

    CHECK(toka_rt_live_tcb_count() == base_tcb);
    puts("  test_task_cancel_timer_logical_invalidation passed");
}

static void test_task_cancel_tcp_reactor_os_silence(void) {
#ifdef _WIN32
    puts("  test_task_cancel_tcp_reactor_os_silence skipped on Windows");
#else
    uint32_t base_tcb = toka_rt_live_tcb_count();
    uint32_t base_waits = toka_rt_live_wait_registry_count();

    int fds[2] = {-1, -1};
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int flags0 = fcntl(fds[0], F_GETFL, 0);
    int flags1 = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[0], F_SETFL, flags0 | O_NONBLOCK);
    fcntl(fds[1], F_SETFL, flags1 | O_NONBLOCK);

    int rfd = -1;
#ifdef __APPLE__
    rfd = kqueue();
#else
    rfd = epoll_create1(0);
#endif
    CHECK(rfd >= 0);

    MockIoCoroFrame *frame = calloc(1, sizeof(MockIoCoroFrame));
    CHECK(frame != NULL);
    frame->resume_fn = mock_io_coro_resume;
    frame->destroy_fn = mock_io_coro_destroy;

    void *owner = toka_task_create(frame, &frame->promise);
    CHECK(owner != NULL);
    frame->tcb = owner;
    frame->fd = fds[0];
    frame->rfd = rfd;

    CHECK(toka_task_start(owner));

    uint64_t task_id = 0;
    uint64_t generation = 0;
    void *worker = NULL;
    CHECK(toka_task_pop_ready(&task_id, &generation, &worker));
    CHECK(worker == owner);

    uint64_t task_instance = 0;
    CHECK(toka_task_prepare_suspend_token(frame, &task_id, &task_instance, &generation));

    uint32_t wait_id = 0;
    uint32_t slot_gen = 0;
    CHECK(toka_wait_registry_allocate_token(task_id, task_instance, generation, 1, &wait_id, &slot_gen));
    frame->wait_id = wait_id;
    frame->slot_gen = slot_gen;

    uint64_t event_key = ((uint64_t)wait_id << 32) | (uint64_t)slot_gen;
    frame->event_key = event_key;

    CHECK(toka_reactor_add_read(rfd, fds[0], event_key) == 0);
    CHECK(toka_rt_test_reactor_is_fd_registered(fds[0]) == 1);

    CHECK(toka_task_commit_suspend(frame));
    toka_task_clear_current(worker);
    toka_task_release(worker);

    // Cancel the suspended TCP task
    CHECK(toka_task_request_cancel(owner) == 1);

    // Pop the ready worker and resume through coroutine dispatch
    void *popped_worker = NULL;
    uint64_t pop_id = 0;
    uint64_t pop_gen = 0;
    CHECK(toka_task_pop_ready(&pop_id, &pop_gen, &popped_worker));
    CHECK(popped_worker == owner);

    mock_io_coro_resume(frame);
    CHECK(frame->cancel_observed == 1);
    CHECK(frame->cleaned_up == 1);
    CHECK(toka_tcb_is_done(owner));
    CHECK(toka_tcb_is_canceled(owner));

    toka_task_clear_current(popped_worker);
    toka_task_release(popped_worker);

    // Verify reactor table registration is removed
    CHECK(toka_rt_test_reactor_is_fd_registered(fds[0]) == 0);

    // OS-level silence proof: write byte to fds[1] to make fds[0] readable
    char byte = 'X';
    CHECK(write(fds[1], &byte, 1) == 1);

    // Wait on OS reactor; because registration was deleted by cancel path, delivered event count MUST be 0
    uint64_t ready_keys[8] = {0};
    int ready_count = toka_reactor_wait(rfd, 50, ready_keys, 8);
    CHECK(ready_count == 0);

    close(fds[0]);
    close(fds[1]);
    close(rfd);

    toka_task_release(owner);

    CHECK(toka_rt_live_wait_registry_count() == base_waits);
    CHECK(toka_rt_live_tcb_count() == base_tcb);
    puts("  test_task_cancel_tcp_reactor_os_silence passed");
#endif
}

int main(void) {
    puts("Running async task I/O cancel reactor cleanup tests...");
    test_task_cancel_timer_logical_invalidation();
    test_task_cancel_tcp_reactor_os_silence();
    puts("All async task I/O cancel reactor cleanup tests passed.");
    return 0;
}
