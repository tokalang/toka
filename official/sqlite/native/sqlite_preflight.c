#include <stddef.h>
#include <sqlite3.h>
#include <stdint.h>

static int toka_sqlite_live_handles = 0;
static int toka_sqlite_live_statements = 0;

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

uintptr_t toka_sqlite_prepare(uintptr_t handle, uintptr_t sql) {
  sqlite3_stmt *statement = NULL;
  if (handle == 0 || sql == 0)
    return 0;
  if (sqlite3_prepare_v2((sqlite3 *)handle, (const char *)sql, -1,
                         &statement, NULL) != SQLITE_OK)
    return 0;
  toka_sqlite_live_statements++;
  return (uintptr_t)statement;
}

int toka_sqlite_bind_i64(uintptr_t statement, int index, int64_t value) {
  if (statement == 0)
    return SQLITE_MISUSE;
  return sqlite3_bind_int64((sqlite3_stmt *)statement, index, value);
}

int toka_sqlite_step(uintptr_t statement) {
  if (statement == 0)
    return SQLITE_MISUSE;
  return sqlite3_step((sqlite3_stmt *)statement);
}

int64_t toka_sqlite_column_i64(uintptr_t statement, int index) {
  return sqlite3_column_int64((sqlite3_stmt *)statement, index);
}

int toka_sqlite_reset(uintptr_t statement) {
  if (statement == 0)
    return SQLITE_MISUSE;
  return sqlite3_reset((sqlite3_stmt *)statement);
}

int toka_sqlite_finalize(uintptr_t statement) {
  if (statement == 0)
    return SQLITE_OK;
  int status = sqlite3_finalize((sqlite3_stmt *)statement);
  toka_sqlite_live_statements--;
  return status;
}

int toka_sqlite_live_statement_count(void) { return toka_sqlite_live_statements; }

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
