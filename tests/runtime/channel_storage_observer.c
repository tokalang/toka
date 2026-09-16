#include <stdatomic.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
static _Atomic int counts[65];
static _Atomic int total;
void channel_drop(int id) {
    if (id < 0 || id >= 65 || atomic_fetch_add(&counts[id], 1) != 0) abort();
    atomic_fetch_add(&total, 1);
}
int channel_drop_count(void) { return atomic_load(&total); }
static _Atomic int fail_lock, fail_init, fail_alloc;
void channel_fail_lock(void) { atomic_store(&fail_lock, 1); }
void channel_fail_init(void) { atomic_store(&fail_init, 1); }
void channel_fail_alloc(void) { atomic_store(&fail_alloc, 1); }
void *channel_native_malloc(size_t size) {
    if (atomic_exchange(&fail_alloc, 0)) {
        write(2, "CHANNEL_ALLOC_FAULT\n", 20);
        return NULL;
    }
    return malloc(size);
}
int channel_native_lock(uintptr_t value) {
    if (atomic_exchange(&fail_lock, 0)) {
        write(2, "CHANNEL_LOCK_FAULT\n", 19);
        return 5;
    }
    return pthread_mutex_lock((pthread_mutex_t *)value);
}
int channel_native_init(uintptr_t value, uintptr_t attr) {
    if (atomic_exchange(&fail_init, 0)) {
        write(2, "CHANNEL_INIT_FAULT\n", 19);
        return 5;
    }
    return pthread_mutex_init((pthread_mutex_t *)value, (pthread_mutexattr_t *)attr);
}
void channel_unexpected_publish(void) { _Exit(99); }
