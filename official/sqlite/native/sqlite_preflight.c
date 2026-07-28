#include <stddef.h>
#include <sqlite3.h>

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
