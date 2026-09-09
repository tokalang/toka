#define _POSIX_C_SOURCE 200809L
#include "thread_handoff_v1_test_hooks.h"
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int mode, opened, published, finished, join_failures, detach_failures;
static atomic_int attempts, drops[3];
static pthread_t creator;
static _Thread_local int locks;

int thread_test_begin(void) {
    const char *value = getenv("TOKA_THREAD_TEST_MODE");
    mode = value ? atoi(value) : 0;
    creator = pthread_self();
    opened = !(mode == 3 || mode == 5 || mode == 7 || mode == 8);
    join_failures = mode == 2 || mode == 11;
    detach_failures = mode == 3 || mode == 8 || mode == 12;
    return mode;
}
void thread_test_wait(void) {
    assert(locks == 0);
    pthread_mutex_lock(&gate);
    while (!opened) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
}
void thread_test_release(void) {
    pthread_mutex_lock(&gate);
    opened = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
}
void thread_test_finished(void) {
    pthread_mutex_lock(&gate);
    while (!finished) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
}
static void wait_published(void) {
    pthread_mutex_lock(&gate);
    while (!published) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
}
void thread_test_drop(int kind) {
    assert(locks == 0 && (kind == 1 || kind == 2));
    if (kind == 1) {
        int canceled = mode == 1 || mode == 9 || mode == 10;
        assert(!!pthread_equal(creator, pthread_self()) == canceled);
    } else if (mode == 5 || mode == 7) {
        assert(!pthread_equal(creator, pthread_self()));
    } else {
        assert(pthread_equal(creator, pthread_self()));
    }
    assert(atomic_fetch_add(&drops[kind], 1) == 0);
}
int thread_test_count(int kind) { return atomic_load(&drops[kind]); }
void *toka_test_alloc(size_t size) {
    assert(locks == 0);
    int attempt = atomic_fetch_add(&attempts, 1) + 1;
    if ((mode == 9 && attempt == 1) || (mode == 10 && attempt == 2)) return NULL;
    return malloc(size);
}
void toka_test_free(void *value) { assert(locks == 0); free(value); }
int toka_test_aligned(void **out, size_t alignment, size_t size) {
    assert(locks == 0); return posix_memalign(out, alignment, size);
}
int toka_test_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    return pthread_mutex_init(mutex, attr);
}
int toka_test_mutex_lock(pthread_mutex_t *mutex) {
    int result = pthread_mutex_lock(mutex); if (!result) ++locks; return result;
}
int toka_test_mutex_unlock(pthread_mutex_t *mutex) {
    assert(locks == 1); --locks; return pthread_mutex_unlock(mutex);
}
int toka_test_create(pthread_t *thread, const pthread_attr_t *attr,
                     void *(*entry)(void *), void *argument) {
    assert(locks == 0);
    if (mode == 1) return EAGAIN;
    int result = pthread_create(thread, attr, entry, argument);
    if (!result && (mode == 4 || mode == 6 || mode == 12 || mode == 13)) wait_published();
    return result;
}
int toka_test_join(pthread_t thread, void **value) {
    assert(locks == 0);
    if (join_failures) {
        --join_failures;
        if (mode == 11) wait_published();
        return EDEADLK;
    }
    return pthread_join(thread, value);
}
int toka_test_detach(pthread_t thread) {
    assert(locks == 0);
    if (detach_failures) { --detach_failures; return EBUSY; }
    return pthread_detach(thread);
}
void toka_test_event(int event) {
    pthread_mutex_lock(&gate);
    if (event == TOKA_TEST_WORKER_PUBLISHED) ++published;
    else if (event == TOKA_TEST_CONTROL_FREED) ++finished;
    else abort();
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
}
