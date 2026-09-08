/* A second translation unit producing the same complete semantic descriptor.
 * Its equivalent callbacks must never replace the producer's own callbacks. */
#include "toka_thread_handoff_v1.h"
#include <stdlib.h>
#include <string.h>

typedef struct { int value; int *resource; } EquivalentValue;
static unsigned calls;
static void move_equivalent(void *source, void *destination) {
    ++calls;
    memcpy(destination, source, sizeof(EquivalentValue));
    ((EquivalentValue *)source)->resource = NULL;
}
static void drop_equivalent(void *source) {
    ++calls;
    free(((EquivalentValue *)source)->resource);
    ((EquivalentValue *)source)->resource = NULL;
}
static const TokaThreadResultOpsV1 ops = {
    1, sizeof(TokaThreadResultOpsV1), TOKA_THREAD_HANDOFF_ABI_V1,
    "test/module::Value/i32+owned-int/target-native/interface-18", sizeof(EquivalentValue),
    _Alignof(EquivalentValue), move_equivalent, drop_equivalent
};
const TokaThreadResultOpsV1 *toka_test_equivalent_value_ops(void) { return &ops; }
unsigned toka_test_equivalent_thunk_calls(void) { return calls; }
