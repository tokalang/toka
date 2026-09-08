#ifndef TOKA_THREAD_HANDOFF_V1_TEST_HOOKS_H
#define TOKA_THREAD_HANDOFF_V1_TEST_HOOKS_H
#include <stddef.h>
#include <pthread.h>
enum { TOKA_TEST_WORKER_PUBLISHED = 1, TOKA_TEST_CONTROL_FREED = 2 };
void *toka_test_alloc(size_t size);
void toka_test_free(void *pointer);
int toka_test_aligned(void **out, size_t alignment, size_t size);
int toka_test_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int toka_test_join(pthread_t, void **);
int toka_test_detach(pthread_t);
int toka_test_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int toka_test_mutex_lock(pthread_mutex_t *);
int toka_test_mutex_unlock(pthread_mutex_t *);
void toka_test_event(int event);
#endif
