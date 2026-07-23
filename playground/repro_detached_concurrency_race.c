// Controlled 20,000-iteration C Runtime ABI Concurrency Redline Probe
// Verifies ZERO underflow on g_active_detached_task_count under heavy pthread race conditions.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>

extern void* toka_task_create(void *coro_frame, void *promise);
extern int toka_task_start(void *tcb_ptr);
extern void toka_task_detach(void *tcb_ptr);
extern void toka_task_complete(void *promise_ptr);
extern void toka_task_release(void *tcb_ptr);
extern uint32_t toka_task_active_detached_count(void);

struct TokaPromiseHeader {
    _Atomic uint8_t result_state;
    void *self_tcb;
    _Atomic uintptr_t continuation;
};

typedef struct {
    void *tcb;
    struct TokaPromiseHeader promise;
} TaskPair;

static void* detach_worker(void *arg) {
    TaskPair *pair = (TaskPair*)arg;
    toka_task_detach(pair->tcb);
    return NULL;
}

static void* complete_worker(void *arg) {
    TaskPair *pair = (TaskPair*)arg;
    toka_task_complete(&pair->promise);
    return NULL;
}

int main(void) {
    printf("Starting 20,000-iteration concurrent detach/completion race probe...\n");

    for (int iter = 1; iter <= 20000; iter++) {
        TaskPair pair;
        pair.tcb = toka_task_create(NULL, &pair.promise);

        // Start task so state transitions out of Created into Running/Queued to test counted path
        toka_task_start(pair.tcb);

        pthread_t t1, t2;
        pthread_create(&t1, NULL, detach_worker, &pair);
        pthread_create(&t2, NULL, complete_worker, &pair);

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        // Release executor reference
        toka_task_release(pair.tcb);

        uint32_t count = toka_task_active_detached_count();
        if (count != 0) {
            printf("[FAILED] Iteration %d reproduced race! active_detached_count=%u (UINT32_MAX underflow!)\n", iter, count);
            assert(count == 0);
        }
    }

    printf("PASSED 20,000/20,000 iterations! active_detached_count is 0 with ZERO underflow/race.\n");
    return 0;
}
