// Test-only targets for scoped substitutions in the generated Arena methods.
// No process-wide malloc/free interposition or production runtime changes.
#include <stdint.h>
#include <stdlib.h>

static struct { void *pointer; int live; } slots[32];
static uint64_t attempts, allocated, freed, live, fail_at;
static int violation;

void b2_arena_begin(uint64_t failure) {
    if (live) abort();
    attempts = allocated = freed = live = 0;
    fail_at = failure;
    violation = 0;
    for (unsigned i = 0; i < 32; ++i) slots[i].live = 0;
}

void *b2_arena_malloc(uint64_t size) {
    ++attempts;
    if (fail_at && attempts == fail_at) return NULL;
    void *pointer = malloc((size_t)size);
    if (!pointer) abort();
    for (unsigned i = 0; i < 32; ++i) if (!slots[i].live) {
        slots[i].pointer = pointer;
        slots[i].live = 1;
        ++allocated;
        ++live;
        return pointer;
    }
    abort();
}

void b2_arena_free(void *pointer) {
    if (!pointer) return;
    for (unsigned i = 0; i < 32; ++i) if (slots[i].live && slots[i].pointer == pointer) {
        slots[i].live = 0;
        --live;
        ++freed;
        free(pointer);
        return;
    }
    violation = 1; // Observe a duplicate/unowned release without invoking UB.
}

int32_t b2_arena_check(uint64_t a, uint64_t n, uint64_t f, uint64_t l) {
    return violation || attempts != a || allocated != n || freed != f || live != l;
}
