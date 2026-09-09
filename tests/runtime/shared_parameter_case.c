#include <stdlib.h>
int toka_shared_case(void) {
    const char *value = getenv("TOKA_SHARED_CASE");
    return value ? atoi(value) : 0;
}
