#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _DEFAULT_SOURCE 1
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

static int active, mode, kind, category, allocations, drops;
static int init_calls, destroy_calls, payload_frees, native_frees;
static void *payload, *native, *last_allocation;
static size_t last_size;
static int *shared_count;
static int public_owner, owner_frees, counter_frees;
static void *owner_storage, *owner_counter;

static int environment(const char *name) { const char *s = getenv(name); return s ? atoi(s) : 0; }
int toka_sync_test_mode(void) { return environment("TOKA_SYNC_MODE"); }
int toka_sync_test_kind(void) { return environment("TOKA_SYNC_KIND"); }
int toka_sync_test_category(void) { return environment("TOKA_SYNC_CATEGORY"); }
int toka_sync_test_public_owner(void) { return environment("TOKA_SYNC_PUBLIC_OWNER"); }
static void terminate_test(int code) {
    syscall(SYS_exit, code);
    __builtin_unreachable();
}
void toka_sync_test_arm(int value) {
    mode = toka_sync_test_mode(); kind = toka_sync_test_kind(); category = value;
    public_owner = environment("TOKA_SYNC_PUBLIC_OWNER");
    if (category == 3) {
        if (!last_allocation || last_size != sizeof(int) || *(int *)last_allocation != 2)
            terminate_test(151);
        shared_count = last_allocation;
    }
    active = 1;
}
void toka_sync_test_drop(void) { if (active) ++drops; }

#if defined(__APPLE__)
#define WRAP(name) test_##name
#define REAL(name) name
#define INTERPOSE(name) __attribute__((used)) static const struct { const void *replacement, *original; } pair_##name __attribute__((section("__DATA,__interpose"))) = { (const void *)&WRAP(name), (const void *)&name };
#else
#define WRAP(name) __wrap_##name
#define REAL(name) __real_##name
#define INTERPOSE(name)
extern void *__real_malloc(size_t);
extern void __real_free(void *);
extern int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int __real_pthread_mutex_destroy(pthread_mutex_t *);
extern int __real_pthread_mutex_lock(pthread_mutex_t *);
extern int __real_pthread_mutex_unlock(pthread_mutex_t *);
extern int __real_pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *);
extern int __real_pthread_rwlock_destroy(pthread_rwlock_t *);
extern int __real_pthread_rwlock_rdlock(pthread_rwlock_t *);
extern int __real_pthread_rwlock_wrlock(pthread_rwlock_t *);
extern int __real_pthread_rwlock_unlock(pthread_rwlock_t *);
extern int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int __real_pthread_cond_destroy(pthread_cond_t *);
extern int __real_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int __real_pthread_cond_signal(pthread_cond_t *);
extern int __real_pthread_cond_broadcast(pthread_cond_t *);
#endif

void *WRAP(malloc)(size_t size) {
    if (active) {
        ++allocations;
        if ((kind == 2 && allocations == 1 && size < sizeof(pthread_cond_t)) ||
            (kind == 0 && allocations == 2 && size < sizeof(pthread_mutex_t)) ||
            (kind == 1 && allocations == 2 && size < sizeof(pthread_rwlock_t)))
            terminate_test(154);
        if ((mode == 1 && allocations == 1) || (mode == 2 && allocations == 2) ||
            (public_owner && mode == 11 && allocations == 3) ||
            (public_owner && mode == 12 && allocations == 4)) return NULL;
    }
    void *p = REAL(malloc)(size);
    if (!active) { last_allocation = p; last_size = size; }
    else if (allocations == 1 && kind == 2) native = p;
    else if (allocations == 1) payload = p;
    else if (allocations == 2) native = p;
    else if (public_owner && allocations == 3) owner_storage = p;
    else if (public_owner && allocations == 4) owner_counter = p;
    return p;
}
void WRAP(free)(void *p) {
    if (active && p) {
        if (p == payload) ++payload_frees;
        if (p == native) ++native_frees;
        if (p == owner_storage) ++owner_frees;
        if (p == owner_counter) ++counter_frees;
    }
    REAL(free)(p);
}
static int fatal_state_ok(void) {
    if (kind == 2) {
        if (drops || payload_frees) return 0;
        if (mode == 1) return allocations == 1 && !native_frees && !init_calls && !destroy_calls;
        if (mode == 3) return native_frees == 1 && init_calls == 1 && !destroy_calls;
        if (mode == 6) return !native_frees && destroy_calls == 1;
        return (mode == 8 || mode == 9) && !native_frees && !destroy_calls;
    }
    const int cleaned_input = mode <= 3 || mode == 6 || mode == 11 || mode == 12;
    const int expected_drops = category == 0 || category == 3 || !cleaned_input ? 0 : 1;
    if (drops != expected_drops) return 0;
    if (shared_count && *shared_count != (cleaned_input ? 1 : 2)) return 0;
    if (mode == 1) return allocations == 1 && !payload_frees && !native_frees && !init_calls && !destroy_calls;
    if (mode == 2) return allocations == 2 && payload_frees == 1 && !native_frees && !init_calls && !destroy_calls;
    if (mode == 3) return allocations == 2 && payload_frees == 1 && native_frees == 1 && init_calls == 1 && !destroy_calls;
    if (mode == 6) return payload_frees == 1 && !native_frees && destroy_calls == 1;
    if (public_owner && (mode == 11 || mode == 12))
        return allocations == (mode == 11 ? 3 : 4) && payload_frees == 1 && native_frees == 1 &&
               init_calls == 1 && destroy_calls == 1 && owner_frees == (mode == 12) && !counter_frees;
    return mode >= 5 && !payload_frees && !native_frees && !destroy_calls;
}
void WRAP(_Exit)(int code) {
    const int cleaned_input = mode <= 3 || mode == 6 || mode == 11 || mode == 12;
    const int expected_drops = category == 0 || category == 3 || !cleaned_input ? 0 : 1;
    if (active && drops != expected_drops) terminate_test(160 + drops);
    if (active && shared_count && *shared_count != (cleaned_input ? 1 : 2))
        terminate_test(170 + (*shared_count & 15));
    terminate_test(code == 134 && active && fatal_state_ok() ? 134 : 152);
}
int toka_sync_test_done(void) {
    int ok = (mode == 0 || mode == 4 || mode == 10) && payload_frees == (kind == 2 ? 0 : 1) && native_frees == 1 &&
             init_calls == 1 && destroy_calls == 1 && drops == (category == 0 ? 0 : 1);
    if (public_owner) ok &= owner_frees == 1 && counter_frees == (public_owner == 2);
    active = 0;
    return ok ? 0 : 153;
}

