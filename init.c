#include <stdio.h>
#include "sqlite/sqlite3.h"
#include "init.h"

// 建表 SQL — 自动化装机工具数据库 schema
const char* kCreateTableSQL =
    "CREATE TABLE IF NOT EXISTS categories ("
    "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE"
    ");"

    "CREATE TABLE IF NOT EXISTS apps ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name        TEXT    NOT NULL UNIQUE,"
    "  category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL,"
    "  priority    INTEGER NOT NULL DEFAULT 0,"
    "  description TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_apps_category ON apps(category_id);"

    "CREATE TABLE IF NOT EXISTS app_platforms ("
    "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  app_id         INTEGER NOT NULL REFERENCES apps(id) ON DELETE CASCADE,"
    "  platform       TEXT    NOT NULL CHECK (platform IN "
    "    ('Windows','macOS','Linux','FreeBSD')),"
    "  is_manual      INTEGER NOT NULL DEFAULT 0 CHECK (is_manual IN (0, 1)),"
    "  download_url   TEXT,"
    "  installer_path TEXT,"
    "  UNIQUE(app_id, platform)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_app_platforms_plat  ON app_platforms(platform);"

    "CREATE TABLE IF NOT EXISTS app_configs ("
    "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  app_platform_id INTEGER NOT NULL REFERENCES app_platforms(id) ON DELETE CASCADE,"
    "  config_path     TEXT,"
    "  script_type     TEXT,"
    "  script_path     TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_app_configs_platform "
    "  ON app_configs(app_platform_id);"
;

// initDatabaseOpen: open / create the database, set PRAGMA foreign_keys,
// execute createSQL, return the open handle.  Caller must sqlite3_close().
sqlite3 *initDatabaseOpen(const char *dbPath, const char *createSQL) {
    sqlite3 *db = NULL;
    char *errMsg = NULL;

    if (sqlite3_open(dbPath, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_open failed: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return NULL;
    }

    // PRAGMA foreign_keys must be executed separately from CREATE TABLE,
    // otherwise it may be silently ignored inside a multi-statement exec.
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    if (sqlite3_exec(db, createSQL, NULL, NULL, &errMsg) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_exec failed: %s\n", errMsg);
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return NULL;
    }

    return db;
}

int initDatabase(const char *dbPath, const char *sql) {
    sqlite3 *db = initDatabaseOpen(dbPath, sql);
    if (!db) return 0;
    sqlite3_close(db);
    return 1;
}
