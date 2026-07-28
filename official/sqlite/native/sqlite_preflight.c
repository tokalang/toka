#include <stddef.h>
#include <sqlite3.h>
#include <stdint.h>

static int toka_sqlite_live_handles = 0;

uintptr_t toka_sqlite_open_path(uintptr_t path) {
  sqlite3 *db = NULL;
  if (path == 0)
    return 0;
  if (sqlite3_open_v2((const char *)path, &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      NULL) != SQLITE_OK) {
    if (db != NULL)
      sqlite3_close(db);
    return 0;
  }
  toka_sqlite_live_handles++;
  return (uintptr_t)db;
}

int toka_sqlite_execute(uintptr_t handle, uintptr_t sql) {
  if (handle == 0 || sql == 0)
    return SQLITE_MISUSE;
  return sqlite3_exec((sqlite3 *)handle, (const char *)sql, NULL, NULL, NULL);
}

int toka_sqlite_close(uintptr_t handle) {
  if (handle == 0)
    return SQLITE_OK;
  int status = sqlite3_close((sqlite3 *)handle);
  if (status == SQLITE_OK)
    toka_sqlite_live_handles--;
  return status;
}

int toka_sqlite_live_handle_count(void) { return toka_sqlite_live_handles; }

// Keep raw SQLite handles inside the native boundary.  The Toka preflight only
// observes a status code, proving the selected package can link and execute.
int toka_sqlite_preflight(void) {
  sqlite3 *db = NULL;
  int status = sqlite3_open_v2(":memory:", &db,
                               SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                   SQLITE_OPEN_FULLMUTEX,
                               NULL);
  if (status != SQLITE_OK) {
    if (db != NULL)
      sqlite3_close(db);
    return status;
  }

  status = sqlite3_exec(db,
                        "CREATE TABLE probe (value INTEGER);"
                        "INSERT INTO probe VALUES (1);",
                        NULL, NULL, NULL);
  int close_status = sqlite3_close(db);
  if (status != SQLITE_OK)
    return status;
  return close_status;
}
