#include <stdio.h>
#include "init.h"
#include "tui.h"
#include "sqlite/sqlite3.h"

int main(void) {
    sqlite3 *db = initDatabaseOpen("app.db", kCreateTableSQL);
    if (!db) {
        fprintf(stderr, "Database initialization failed.\n");
        return 1;
    }

    int ret = runTuiLoop(db);
    sqlite3_close(db);
    return ret;
}
