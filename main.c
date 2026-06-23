#include <stdio.h>
#include "init.h"
#include "test.h"
#include "tui.h"

/*
 * 建表 SQL —— 自动化运维 / 装机工具数据库初始化脚本
 *
 * 表设计理念：
 *   1. 平台与配置解耦 —— 软件主表 + 多张子表，避免单表过宽
 *   2. 分类字典化管理 —— 支持用户自定义扩展
 *   3. 外键级联 —— 保证数据一致性
 */
const char* kCreateTableSQL =
    /* 分类字典表：存储软件分类名称（如"文件管理"、"聊天通信"） */
    "CREATE TABLE IF NOT EXISTS categories ("
    "  id   INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE"
    ");"

    /* 软件主表：记录软件名称、所属分类及简介 */
    "CREATE TABLE IF NOT EXISTS apps ("
    "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name        TEXT    NOT NULL UNIQUE,"
    "  category_id INTEGER REFERENCES categories(id) ON DELETE SET NULL,"
    "  priority    INTEGER NOT NULL DEFAULT 0,"
    "  description TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_apps_category ON apps(category_id);"

    /* 软件平台与下载子表：同一软件在不同平台下的下载链接与安装包路径 */
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

    /* 配置文件与脚本子表：记录每个平台版本对应的配置备份路径与初始化脚本 */
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

int main(void) {
    if (runTests() != 0) {
        fprintf(stderr, "\nTests failed — aborting.\n");
        return 1;
    }

    if (!initDatabase("app.db", kCreateTableSQL)) {
        fprintf(stderr, "Database initialization failed.\n");
        return 1;
    }

    return runTuiLoop();
}
