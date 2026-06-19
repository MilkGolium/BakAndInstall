#include <stdio.h>
#include <string.h>
#include "sqlite/sqlite3.h"
#include "dbAPI.h"
#include "deploy.h"

extern const char* kCreateTableSQL;

static int  gPassed = 0;
static int  gFailed = 0;
static int  gSuite  = 0;  // test suite flag for counting

static void t(const char *name, int ok) {
    if (ok) {
        printf("  PASS  %s\n", name);
        gPassed++;
    } else {
        printf("  FAIL  %s\n", name);
        gFailed++;
    }
}

static void suite(const char *name) {
    if (gSuite) printf("\n");
    printf("--- %s ---\n", name);
    gSuite = 1;
}

// -- helpers for query verification --

typedef struct {
    int count;
} CountResult;

static int countCallback(void *ctx, const AppInfo *app) {
    (void)app;
    ((CountResult *)ctx)->count++;
    return 0;
}

static int countRows(sqlite3 *db, const char *sql, int bind) {
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK)
        return -1;
    if (bind >= 0)
        sqlite3_bind_int(s, 1, bind);
    int n = 0;
    while (sqlite3_step(s) == SQLITE_ROW)
        n++;
    sqlite3_finalize(s);
    return n;
}

int runTests(void) {
    printf("\n===== Running tests =====\n\n");

    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        fprintf(stderr, "FATAL: cannot open :memory: database\n");
        return 1;
    }
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    if (sqlite3_exec(db, kCreateTableSQL, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "FATAL: cannot create schema\n");
        sqlite3_close(db);
        return 1;
    }

    // ================================================================
    suite("Category API");
    // ================================================================
    {
        t("addCategory creates a category",
          addCategory(db, "file manager"));
        t("addCategory creates another",
          addCategory(db, "communication"));
        t("addCategory rejects duplicate name",
          !addCategory(db, "file manager"));

        // removeCategory
        t("removeCategory deletes by id",
          removeCategory(db, 1));          // "file manager"
        t("removeCategory on nonexistent id returns 1",
          removeCategory(db, 999));
    }

    // ================================================================
    suite("App API");
    // ================================================================
    {
        // Re-add category 1 so later cascade test is clean
        addCategory(db, "file manager");     // id = 3

        t("addApp with category",
          addApp(db, "Total Commander", 3, 0, "A file manager"));
        t("addApp without category (category_id <= 0)",
          addApp(db, "Standalone Tool", -1, 5, NULL));
        t("addApp rejects duplicate name",
          !addApp(db, "Total Commander", 3, 0, NULL));
        t("addApp accepts empty description",
          addApp(db, "Minimal App", 3, 10, ""));

        // removeApp
        t("removeApp deletes by id",
          removeApp(db, 2));   // "Standalone Tool" (id=2)
    }

    // ================================================================
    suite("AppPlatform API");
    // ================================================================
    {
        // app 1 = "Total Commander", app 2 = deleted, app 3 = "Minimal App"
        t("addAppPlatform with download_url",
          addAppPlatform(db, 1, "macOS", 0, "https://example.com/tc.dmg", NULL));
        t("addAppPlatform with installer_path",
          addAppPlatform(db, 1, "Windows", 1, NULL, "setup.exe"));
        t("addAppPlatform rejects duplicate (app_id, platform)",
          !addAppPlatform(db, 1, "macOS", 0, "https://x.com", NULL));
        t("addAppPlatform for another app",
          addAppPlatform(db, 3, "Linux", 0, "https://example.com/min.tar.gz", NULL));

        int linuxPlatformId = (int)sqlite3_last_insert_rowid(db);
        t("removeAppPlatform deletes by id",
          linuxPlatformId > 0 && removeAppPlatform(db, linuxPlatformId));
    }

    // ================================================================
    suite("AppConfig API");
    // ================================================================
    {
        // app_plat 1 = "Total Commander / macOS"
        // app_plat 2 = "Total Commander / Windows"
        // app_plat 3 = deleted (Minimal App / Linux)

        t("addAppConfig with config_path only",
          addAppConfig(db, 1, "/home/user/.config/tc", NULL, NULL));
        t("addAppConfig with script only",
          addAppConfig(db, 1, NULL, "Shell", "/opt/tc/setup.sh"));
        t("addAppConfig with both config and script",
          addAppConfig(db, 2, "C:\\Users\\user\\AppData\\tc", "Bat", "C:\\tc\\setup.bat"));
        t("addAppConfig with empty text params (treated as NULL)",
          addAppConfig(db, 2, "", "", ""));
    }

    // ================================================================
    suite("Cascade (foreign key)");
    // ================================================================
    {
        t("removeApp cascades to app_platforms",
          removeApp(db, 1));

        t("app_platforms for deleted app are gone",
          countRows(db,
            "SELECT 1 FROM app_platforms WHERE app_id = ?1", 1) == 0);

        t("app_configs cascade-deleted with platforms",
          countRows(db, "SELECT 1 FROM app_configs", -1) == 0);

        t("removeCategory cascades SET NULL on related apps",
          removeCategory(db, 3));

        t("orphaned apps still exist with NULL category",
          countRows(db,
            "SELECT 1 FROM apps WHERE category_id IS NULL AND id = ?1", 3) == 1);
    }

    // ================================================================
    suite("Query: queryAppsByPlatform");
    // ================================================================
    {
        addCategory(db, "dev tools");               // id = 4
        addApp(db, "VSCode",  4, 1, NULL);          // id = 4
        addApp(db, "Node.js", 4, 0, "JS runtime");  // id = 5
        addApp(db, "iTerm2",  4, 2, NULL);          // id = 6
        addAppPlatform(db, 4, "macOS", 0, "https://code.visualstudio.com", NULL);
        addAppPlatform(db, 5, "macOS", 0, "https://nodejs.org", NULL);
        addAppPlatform(db, 6, "macOS", 0, "https://iterm2.com", NULL);
        addAppPlatform(db, 5, "Windows", 0, "https://nodejs.org/dist", NULL);

        CountResult cr = {0};
        t("queryAppsByPlatform returns 3 for macOS",
          queryAppsByPlatform(db, "macOS", countCallback, &cr) && cr.count == 3);

        CountResult cr2 = {0};
        t("queryAppsByPlatform returns 0 for Linux",
          queryAppsByPlatform(db, "Linux", countCallback, &cr2) && cr2.count == 0);
    }

    // ================================================================
    suite("Deploy: deployAppConfig");
    // ================================================================
    {
        addCategory(db, "deploy-test");
        addApp(db, "TestApp", 5, 0, NULL);
        addAppPlatform(db, 7, "macOS", 0, NULL, NULL);
        int platId = (int)sqlite3_last_insert_rowid(db);

        t("deployAppConfig returns 1 with config_path (same-path copyPath)",
          platId > 0 &&
          addAppConfig(db, platId, "/tmp/test_backup", NULL, NULL) &&
          deployAppConfig(db, platId));

        t("deployAppConfig handles multiple config rows",
          addAppConfig(db, platId, "/tmp/another_backup", NULL, NULL) &&
          deployAppConfig(db, platId));

        t("deployAppConfig returns 0 when a script row fails (no real script)",
          addAppConfig(db, platId, NULL, "Shell", "/nonexistent/script.sh") &&
          !deployAppConfig(db, platId));
    }

    // ================================================================
    printf("\n===== Results: %d passed, %d failed =====\n",
           gPassed, gFailed);

    sqlite3_close(db);
    return gFailed > 0 ? 1 : 0;
}
