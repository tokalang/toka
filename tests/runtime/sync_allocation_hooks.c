// Test-only malloc injection, armed immediately before the i32 storage allocation.
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
static int armed;
void toka_sync_fail_next_allocation(void) { armed = 1; }
#if defined(__APPLE__)
static void *sync_test_malloc(size_t size) {
    if (armed) {
        armed = 0;
        if (size != sizeof(int)) _exit(97);
        return NULL;
    }
    return malloc(size);
}
__attribute__((used)) static const struct { const void *replacement, *original; }
sync_malloc_interpose __attribute__((section("__DATA,__interpose"))) = {
    (const void *)&sync_test_malloc, (const void *)&malloc
};
#else
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size) {
    if (armed) {
        armed = 0;
        if (size != sizeof(int)) _exit(97);
        return NULL;
    }
    return __real_malloc(size);
}
#endif
