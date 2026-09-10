#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#include <pthread.h>

static pthread_mutex_t *observed;
static int unlocks;
#if defined(__APPLE__)
#define REAL(name) name
#define WRAP(name) audit_##name
#define INTERPOSE(name) __attribute__((used)) static const struct { const void *replace; const void *original; } pair_##name __attribute__((section("__DATA,__interpose"))) = { (const void *)&WRAP(name), (const void *)&name };
#else
#define REAL(name) __real_##name
#define WRAP(name) __wrap_##name
#define INTERPOSE(name)
extern int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
extern int __real_pthread_mutex_unlock(pthread_mutex_t *);
#endif
int WRAP(pthread_mutex_init)(pthread_mutex_t *p, const pthread_mutexattr_t *a) {
    int rc = REAL(pthread_mutex_init)(p, a);
    if (!rc && !observed) observed = p;
    return rc;
}
int WRAP(pthread_mutex_unlock)(pthread_mutex_t *p) {
    if (p == observed) ++unlocks;
    return REAL(pthread_mutex_unlock)(p);
}
int audit_unlock_count(void) { return unlocks; }
INTERPOSE(pthread_mutex_init)
INTERPOSE(pthread_mutex_unlock)
