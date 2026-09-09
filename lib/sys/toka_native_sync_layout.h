#ifndef TOKA_NATIVE_SYNC_LAYOUT_H
#define TOKA_NATIVE_SYNC_LAYOUT_H
/* Storage reserves used by the private std/sync adapters. This emits no ABI
 * entry or runtime witness. Supported POSIX runtime builds check real headers,
 * not a guessed opaque pthread layout. Other targets remain unqualified. */
#if !defined(_WIN32)
#include <pthread.h>
#include <stddef.h>
#if defined(__cplusplus)
#define TOKA_SYNC_ASSERT static_assert
#define TOKA_SYNC_ALIGNOF alignof
#else
#define TOKA_SYNC_ASSERT _Static_assert
#define TOKA_SYNC_ALIGNOF _Alignof
#endif
TOKA_SYNC_ASSERT(sizeof(pthread_mutex_t) <= 64, "native mutex reserve is too small");
TOKA_SYNC_ASSERT(sizeof(pthread_rwlock_t) <= 256, "native rwlock reserve is too small");
TOKA_SYNC_ASSERT(sizeof(pthread_cond_t) <= 64, "native condition reserve is too small");
TOKA_SYNC_ASSERT(TOKA_SYNC_ALIGNOF(pthread_mutex_t) <= TOKA_SYNC_ALIGNOF(max_align_t), "mutex malloc alignment");
TOKA_SYNC_ASSERT(TOKA_SYNC_ALIGNOF(pthread_rwlock_t) <= TOKA_SYNC_ALIGNOF(max_align_t), "rwlock malloc alignment");
TOKA_SYNC_ASSERT(TOKA_SYNC_ALIGNOF(pthread_cond_t) <= TOKA_SYNC_ALIGNOF(max_align_t), "condition malloc alignment");
#undef TOKA_SYNC_ASSERT
#undef TOKA_SYNC_ALIGNOF
#endif
#endif