int WRAP(pthread_mutex_init)(pthread_mutex_t *p, const pthread_mutexattr_t *a) {
    if (active && kind == 0) { ++init_calls; if (mode == 3) return 11; }
    return REAL(pthread_mutex_init)(p, a);
}
int WRAP(pthread_rwlock_init)(pthread_rwlock_t *p, const pthread_rwlockattr_t *a) {
    if (active && kind == 1) { ++init_calls; if (mode == 3) return 11; }
    return REAL(pthread_rwlock_init)(p, a);
}
int WRAP(pthread_cond_init)(pthread_cond_t *p, const pthread_condattr_t *a) {
    if (active && kind == 2) { ++init_calls; if (mode == 3) return 11; }
    return REAL(pthread_cond_init)(p, a);
}
int WRAP(pthread_mutex_destroy)(pthread_mutex_t *p) {
    if (active && (void *)p == native) { ++destroy_calls; if (mode == 6) return 16; }
    return REAL(pthread_mutex_destroy)(p);
}
int WRAP(pthread_rwlock_destroy)(pthread_rwlock_t *p) {
    if (active && (void *)p == native) { ++destroy_calls; if (mode == 6) return 16; }
    return REAL(pthread_rwlock_destroy)(p);
}
int WRAP(pthread_cond_destroy)(pthread_cond_t *p) {
    if (active && (void *)p == native) { ++destroy_calls; if (mode == 6) return 16; }
    return REAL(pthread_cond_destroy)(p);
}
int WRAP(pthread_mutex_lock)(pthread_mutex_t *p) { return active && mode == 4 ? 16 : REAL(pthread_mutex_lock)(p); }
int WRAP(pthread_rwlock_rdlock)(pthread_rwlock_t *p) { return active && mode == 4 ? 16 : REAL(pthread_rwlock_rdlock)(p); }
int WRAP(pthread_rwlock_wrlock)(pthread_rwlock_t *p) { return active && mode == 4 ? 16 : REAL(pthread_rwlock_wrlock)(p); }
int WRAP(pthread_mutex_unlock)(pthread_mutex_t *p) { return active && mode == 5 ? 1 : REAL(pthread_mutex_unlock)(p); }
int WRAP(pthread_rwlock_unlock)(pthread_rwlock_t *p) { return active && mode == 5 ? 1 : REAL(pthread_rwlock_unlock)(p); }
int WRAP(pthread_cond_wait)(pthread_cond_t *p, pthread_mutex_t *m) { return active && mode == 7 ? 1 : REAL(pthread_cond_wait)(p, m); }
int WRAP(pthread_cond_signal)(pthread_cond_t *p) { return active && mode == 8 ? 1 : REAL(pthread_cond_signal)(p); }
int WRAP(pthread_cond_broadcast)(pthread_cond_t *p) { return active && mode == 9 ? 1 : REAL(pthread_cond_broadcast)(p); }
INTERPOSE(malloc) INTERPOSE(free) INTERPOSE(_Exit)
INTERPOSE(pthread_mutex_init) INTERPOSE(pthread_mutex_destroy) INTERPOSE(pthread_mutex_lock) INTERPOSE(pthread_mutex_unlock)
INTERPOSE(pthread_rwlock_init) INTERPOSE(pthread_rwlock_destroy) INTERPOSE(pthread_rwlock_rdlock) INTERPOSE(pthread_rwlock_wrlock) INTERPOSE(pthread_rwlock_unlock)
INTERPOSE(pthread_cond_init) INTERPOSE(pthread_cond_destroy) INTERPOSE(pthread_cond_wait) INTERPOSE(pthread_cond_signal) INTERPOSE(pthread_cond_broadcast)
