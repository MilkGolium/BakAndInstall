#ifndef INIT_H
#define INIT_H

#include "sqlite/sqlite3.h"

// initDatabase: open / create the database, execute the given SQL, then close.
// Legacy convenience wrapper.  Returns 1 on success, 0 on failure.
int initDatabase(const char *dbPath, const char *sql);

// initDatabaseOpen: like initDatabase but keeps the database open and
// returns the handle.  Sets PRAGMA foreign_keys and executes createSQL.
// Caller must close the handle with sqlite3_close() when done.
// Returns NULL on failure.
sqlite3 *initDatabaseOpen(const char *dbPath, const char *createSQL);

// kCreateTableSQL 建表 SQL 常量（由 init.c 定义）
extern const char *kCreateTableSQL;

#endif // INIT_H
