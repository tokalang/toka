#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _DEFAULT_SOURCE 1
#endif
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

static int active, kind, shared, fault, allocations, freed[12], initialized, destroyed;
static void *blocks[12];
static int env(const char *name) { const char *v = getenv(name); return v ? atoi(v) : 0; }
static void finish(int code) { syscall(SYS_exit, code); __builtin_unreachable(); }
int toka_composite_kind(void) { return env("TOKA_COMPOSITE_KIND"); }
int toka_composite_shared(void) { return env("TOKA_COMPOSITE_SHARED"); }
void toka_composite_arm(void) {
    kind = toka_composite_kind(); shared = toka_composite_shared();
    fault = env("TOKA_COMPOSITE_FAULT"); active = 1;
}
#if defined(__APPLE__)
#define WRAP(n) test_##n
#define REAL(n) n
#define INTERPOSE(n) __attribute__((used)) static const struct { const void *replacement, *original; } pair_##n __attribute__((section("__DATA,__interpose"))) = { (const void *)&WRAP(n), (const void *)&n };
#else
#define WRAP(n) __wrap_##n
#define REAL(n) __real_##n
#define INTERPOSE(n)
extern void *__real_malloc(size_t);
extern void __real_free(void *);
extern int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int __real_pthread_mutex_destroy(pthread_mutex_t *);
extern int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
extern int __real_pthread_cond_destroy(pthread_cond_t *);
#endif
void *WRAP(malloc)(size_t size) {
    if (!active) return REAL(malloc)(size);
    if (++allocations >= 12) finish(150);
    /* Faults are deliberately after the complete prepared source exists. */
    if ((fault == 1 && allocations == kind + 3) ||
        (fault == 2 && allocations == kind + 4)) return NULL;
    return blocks[allocations] = REAL(malloc)(size);
}
void WRAP(free)(void *p) {
    if (active && p) {
        for (int i = 1; i <= allocations; ++i)
            if (blocks[i] == p && ++freed[i] != 1) finish(151);
    }
    REAL(free)(p);
}
static int complete(void) {
    if (initialized != kind + 1 || destroyed != initialized) return 0;
    if (allocations != kind + 3 + (shared && fault != 1)) return 0;
    for (int i = 1; i <= allocations; ++i)
        if (freed[i] != (blocks[i] != NULL)) return 0;
    return 1;
}
int toka_composite_done(void) {
    int ok = complete(); active = 0; return ok ? 0 : 152;
}
void WRAP(_Exit)(int code) { finish(code == 134 && fault && complete() ? 134 : 153); }
int WRAP(pthread_mutex_init)(pthread_mutex_t *p, const pthread_mutexattr_t *a) {
    int rc = REAL(pthread_mutex_init)(p, a); if (active && !rc) ++initialized; return rc;
}
int WRAP(pthread_cond_init)(pthread_cond_t *p, const pthread_condattr_t *a) {
    int rc = REAL(pthread_cond_init)(p, a); if (active && !rc) ++initialized; return rc;
}
int WRAP(pthread_mutex_destroy)(pthread_mutex_t *p) {
    int rc = REAL(pthread_mutex_destroy)(p); if (active && !rc) ++destroyed; return rc;
}
int WRAP(pthread_cond_destroy)(pthread_cond_t *p) {
    int rc = REAL(pthread_cond_destroy)(p); if (active && !rc) ++destroyed; return rc;
}
INTERPOSE(malloc) INTERPOSE(free) INTERPOSE(_Exit)
INTERPOSE(pthread_mutex_init) INTERPOSE(pthread_mutex_destroy)
INTERPOSE(pthread_cond_init) INTERPOSE(pthread_cond_destroy)
